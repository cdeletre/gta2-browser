// Decodes a whole WAV file with port/native/audio/wav_ima.cpp and writes the samples as
// planar little-endian float32 (channel 0, then channel 1...). Used by
// scripts/native/check_ima.sh to compare with web/audio.js's decoder.
#include <stdio.h>
#include <stdlib.h>

#include <vector>

#include "../audio/wav_ima.h"

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: wav_dump in.wav out.f32\n");
        return 2;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f)
    {
        return 1;
    }
    std::vector<uint8_t> bytes;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        bytes.insert(bytes.end(), buf, buf + n);
    }
    fclose(f);
    WavInfo wav;
    if (!ParseWav(bytes.data(), bytes.size(), &wav))
    {
        printf("unsupported\n");
        return 0;
    }
    std::vector<std::vector<float>> ch;
    if (wav.tag == 0x11)
    {
        DecodeImaBlocks(bytes.data(), wav, 0, wav.blocks, &ch);
    }
    else
    {
        DecodePcm16(bytes.data(), wav, 0, wav.frames, &ch);
    }
    FILE* o = fopen(argv[2], "wb");
    for (const auto& c : ch)
    {
        fwrite(c.data(), sizeof(float), c.size(), o);
    }
    fclose(o);
    printf("tag %d, %d ch, %d Hz, %zu frames\n", wav.tag, wav.channels, wav.rate, wav.frames);
    return 0;
}
