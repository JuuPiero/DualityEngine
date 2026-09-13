#pragma once

#include <algorithm>
#include <cmath>

namespace Duality {

    // Small dependency-free math toolbox for game scripts. GLM remains the
    // vector/matrix API; Mathf intentionally covers the scalar operations that
    // otherwise get reimplemented in every Behaviour.
    class Mathf {
    public:
        static constexpr float PI = 3.14159265358979323846f;
        static constexpr float Deg2Rad = PI / 180.0f;
        static constexpr float Rad2Deg = 180.0f / PI;

        static float Clamp(float value, float minimum, float maximum) { return std::clamp(value, minimum, maximum); }
        static float Clamp01(float value) { return Clamp(value, 0.0f, 1.0f); }
        static float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }
        static float LerpUnclamped(float a, float b, float t) { return a + (b - a) * t; }
        static float MoveTowards(float current, float target, float maxDelta) {
            maxDelta = std::max(0.0f, maxDelta);
            if (std::abs(target - current) <= maxDelta)
                return target;
            return current + (target > current ? maxDelta : -maxDelta);
        }
        static float Repeat(float value, float length) {
            return length > 0.0f ? value - std::floor(value / length) * length : 0.0f;
        }
        static float PingPong(float value, float length) {
            float wrapped = Repeat(value, length * 2.0f);
            return length - std::abs(wrapped - length);
        }
        static bool Approximately(float a, float b) {
            return std::abs(a - b) <= 1e-5f * std::max(1.0f, std::max(std::abs(a), std::abs(b)));
        }
    };

}
