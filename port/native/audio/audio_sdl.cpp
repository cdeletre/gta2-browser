// SDL2 audio back end for the native build (jalon 5): the native counterpart of
// web/audio.js, behind the same EM_JS bridges port/audio/miles_web.cpp calls.
//
// Effects: PCM from the game's sound bank (8-bit unsigned or 16-bit, mono or stereo),
// played at the sample's rate with Miles-style volume (0..127), pan (0..127, 64 = centre;
// equal-power, as Web Audio's StereoPannerNode) and loop points. The bank bytes are
// converted once and cached by address, length, format and a sparse checksum, as
// web/audio.js does (banks are reloaded at the same address on a district change).
// Streams (speech, radio, music): 16-bit PCM or IMA ADPCM WAV files (wav_ima.cpp), read
// into memory when opened and decoded about half a second at a time, up to a second
// ahead, on the game's thread (Gta2Native_AudioPump, once per loop iteration); the audio
// callback only mixes. As in web/audio.js, a stream's status and position follow the wall
// clock (scaled by its playback rate), so game logic keeps the original pacing whatever
// the audio device does; after an underrun the decoder resyncs to the wall clock.
//
// The mixer runs in SDL's audio callback; the game thread changes its state under
// SDL_LockAudioDevice. Output: 44100 Hz stereo, 16-bit (SDL converts if the device wants
// something else).
#if !defined(GTA2_NATIVE_HEADLESS)
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <emscripten.h> // emscripten_get_now (port/native/compat)

#include "wav_ima.h"

// port_prefix.h (forced into every file) routes fopen/fclose to the game's versions; the
// stream paths arrive already resolved (gta2_port_normalise_path, miles_web.cpp).
#undef fopen
#undef fclose

