#pragma once

#include <cstdint>

namespace Duality {

    // Fast deterministic PRNG for gameplay (not cryptography). State lives in
    // the script module on desktop, so Seed() gives repeatable runs without
    // requiring an expensive platform RNG call on Nintendo 3DS.
    class Random {
    public:
        static void Seed(uint32_t seed) { State() = seed ? seed : DefaultSeed; }

        static uint32_t NextUInt() {
            uint32_t value = State();
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
            State() = value;
            return value;
        }

        static float Value() {
            // The upper 24 bits map exactly into a float mantissa, yielding a
            // stable [0, 1) value on both desktop and ARM.
            return static_cast<float>(NextUInt() >> 8) * (1.0f / 16777216.0f);
        }

        static int Range(int minimumInclusive, int maximumExclusive) {
            if (maximumExclusive <= minimumInclusive)
                return minimumInclusive;
            return minimumInclusive + static_cast<int>(NextUInt() %
                static_cast<uint32_t>(maximumExclusive - minimumInclusive));
        }

        static float Range(float minimumInclusive, float maximumInclusive) {
            return minimumInclusive + (maximumInclusive - minimumInclusive) * Value();
        }

    private:
        static uint32_t& State() {
            static uint32_t state = DefaultSeed;
            return state;
        }
        static constexpr uint32_t DefaultSeed = 0xA341316Cu;
    };

}
