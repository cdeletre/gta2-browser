// See wav_ima.h. Line for line web/audio.js (parseWav, decodeImaBlocks, decodePcm16).
#include "wav_ima.h"

#include <string.h>

namespace
{
const int kImaIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
const int kImaStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};

uint32_t Be32(const uint8_t* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8) | p[3];
}
uint32_t Le32(const uint8_t* p)
{
    return p[0] | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t Le16(const uint8_t* p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
int16_t LeS16(const uint8_t* p)
{
    return static_cast<int16_t>(Le16(p));
}
} // namespace

bool ParseWav(const uint8_t* bytes, size_t size, WavInfo* out)
{
    if (size < 12 || Be32(bytes) != 0x52494646 || Be32(bytes + 8) != 0x57415645)
    {
        return false;
    }
    bool haveFmt = false;
    WavInfo fmt;
    size_t off = 12;
    while (off + 8 <= size)
    {
        const uint32_t id = Be32(bytes + off);
        const uint32_t chunk = Le32(bytes + off + 4);
        const size_t body = off + 8;
        if (id == 0x666d7420 && body + 16 <= size) // 'fmt '
        {
            fmt.tag = Le16(bytes + body);
            fmt.channels = Le16(bytes + body + 2);
            fmt.rate = static_cast<int>(Le32(bytes + body + 4));
            fmt.blockAlign = Le16(bytes + body + 12);
            fmt.bits = Le16(bytes + body + 14);
            fmt.samplesPerBlock = 0;
            if (fmt.tag == 0x11 && chunk >= 20 && body + 20 <= size)
            {
                fmt.samplesPerBlock = Le16(bytes + body + 18);
            }
            haveFmt = true;
        }
        else if (id == 0x64617461 && haveFmt) // 'data'
        {
            const size_t dataLen = chunk < size - body ? chunk : size - body;
            if (fmt.channels <= 0)
            {
                return false;
            }
            if (fmt.tag == 1 && fmt.bits == 16)
            {
                *out = fmt;
                out->data = body;
                out->dataLen = dataLen;
                out->frames = dataLen / (2 * static_cast<size_t>(fmt.channels));
                return true;
            }
            if (fmt.tag == 0x11 && fmt.bits == 4 && fmt.blockAlign > 4 * fmt.channels)
            {
                *out = fmt;
                if (!out->samplesPerBlock)
                {
                    out->samplesPerBlock = ((fmt.blockAlign - 4 * fmt.channels) * 2) / fmt.channels + 1;
                }
                out->data = body;
                out->dataLen = dataLen;
                out->blocks = dataLen / static_cast<size_t>(fmt.blockAlign);
                out->frames = out->blocks * static_cast<size_t>(out->samplesPerBlock);
                return true;
            }
            return false;
        }
        off = body + chunk + (chunk & 1);
    }
    return false;
}

void DecodeImaBlocks(const uint8_t* bytes, const WavInfo& wav, size_t first, size_t count,
                     std::vector<std::vector<float>>* out)
{
    const int channels = wav.channels;
    const size_t spb = static_cast<size_t>(wav.samplesPerBlock);
    out->assign(static_cast<size_t>(channels), std::vector<float>(count * spb, 0.0f));
    for (size_t b = 0; b < count; ++b)
    {
        const size_t base = wav.data + (first + b) * static_cast<size_t>(wav.blockAlign);
        int pred[8];
        int index[8];
        size_t written[8];
        for (int c = 0; c < channels && c < 8; ++c)
        {
            pred[c] = LeS16(bytes + base + 4 * c);
            index[c] = bytes[base + 4 * c + 2] < 88 ? bytes[base + 4 * c + 2] : 88;
            (*out)[c][b * spb] = pred[c] / 32768.0f;
            written[c] = 1;
        }
        // Data: groups of 4 bytes (8 samples) per channel, interleaved by channel.
        size_t p = base + 4 * static_cast<size_t>(channels);
        const size_t end = base + static_cast<size_t>(wav.blockAlign);
        while (p < end)
        {
            for (int c = 0; c < channels && p < end; ++c)
            {
                for (int i = 0; i < 4; ++i, ++p)
                {
                    const int byte = bytes[p];
                    const int nibbles[2] = {byte & 0x0f, byte >> 4};
                    for (int nibble : nibbles)
                    {
                        const int step = kImaStep[index[c]];
                        int diff = step >> 3;
                        if (nibble & 4)
                        {
                            diff += step;
                        }
                        if (nibble & 2)
                        {
                            diff += step >> 1;
                        }
                        if (nibble & 1)
                        {
                            diff += step >> 2;
                        }
                        pred[c] += (nibble & 8) ? -diff : diff;
                        pred[c] = pred[c] < -32768 ? -32768 : pred[c] > 32767 ? 32767 : pred[c];
                        index[c] += kImaIndex[nibble];
                        index[c] = index[c] < 0 ? 0 : index[c] > 88 ? 88 : index[c];
                        if (written[c] < spb)
                        {
                            (*out)[c][b * spb + written[c]++] = pred[c] / 32768.0f;
                        }
                    }
                }
            }
        }
    }
}

void DecodePcm16(const uint8_t* bytes, const WavInfo& wav, size_t firstFrame, size_t frames,
                 std::vector<std::vector<float>>* out)
{
    out->assign(static_cast<size_t>(wav.channels), std::vector<float>(frames, 0.0f));
    for (size_t f = 0; f < frames; ++f)
    {
        for (int c = 0; c < wav.channels; ++c)
        {
            (*out)[c][f] = LeS16(bytes + wav.data + ((firstFrame + f) * wav.channels + c) * 2) / 32768.0f;
        }
    }
}