namespace
{
constexpr int kBankRate = 22050;     // web/audio.js BANK_RATE
constexpr double kAheadS = 1.0;      // web/audio.js AHEAD_S
constexpr double kChunkS = 0.5;      // web/audio.js CHUNK_S

SDL_AudioDeviceID gDevice = 0;
int gOutRate = 44100;
float gMaster = 1.0f;

struct Pcm
{
    std::vector<float> data; // interleaved
    int channels = 1;
    size_t frames = 0;
};

struct Voice
{
    std::shared_ptr<Pcm> pcm;
    double pos = 0;         // source frame
    double step = 1;        // source frames per output frame
    int loops = 1;          // 1 once, 0 forever, n times
    double loopStart = 0;   // frames
    double loopEnd = 0;     // frames, 0 = the whole buffer
    double played = 0;      // source frames advanced (for loops > 1)
    float volume = 1;
    float pan = 0;          // -1..1
};

struct Stream
{
    // The WAV file mapped into memory: the decoder reads it 0.5 s at a time, so opening a
    // radio track (up to 32 MB) no longer reads it all at once in the game's thread (a
    // stall of up to 1.5 s from an SD card, TrimUI Smart Pro S).
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    ~Stream()
    {
        if (bytes)
        {
            munmap(const_cast<uint8_t*>(bytes), size);
        }
    }
    WavInfo wav;
    bool radio = false;
    int loops = 1;
    double rateFactor = 1;
    int volume = 127;
    bool started = false;
    double baseMs = 0;
    double startWall = 0;
    double durationMs = 0;
    size_t cursorFrame = 0;    // next frame to decode
    std::deque<float> fifo;    // decoded frames, interleaved by channel (callback side)
    double readPos = 0;        // fractional frame position in the fifo
    bool underrun = false;     // set by the callback, cleared by the pump
    bool resync = true;        // the pump positions the decoder from the wall clock
    double waitSince = 0;      // when the pump started waiting for this stream's data
};

// Byte offset in the file of a stream's next frame to decode.
size_t CursorOffset(const Stream& st)
{
    const WavInfo& wav = st.wav;
    if (wav.tag == 0x11 && wav.samplesPerBlock > 0)
    {
        return wav.data + st.cursorFrame / static_cast<size_t>(wav.samplesPerBlock) * static_cast<size_t>(wav.blockAlign);
    }
    return wav.data + st.cursorFrame * 2 * static_cast<size_t>(wav.channels);
}

// True when the next kResidentBytes from the cursor are in memory (or cannot be checked),
// so that decoding them will not wait for the SD card.
bool Resident(const Stream& st)
{
    constexpr size_t kResidentBytes = 64 * 1024;
    const size_t page = 4096;
    const size_t offset = CursorOffset(st);
    if (!st.bytes || offset >= st.size)
    {
        return true;
    }
    const size_t start = offset / page * page;
    const size_t end = offset + kResidentBytes < st.size ? offset + kResidentBytes : st.size;
    unsigned char vec[(kResidentBytes / 4096) + 2];
    if (mincore(const_cast<uint8_t*>(st.bytes) + start, end - start, vec) != 0)
    {
        return true;
    }
    for (size_t i = 0; i < (end - start + page - 1) / page; ++i)
    {
        if (!(vec[i] & 1))
        {
            return false;
        }
    }
    return true;
}

// Asks the kernel to read the next part of a mapped stream in the background, so that the
// decoder (in the game's thread) finds it in memory instead of waiting for the SD card
// (RG CubeXX: up to 150 ms for one 0.5 s chunk).
void Prefetch(const Stream& st)
{
    constexpr size_t kAheadBytes = 1 << 20;
    const WavInfo& wav = st.wav;
    size_t offset = wav.data;
    if (wav.tag == 0x11 && wav.samplesPerBlock > 0)
    {
        offset += st.cursorFrame / static_cast<size_t>(wav.samplesPerBlock) * static_cast<size_t>(wav.blockAlign);
    }
    else
    {
        offset += st.cursorFrame * 2 * static_cast<size_t>(wav.channels);
    }
    if (!st.bytes || offset >= st.size)
    {
        return;
    }
    const size_t page = 4096;
    const size_t start = offset / page * page;
    const size_t end = offset + kAheadBytes < st.size ? offset + kAheadBytes : st.size;
    madvise(const_cast<uint8_t*>(st.bytes) + start, end - start, MADV_WILLNEED);
}

std::unordered_map<int, Voice> gVoices;
std::map<std::string, std::shared_ptr<Pcm>> gBankCache;
std::unordered_map<int, std::unique_ptr<Stream>> gStreams;

double NowMs()
{
    return emscripten_get_now();
}

float Clamp(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

struct Lock
{
    Lock()
    {
        if (gDevice)
        {
            SDL_LockAudioDevice(gDevice);
        }
    }
    ~Lock()
    {
        if (gDevice)
        {
            SDL_UnlockAudioDevice(gDevice);
        }
    }
};

// Web Audio's StereoPannerNode (equal power).
void Pan(float pan, int channels, float inL, float inR, float* outL, float* outR)
{
    if (channels == 1)
    {
        const float x = (pan + 1) / 2;
        *outL = inL * cosf(x * static_cast<float>(M_PI) / 2);
        *outR = inL * sinf(x * static_cast<float>(M_PI) / 2);
        return;
    }
    if (pan <= 0)
    {
        const float x = pan + 1;
        *outL = inL + inR * cosf(x * static_cast<float>(M_PI) / 2);
        *outR = inR * sinf(x * static_cast<float>(M_PI) / 2);
    }
    else
    {
        const float x = pan;
        *outL = inL * cosf(x * static_cast<float>(M_PI) / 2);
        *outR = inR + inL * sinf(x * static_cast<float>(M_PI) / 2);
    }
}

std::shared_ptr<Pcm> BankPcm(const uint8_t* ptr, int len, int format)
{
    const bool sixteen = (format & 1) != 0;
    const bool stereo = (format & 2) != 0;
    int sum = 0; // as web/audio.js bufferFor
    const int stride = (len >> 5) > 1 ? (len >> 5) : 1;
    for (int i = 0; i < len; i += stride)
    {
        sum = static_cast<int>(static_cast<unsigned>(sum) * 31u + ptr[i]);
    }
    char key[96];
    snprintf(key, sizeof(key), "%p:%d:%d:%d", static_cast<const void*>(ptr), len, format, sum);
    auto it = gBankCache.find(key);
    if (it != gBankCache.end())
    {
        return it->second;
    }
    auto pcm = std::make_shared<Pcm>();
    pcm->channels = stereo ? 2 : 1;
    const int bytesPerFrame = (sixteen ? 2 : 1) * pcm->channels;
    pcm->frames = static_cast<size_t>(len / bytesPerFrame > 0 ? len / bytesPerFrame : 1);
    pcm->data.resize(pcm->frames * pcm->channels);
    for (size_t f = 0; f < pcm->frames && static_cast<int>(f) * bytesPerFrame < len; ++f)
    {
        for (int c = 0; c < pcm->channels; ++c)
        {
            const uint8_t* p = ptr + f * bytesPerFrame + c * (sixteen ? 2 : 1);
            pcm->data[f * pcm->channels + c] =
                sixteen ? static_cast<int16_t>(p[0] | (p[1] << 8)) / 32768.0f : (static_cast<int>(*p) - 128) / 128.0f;
        }
    }
    if (gBankCache.size() > 600)
    {
        gBankCache.clear(); // voices keep their own reference
    }
    gBankCache[key] = pcm;
    return pcm;
}

double StreamElapsedMs(const Stream& st)
{
    return st.baseMs + (NowMs() - st.startWall) * st.rateFactor;
}

float StreamGain(const Stream& st)
{
    return Clamp(static_cast<float>(st.volume), 0, 127) / 127.0f;
}

// --- mixing (audio thread, device locked) ------------------------------------------
std::vector<float> gMix;

void MixVoice(Voice& v, float* out, int frames, bool* done)
{
    const Pcm& pcm = *v.pcm;
    const double end = v.loops != 1 && v.loopEnd > v.loopStart ? v.loopEnd : static_cast<double>(pcm.frames);
    const double start = v.loops != 1 && v.loopEnd > v.loopStart ? v.loopStart : 0;
    const double maxPlay = v.loops > 1 ? static_cast<double>(pcm.frames) * v.loops : 0;
    for (int i = 0; i < frames; ++i)
    {
        if (v.pos >= end)
        {
            if (v.loops == 1)
            {
                *done = true;
                return;
            }
            v.pos = start + fmod(v.pos - start, end - start > 0 ? end - start : 1);
        }
        if (maxPlay > 0 && v.played >= maxPlay)
        {
            *done = true;
            return;
        }
        const size_t i0 = static_cast<size_t>(v.pos);
        const size_t i1 = i0 + 1 < pcm.frames ? i0 + 1 : i0;
        const float t = static_cast<float>(v.pos - static_cast<double>(i0));
        float l = pcm.data[i0 * pcm.channels] * (1 - t) + pcm.data[i1 * pcm.channels] * t;
        float r = l;
        if (pcm.channels == 2)
        {
            r = pcm.data[i0 * 2 + 1] * (1 - t) + pcm.data[i1 * 2 + 1] * t;
        }
        float ol;
        float orr;
        Pan(v.pan, pcm.channels, l, r, &ol, &orr);
        out[2 * i] += ol * v.volume;
        out[2 * i + 1] += orr * v.volume;
        v.pos += v.step;
        v.played += v.step;
    }
}

void MixStream(Stream& st, float* out, int frames)
{
    if (!st.started)
    {
        return;
    }
    const int ch = st.wav.channels;
    const double step = st.wav.rate * st.rateFactor / gOutRate;
    const float gain = StreamGain(st);
    for (int i = 0; i < frames; ++i)
    {
        const size_t have = st.fifo.size() / static_cast<size_t>(ch);
        const size_t i0 = static_cast<size_t>(st.readPos);
        if (i0 + 1 >= have)
        {
            if (st.loops == 0 || StreamElapsedMs(st) < st.durationMs * st.loops)
            {
                st.underrun = true; // still playing by the clock: the decoder fell behind
            }
            return;
        }
        const float t = static_cast<float>(st.readPos - static_cast<double>(i0));
        const float l = st.fifo[i0 * ch] * (1 - t) + st.fifo[(i0 + 1) * ch] * t;
        const float r = ch == 2 ? st.fifo[i0 * 2 + 1] * (1 - t) + st.fifo[(i0 + 1) * 2 + 1] * t : l;
        out[2 * i] += l * gain;
        out[2 * i + 1] += r * gain;
        st.readPos += step;
        const size_t consumed = static_cast<size_t>(st.readPos);
        if (consumed > 0)
        {
            st.fifo.erase(st.fifo.begin(), st.fifo.begin() + static_cast<long>(consumed * ch));
            st.readPos -= static_cast<double>(consumed);
        }
    }
}

void Callback(void*, Uint8* stream, int len)
{
    const int frames = len / 4; // S16 stereo
    gMix.assign(static_cast<size_t>(frames) * 2, 0.0f);
    for (auto it = gVoices.begin(); it != gVoices.end();)
    {
        bool done = false;
        MixVoice(it->second, gMix.data(), frames, &done);
        it = done ? gVoices.erase(it) : std::next(it);
    }
    for (auto& kv : gStreams)
    {
        MixStream(*kv.second, gMix.data(), frames);
    }
    int16_t* out = reinterpret_cast<int16_t*>(stream);
    for (int i = 0; i < frames * 2; ++i)
    {
        const float v = Clamp(gMix[i] * gMaster, -1.0f, 1.0f);
        out[i] = static_cast<int16_t>(lrintf(v * 32767.0f));
    }
}

// --- streams: decoding ahead (game thread) -----------------------------------------
void PumpStream(Stream& st)
{
    if (!st.started)
    {
        return;
    }
    const WavInfo& wav = st.wav;
    size_t queued;
    {
        Lock lock;
        if (st.underrun)
        {
            st.underrun = false;
            st.resync = true;
        }
        if (st.resync)
        {
            st.fifo.clear();
            st.readPos = 0;
        }
        queued = st.fifo.size() / static_cast<size_t>(wav.channels);
    }
    if (st.resync)
    {
        // First chunk, after a seek or an underrun: position from the wall clock.
        st.resync = false;
        const double elapsed = StreamElapsedMs(st);
        if (st.loops == 0 || elapsed < st.durationMs * st.loops)
        {
            st.cursorFrame = static_cast<size_t>(fmod(elapsed, st.durationMs > 0 ? st.durationMs : 1) / 1000.0 * wav.rate);
        }
    }
    const size_t aheadFrames = static_cast<size_t>(kAheadS * wav.rate);
    std::vector<std::vector<float>> channels;
    while (queued < aheadFrames)
    {
        if (st.loops != 0 && StreamElapsedMs(st) >= st.durationMs * st.loops)
        {
            return;
        }
        if (st.cursorFrame >= wav.frames)
        {
            if (st.loops == 1)
            {
                return;
            }
            st.cursorFrame = 0;
        }
        // Data not in memory yet (a radio station starts mid-track, from the wall clock): read
        // it in the background and decode at a later pump, rather than stall the game; after
        // 300 ms decode anyway.
        if (!Resident(st))
        {
            const double now = NowMs();
            if (st.waitSince == 0)
            {
                st.waitSince = now;
                Prefetch(st);
            }
            if (now - st.waitSince < 300)
            {
                return;
            }
        }
        st.waitSince = 0;
        size_t frames;
        size_t skip = 0;
        if (wav.tag == 0x11)
        {
            const size_t firstBlock = st.cursorFrame / static_cast<size_t>(wav.samplesPerBlock);
            if (firstBlock >= wav.blocks)
            {
                st.cursorFrame = wav.frames;
                continue;
            }
            size_t count = static_cast<size_t>(ceil(kChunkS * wav.rate / wav.samplesPerBlock));
            count = count < 1 ? 1 : count;
            count = count < wav.blocks - firstBlock ? count : wav.blocks - firstBlock;
            DecodeImaBlocks(st.bytes, wav, firstBlock, count, &channels);
            skip = st.cursorFrame - firstBlock * static_cast<size_t>(wav.samplesPerBlock);
            frames = channels[0].size() - skip;
        }
        else
        {
            frames = wav.frames - st.cursorFrame;
            const size_t chunk = static_cast<size_t>(ceil(kChunkS * wav.rate));
            frames = frames < chunk ? frames : chunk;
            DecodePcm16(st.bytes, wav, st.cursorFrame, frames, &channels);
        }
        if (!frames)
        {
            st.cursorFrame = wav.frames;
            continue;
        }
        {
            Lock lock;
            for (size_t f = 0; f < frames; ++f)
            {
                for (int c = 0; c < wav.channels; ++c)
                {
                    st.fifo.push_back(channels[c][skip + f]);
                }
            }
        }
        st.cursorFrame += frames;
        queued += frames;
        Prefetch(st);
    }
}
} // namespace

extern "C" {

// After the window (SDL is initialised). GTA2_AUDIO=0 turns sound off.
void Gta2Native_AudioInit()
{
    const char* env = getenv("GTA2_AUDIO");
    if (env && atoi(env) == 0)
    {
        fprintf(stderr, "gta2: sound off (GTA2_AUDIO=0)\n");
        return;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        fprintf(stderr, "gta2: no sound: %s\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec want{};
    SDL_AudioSpec have{};
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = Callback;
    gDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!gDevice)
    {
        fprintf(stderr, "gta2: no sound: %s\n", SDL_GetError());
        return;
    }
    gOutRate = have.freq;
    fprintf(stderr, "gta2: SDL %s audio driver, %d Hz, %d channels, %d-frame buffer\n", SDL_GetCurrentAudioDriver(),
            have.freq, have.channels, have.samples);
    SDL_PauseAudioDevice(gDevice, 0);
}

// Once per loop iteration (main.cpp): keeps the playing streams decoded ahead.
void Gta2Native_AudioPump()
{
    if (!gDevice)
    {
        return;
    }
    for (auto& kv : gStreams)
    {
        PumpStream(*kv.second);
    }
}

void Gta2Native_AudioShutdown()
{
    if (gDevice)
    {
        SDL_CloseAudioDevice(gDevice);
        gDevice = 0;
    }
}

// --- the bridges (signatures: port/audio/miles_web.cpp's EM_JS declarations) --------
void gta2_js_sample_start(int id, const void* data, int len, int rate, int format, int loops, int loopStart, int loopEnd,
                          int volume, int pan)
{
    if (!gDevice)
    {
        return;
    }
    Lock lock;
    gVoices.erase(id);
    if (!data || len <= 0)
    {
        return;
    }
    Voice v;
    v.pcm = BankPcm(static_cast<const uint8_t*>(data), len, format);
    v.step = (rate > kBankRate * 0.05 ? rate : kBankRate * 0.05) / static_cast<double>(gOutRate);
    v.loops = loops;
    const int bytesPerFrame = ((format & 1) ? 2 : 1) * ((format & 2) ? 2 : 1);
    if (loops != 1 && loopEnd > 0 && loopEnd > loopStart)
    {
        v.loopStart = static_cast<double>(loopStart) / bytesPerFrame;
        v.loopEnd = static_cast<double>(loopEnd) / bytesPerFrame;
    }
    v.volume = Clamp(static_cast<float>(volume), 0, 127) / 127.0f;
    v.pan = Clamp((pan - 64) / 64.0f, -1, 1);
    gVoices[id] = v;
}

void gta2_js_sample_update(int id, int volume, int pan, int rate)
{
    if (!gDevice)
    {
        return;
    }
    Lock lock;
    auto it = gVoices.find(id);
    if (it == gVoices.end())
    {
        return;
    }
    it->second.volume = Clamp(static_cast<float>(volume), 0, 127) / 127.0f;
    it->second.pan = Clamp((pan - 64) / 64.0f, -1, 1);
    it->second.step = (rate > kBankRate * 0.05 ? rate : kBankRate * 0.05) / static_cast<double>(gOutRate);
}

void gta2_js_sample_stop(int id)
{
    if (!gDevice)
    {
        return;
    }
    Lock lock;
    gVoices.erase(id);
}

int gta2_js_stream_open(int id, const char* path)
{
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f)
    {
        return 0;
    }
    auto st = std::make_unique<Stream>();
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    void* map = size > 0 ? mmap(nullptr, static_cast<size_t>(size), PROT_READ, MAP_PRIVATE, fileno(f), 0) : MAP_FAILED;
    fclose(f); // the mapping stays valid
    if (map != MAP_FAILED)
    {
        st->bytes = static_cast<const uint8_t*>(map);
        st->size = static_cast<size_t>(size);
    }
    if (!st->bytes || !ParseWav(st->bytes, st->size, &st->wav))
    {
        fprintf(stderr, "gta2: audio: unsupported or unreadable WAV %s\n", path);
        return 0;
    }
    st->durationMs = static_cast<double>(st->wav.frames) / st->wav.rate * 1000.0;
    Prefetch(*st); // the first megabyte, read in the background
    Lock lock;
    gStreams[id] = std::move(st);
    return 1;
}

void gta2_js_stream_close(int id)
{
    Lock lock;
    gStreams.erase(id);
}

void gta2_js_stream_start(int id, int loops)
{
    Lock lock;
    auto it = gStreams.find(id);
    if (it == gStreams.end())
    {
        return;
    }
    Stream& st = *it->second;
    st.loops = loops;
    st.started = true;
    st.startWall = NowMs();
    st.resync = true;
    st.fifo.clear();
    st.readPos = 0;
}

int gta2_js_stream_playing(int id)
{
    Lock lock;
    auto it = gStreams.find(id);
    if (it == gStreams.end() || !it->second->started)
    {
        return 0;
    }
    const Stream& st = *it->second;
    if (st.loops == 0)
    {
        return 1;
    }
    return StreamElapsedMs(st) < st.durationMs * st.loops ? 1 : 0;
}

void gta2_js_stream_set(int id, int volume, int rate)
{
    Lock lock;
    auto it = gStreams.find(id);
    if (it == gStreams.end())
    {
        return;
    }
    Stream& st = *it->second;
    st.volume = volume;
    const double factor = rate / static_cast<double>(st.wav.rate) > 0.05 ? rate / static_cast<double>(st.wav.rate) : 0.05;
    if (factor != st.rateFactor)
    {
        if (st.started)
        {
            st.baseMs = StreamElapsedMs(st);
            st.startWall = NowMs();
        }
        st.rateFactor = factor;
    }
}

void gta2_js_stream_seek(int id, int ms)
{
    Lock lock;
    auto it = gStreams.find(id);
    if (it == gStreams.end())
    {
        return;
    }
    Stream& st = *it->second;
    st.baseMs = ms;
    st.startWall = NowMs();
    if (st.started)
    {
        st.resync = true;
        st.fifo.clear();
        st.readPos = 0;
    }
}

int gta2_js_stream_ms(int id, int which)
{
    Lock lock;
    auto it = gStreams.find(id);
    if (it == gStreams.end())
    {
        return 0;
    }
    const Stream& st = *it->second;
    if (which == 0)
    {
        return static_cast<int>(lround(st.durationMs));
    }
    if (!st.started)
    {
        return static_cast<int>(lround(st.baseMs));
    }
    return static_cast<int>(lround(fmod(StreamElapsedMs(st), st.durationMs > 1 ? st.durationMs : 1)));
}

void gta2_js_audio_reset()
{
    Lock lock;
    gVoices.clear();
    gStreams.clear();
    gBankCache.clear();
}

void gta2_js_master_volume(int volume)
{
    Lock lock;
    gMaster = Clamp(static_cast<float>(volume), 0, 127) / 127.0f;
}

} // extern "C"
#endif
