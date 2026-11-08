// RIFF WAVE parsing and IMA ADPCM / PCM16 decoding for the native audio back end: a
// translation of parseWav, decodeImaBlocks and decodePcm16 in web/audio.js, kept
// sample-for-sample identical (scripts/native/check_ima.sh compares them).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include <vector>

struct WavInfo
{
    int tag = 0;       // 1 = PCM, 0x11 = IMA ADPCM
    int channels = 0;
    int rate = 0;
    int blockAlign = 0;
    int bits = 0;
    int samplesPerBlock = 0; // IMA only
    size_t data = 0;         // offset of the sample data in the file
    size_t dataLen = 0;
    size_t blocks = 0;       // IMA only
    size_t frames = 0;
};

// False if the format is not 16-bit PCM or 4-bit IMA ADPCM (as parseWav returning null).
bool ParseWav(const uint8_t* bytes, size_t size, WavInfo* out);

// Decodes IMA blocks [first, first + count) into out[channel] (count * samplesPerBlock
// frames each), as decodeImaBlocks: samples / 32768.
void DecodeImaBlocks(const uint8_t* bytes, const WavInfo& wav, size_t first, size_t count,
                     std::vector<std::vector<float>>* out);

// Decodes 16-bit PCM frames [firstFrame, firstFrame + frames), as decodePcm16.
void DecodePcm16(const uint8_t* bytes, const WavInfo& wav, size_t firstFrame, size_t frames,
                 std::vector<std::vector<float>>* out);
