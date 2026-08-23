#include "DualityEngine/Audio/WavLoader.h"

#include <cstring>
#include <fstream>

namespace Duality {

    namespace {
        uint32_t ReadU32(const char* bytes) {
            return static_cast<uint32_t>(static_cast<uint8_t>(bytes[0])) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(bytes[1])) << 8) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(bytes[2])) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(bytes[3])) << 24);
        }

        uint16_t ReadU16(const char* bytes) {
            return static_cast<uint16_t>(static_cast<uint8_t>(bytes[0]) | (static_cast<uint8_t>(bytes[1]) << 8));
        }
    }

    bool LoadWavFile(const std::string& path, WavData& outData) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return false;

        char riffHeader[12];
        file.read(riffHeader, 12);
        if (!file || std::memcmp(riffHeader, "RIFF", 4) != 0 || std::memcmp(riffHeader + 8, "WAVE", 4) != 0)
            return false;

        bool haveFormat = false;
        WavData result;

        char chunkHeader[8];
        while (file.read(chunkHeader, 8)) {
            uint32_t chunkSize = ReadU32(chunkHeader + 4);

            if (std::memcmp(chunkHeader, "fmt ", 4) == 0) {
                if (chunkSize < 16)
                    return false;
                char fmt[16];
                file.read(fmt, 16);
                if (!file)
                    return false;
                result.Channels = ReadU16(fmt + 2);
                result.SampleRate = ReadU32(fmt + 4);
                result.BitsPerSample = ReadU16(fmt + 14);
                haveFormat = true;
                // Skip any extra format bytes beyond the standard 16.
                if (chunkSize > 16)
                    file.seekg(static_cast<std::streamoff>(chunkSize - 16), std::ios::cur);
            } else if (std::memcmp(chunkHeader, "data", 4) == 0) {
                result.Samples.resize(chunkSize);
                file.read(reinterpret_cast<char*>(result.Samples.data()), chunkSize);
                if (!file)
                    return false;
            } else {
                // Unknown chunk (e.g. "LIST" metadata) -- skip it.
                file.seekg(static_cast<std::streamoff>(chunkSize), std::ios::cur);
            }

            // Chunks are word-aligned -- skip one pad byte after an odd-sized chunk.
            if (chunkSize % 2 != 0)
                file.seekg(1, std::ios::cur);
        }

        if (!haveFormat || result.Samples.empty())
            return false;

        outData = std::move(result);
        return true;
    }

}
