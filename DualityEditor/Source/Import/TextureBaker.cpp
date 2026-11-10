#include "DualityEditor/Import/TextureBaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>

// Own static stb implementation, like BuildPipeline.cpp: no exported symbols that could clash
// with the desktop OpenGL texture loader's copy.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Duality {

    namespace TextureBaker {

        namespace {
            // ---- CRC32 / Adler32 ---------------------------------------------------------------
            uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
                static std::array<uint32_t, 256> table = [] {
                    std::array<uint32_t, 256> values{};
                    for (uint32_t n = 0; n < 256; ++n) {
                        uint32_t c = n;
                        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                        values[n] = c;
                    }
                    return values;
                }();
                crc = ~crc;
                for (size_t i = 0; i < size; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
                return ~crc;
            }

            uint32_t Adler32(const std::vector<uint8_t>& data) {
                uint32_t a = 1, b = 0;
                for (uint8_t byte : data) {
                    a = (a + byte) % 65521u;
                    b = (b + a) % 65521u;
                }
                return (b << 16) | a;
            }

            // ---- Deflate (one fixed-Huffman block, greedy LZ77 with hash chains) ---------------
            struct BitWriter {
                std::vector<uint8_t>& Out;
                uint32_t Buffer = 0;
                int Count = 0;
                void Bits(uint32_t value, int count) {
                    Buffer |= value << Count;
                    Count += count;
                    while (Count >= 8) {
                        Out.push_back(static_cast<uint8_t>(Buffer & 0xFF));
                        Buffer >>= 8;
                        Count -= 8;
                    }
                }
                // Huffman codes are stored most-significant bit first.
                void Code(uint32_t code, int length) {
                    uint32_t reversed = 0;
                    for (int i = 0; i < length; ++i) reversed |= ((code >> i) & 1u) << (length - 1 - i);
                    Bits(reversed, length);
                }
                void Flush() {
                    if (Count > 0) Out.push_back(static_cast<uint8_t>(Buffer & 0xFF));
                    Buffer = 0;
                    Count = 0;
                }
            };

            void LiteralLength(BitWriter& writer, int symbol) {
                if (symbol < 144) writer.Code(0x30 + symbol, 8);
                else if (symbol < 256) writer.Code(0x190 + (symbol - 144), 9);
                else if (symbol < 280) writer.Code(symbol - 256, 7);
                else writer.Code(0xC0 + (symbol - 280), 8);
            }

            constexpr int LengthBase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
            constexpr int LengthExtra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
            constexpr int DistanceBase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
            constexpr int DistanceExtra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

            void Match(BitWriter& writer, int length, int distance) {
                int lengthIndex = 28;
                if (length < 258)
                    for (int i = 0; i < 28; ++i)
                        if (length < LengthBase[i + 1]) { lengthIndex = i; break; }
                LiteralLength(writer, 257 + lengthIndex);
                if (LengthExtra[lengthIndex]) writer.Bits(static_cast<uint32_t>(length - LengthBase[lengthIndex]), LengthExtra[lengthIndex]);
                int distanceIndex = 29;
                for (int i = 0; i < 29; ++i)
                    if (distance < DistanceBase[i + 1]) { distanceIndex = i; break; }
                writer.Code(static_cast<uint32_t>(distanceIndex), 5);
                if (DistanceExtra[distanceIndex]) writer.Bits(static_cast<uint32_t>(distance - DistanceBase[distanceIndex]), DistanceExtra[distanceIndex]);
            }

            std::vector<uint8_t> ZlibCompress(const std::vector<uint8_t>& data) {
                std::vector<uint8_t> out{ 0x78, 0x01 };
                BitWriter writer{ out };
                writer.Bits(1, 1); // BFINAL
                writer.Bits(1, 2); // BTYPE = fixed Huffman
                constexpr int HashBits = 15, Window = 32768, MaxChain = 32, MaxLength = 258;
                std::vector<int> head(1 << HashBits, -1), previous(Window, -1);
                auto hashAt = [&](size_t i) {
                    const uint32_t value = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
                    return (value * 2654435761u) >> (32 - HashBits);
                };
                auto insert = [&](size_t i) {
                    if (i + 2 >= data.size()) return;
                    const uint32_t hash = hashAt(i);
                    previous[i % Window] = head[hash];
                    head[hash] = static_cast<int>(i);
                };
                size_t position = 0;
                while (position < data.size()) {
                    int bestLength = 0, bestDistance = 0;
                    if (position + 2 < data.size()) {
                        int candidate = head[hashAt(position)];
                        const int limit = static_cast<int>(std::min<size_t>(MaxLength, data.size() - position));
                        for (int chain = 0; candidate >= 0 && chain < MaxChain; ++chain) {
                            const int distance = static_cast<int>(position) - candidate;
                            if (distance <= 0 || distance > Window - 1) break;
                            int length = 0;
                            while (length < limit && data[static_cast<size_t>(candidate) + length] == data[position + length]) ++length;
                            if (length > bestLength) {
                                bestLength = length;
                                bestDistance = distance;
                                if (length == limit) break;
                            }
                            const int next = previous[static_cast<size_t>(candidate) % Window];
                            if (next >= candidate) break;
                            candidate = next;
                        }
                    }
                    if (bestLength >= 3) {
                        Match(writer, bestLength, bestDistance);
                        for (int i = 0; i < bestLength; ++i) insert(position + i);
                        position += static_cast<size_t>(bestLength);
                    } else {
                        LiteralLength(writer, data[position]);
                        insert(position);
                        ++position;
                    }
                }
                LiteralLength(writer, 256); // end of block
                writer.Flush();
                const uint32_t adler = Adler32(data);
                for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>((adler >> shift) & 0xFF));
                return out;
            }

            void PutU32(std::vector<uint8_t>& out, uint32_t value) {
                for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
            }

            void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
                PutU32(out, static_cast<uint32_t>(data.size()));
                const size_t start = out.size();
                out.insert(out.end(), type, type + 4);
                out.insert(out.end(), data.begin(), data.end());
                PutU32(out, Crc32(out.data() + start, out.size() - start));
            }

            uint8_t Paeth(int a, int b, int c) {
                const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                return static_cast<uint8_t>(pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
            }
        }

        bool DecodeMemory(const std::vector<uint8_t>& fileBytes, Image& out) {
            out = {};
            if (fileBytes.empty() || fileBytes.size() > 0x7fffffff)
                return false;
            int channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(fileBytes.data(), static_cast<int>(fileBytes.size()), &out.Width, &out.Height, &channels, 4);
            if (!pixels)
                return false;
            out.Rgba.assign(pixels, pixels + static_cast<size_t>(out.Width) * out.Height * 4);
            stbi_image_free(pixels);
            return out.Width > 0 && out.Height > 0;
        }

        bool DecodeFile(const std::string& path, Image& out) {
            out = {};
            int channels = 0;
            stbi_uc* pixels = stbi_load(path.c_str(), &out.Width, &out.Height, &channels, 4);
            if (!pixels)
                return false;
            out.Rgba.assign(pixels, pixels + static_cast<size_t>(out.Width) * out.Height * 4);
            stbi_image_free(pixels);
            return out.Width > 0 && out.Height > 0;
        }

        int NearestPowerOfTwo(int value, int maxSize) {
            int maxPot = 1;
            while (maxPot * 2 <= std::max(1, maxSize)) maxPot *= 2;
            int lower = 1;
            while (lower * 2 <= std::max(1, value)) lower *= 2;
            const int upper = lower * 2;
            // Ties round up (keeps detail): 3 -> 4, 384 -> 512.
            const int nearest = value == lower ? lower : ((value - lower) < (upper - value) ? lower : upper);
            return std::clamp(nearest, 1, maxPot);
        }

        Image ResizeToPowerOfTwo(const Image& source, int maxSize) {
            const int width = NearestPowerOfTwo(source.Width, maxSize);
            const int height = NearestPowerOfTwo(source.Height, maxSize);
            if (width == source.Width && height == source.Height)
                return source;
            Image result;
            result.Width = width;
            result.Height = height;
            result.Rgba.resize(static_cast<size_t>(width) * height * 4);
            const float scaleX = static_cast<float>(source.Width) / width;
            const float scaleY = static_cast<float>(source.Height) / height;
            auto texel = [&](int x, int y, int c) {
                x = std::clamp(x, 0, source.Width - 1);
                y = std::clamp(y, 0, source.Height - 1);
                return static_cast<float>(source.Rgba[(static_cast<size_t>(y) * source.Width + x) * 4 + c]);
            };
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    for (int c = 0; c < 4; ++c) {
                        float value = 0.0f;
                        if (scaleX > 1.0f || scaleY > 1.0f) {
                            // Shrinking: average the source footprint (box filter).
                            const int x0 = static_cast<int>(std::floor(x * scaleX)), x1 = std::max(x0 + 1, static_cast<int>(std::floor((x + 1) * scaleX)));
                            const int y0 = static_cast<int>(std::floor(y * scaleY)), y1 = std::max(y0 + 1, static_cast<int>(std::floor((y + 1) * scaleY)));
                            float sum = 0.0f;
                            for (int sy = y0; sy < y1; ++sy)
                                for (int sx = x0; sx < x1; ++sx) sum += texel(sx, sy, c);
                            value = sum / static_cast<float>((x1 - x0) * (y1 - y0));
                        } else {
                            // Enlarging: bilinear.
                            const float sx = (x + 0.5f) * scaleX - 0.5f, sy = (y + 0.5f) * scaleY - 0.5f;
                            const int ix = static_cast<int>(std::floor(sx)), iy = static_cast<int>(std::floor(sy));
                            const float fx = sx - ix, fy = sy - iy;
                            value = (texel(ix, iy, c) * (1 - fx) + texel(ix + 1, iy, c) * fx) * (1 - fy) +
                                (texel(ix, iy + 1, c) * (1 - fx) + texel(ix + 1, iy + 1, c) * fx) * fy;
                        }
                        result.Rgba[(static_cast<size_t>(y) * width + x) * 4 + c] = static_cast<uint8_t>(std::clamp(std::lround(value), 0L, 255L));
                    }
                }
            }
            return result;
        }

        bool HasTransparency(const Image& image) {
            for (size_t i = 3; i < image.Rgba.size(); i += 4)
                if (image.Rgba[i] != 255) return true;
            return false;
        }

        std::vector<uint8_t> EncodePng(const Image& image) {
            std::vector<uint8_t> png = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
            if (image.Width <= 0 || image.Height <= 0 || image.Rgba.size() != static_cast<size_t>(image.Width) * image.Height * 4)
                return {};
            const bool alpha = HasTransparency(image);
            const int channels = alpha ? 4 : 3;
            std::vector<uint8_t> header;
            PutU32(header, static_cast<uint32_t>(image.Width));
            PutU32(header, static_cast<uint32_t>(image.Height));
            header.push_back(8);
            header.push_back(alpha ? 6 : 2);
            header.push_back(0); header.push_back(0); header.push_back(0);
            Chunk(png, "IHDR", header);

            // Per-row filter chosen by the minimum sum of absolute differences (libpng's heuristic).
            const size_t stride = static_cast<size_t>(image.Width) * channels;
            std::vector<uint8_t> raw, current(stride), previous(stride, 0), candidate(stride), best(stride);
            raw.reserve((stride + 1) * image.Height);
            for (int y = 0; y < image.Height; ++y) {
                for (int x = 0; x < image.Width; ++x)
                    for (int c = 0; c < channels; ++c)
                        current[static_cast<size_t>(x) * channels + c] = image.Rgba[(static_cast<size_t>(y) * image.Width + x) * 4 + c];
                uint64_t bestScore = ~0ull;
                uint8_t bestFilter = 0;
                for (uint8_t filter = 0; filter <= 4; ++filter) {
                    uint64_t score = 0;
                    for (size_t i = 0; i < stride; ++i) {
                        const int left = i >= static_cast<size_t>(channels) ? current[i - channels] : 0;
                        const int up = previous[i];
                        const int upLeft = i >= static_cast<size_t>(channels) ? previous[i - channels] : 0;
                        int predictor = 0;
                        if (filter == 1) predictor = left;
                        else if (filter == 2) predictor = up;
                        else if (filter == 3) predictor = (left + up) / 2;
                        else if (filter == 4) predictor = Paeth(left, up, upLeft);
                        candidate[i] = static_cast<uint8_t>(current[i] - predictor);
                        score += static_cast<uint64_t>(std::abs(static_cast<int8_t>(candidate[i])));
                    }
                    if (score < bestScore) {
                        bestScore = score;
                        bestFilter = filter;
                        best = candidate;
                    }
                }
                raw.push_back(bestFilter);
                raw.insert(raw.end(), best.begin(), best.end());
                previous = current;
            }
            Chunk(png, "IDAT", ZlibCompress(raw));
            Chunk(png, "IEND", {});
            return png;
        }
    }

}
