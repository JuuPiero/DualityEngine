#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Duality {

    // 3DS-only: hand-parses a PCM WAV file's RIFF/fmt/data chunks. There's
    // no vendored audio decoder for real hardware (unlike desktop, where
    // miniaudio decodes WAV/MP3/etc. itself) -- WAV is a simple enough
    // container to parse directly, and it's the only format this engine
    // supports on-device for now (see ROADMAP.md).
    struct WavData {
        std::vector<uint8_t> Samples; // raw PCM bytes, exactly as stored in the file
        uint32_t SampleRate = 0;
        uint16_t Channels = 0;
        uint16_t BitsPerSample = 0;
    };

    // Returns false (outData left untouched) if the file doesn't exist, or
    // isn't a well-formed PCM WAV (missing RIFF/WAVE header, or no
    // "fmt "/"data" chunk found).
    bool LoadWavFile(const std::string& path, WavData& outData);

}
