#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Duality {

    // Model textures -> 3DS-ready PNG files: decoded (stb_image), resized to a power of two no
    // larger than the import setting (PICA200 samples only power-of-two textures, max 1024),
    // and re-encoded as PNG so desktop, the Content Browser and tex3ds all read the same file.
    namespace TextureBaker {

        struct Image {
            int Width = 0;
            int Height = 0;
            std::vector<uint8_t> Rgba; // Width * Height * 4
        };

        bool DecodeMemory(const std::vector<uint8_t>& fileBytes, Image& out);
        bool DecodeFile(const std::string& path, Image& out);

        // Nearest power of two per axis (Unity's "Non-Power of 2: ToNearest"), each clamped to
        // `maxSize` (itself rounded down to a power of two). Box-filtered when shrinking.
        int NearestPowerOfTwo(int value, int maxSize);
        Image ResizeToPowerOfTwo(const Image& source, int maxSize);

        bool HasTransparency(const Image& image);
        // Minimal PNG encoder (8-bit RGBA, or RGB when opaque; fixed-Huffman deflate).
        std::vector<uint8_t> EncodePng(const Image& image);
    }

}
