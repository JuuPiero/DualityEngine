#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// Lightweight CPU profiler behind the editor's Window > Analysis > Profiler (Unity's CPU Usage
// module). Code marks phases with DUALITY_PROFILE_SCOPE("Physics2D.Step"); the owner of the
// frame loop brackets each frame with Profiler::BeginFrame/EndFrame.
//
// Cost: when recording is off (the default) a marker is one predictable branch on a global
// bool. On the 3DS player the markers compile out entirely (DE_PLATFORM_3DS), unless the build
// defines DUALITY_ENABLE_PROFILER -- the device has no profiler UI yet.
#if defined(DE_PLATFORM_3DS) && !defined(DUALITY_ENABLE_PROFILER)
#define DUALITY_PROFILER_COMPILED 0
#else
#define DUALITY_PROFILER_COMPILED 1
#endif

namespace Duality {

    class Profiler {
    public:
        struct Sample {
            const char* Name = "";  // string literal (markers never allocate)
            int Depth = 0;
            int Parent = -1;        // index into Frame::Samples, -1 = root
            double StartMs = 0.0;   // relative to the frame start
            double DurationMs = 0.0;
        };
        struct Counter {
            std::string Name;
            double Value = 0.0;
        };
        struct Frame {
            uint64_t Index = 0;
            double DurationMs = 0.0;
            std::vector<Sample> Samples;
            std::vector<Counter> Counters;
            // Total / self time of the samples named `name` in this frame (all occurrences).
            double TotalMs(const char* name) const;
            double CounterValue(const std::string& name, double fallback = 0.0) const;
        };

        static constexpr size_t MaxFrames = 300;

        static void SetRecording(bool recording);
        static bool IsRecording() { return s_Recording; }

        static void BeginFrame();
        static void EndFrame();

        static void BeginSample(const char* name);
        static void EndSample();
        // Per-frame value (draw calls, triangles, memory...). Last write in a frame wins.
        static void SetCounter(const std::string& name, double value);

        static const std::deque<Frame>& Frames();
        static void Clear();

        // Exposed for tests: the in-progress frame.
        static const Frame& CurrentFrame();

    private:
        static bool s_Recording;
    };

    struct ProfilerScope {
        explicit ProfilerScope(const char* name) : Active(Profiler::IsRecording()) {
            if (Active)
                Profiler::BeginSample(name);
        }
        ~ProfilerScope() {
            if (Active)
                Profiler::EndSample();
        }
        ProfilerScope(const ProfilerScope&) = delete;
        ProfilerScope& operator=(const ProfilerScope&) = delete;
        bool Active;
    };

}

#define DUALITY_PROFILE_CONCAT_INNER(a, b) a##b
#define DUALITY_PROFILE_CONCAT(a, b) DUALITY_PROFILE_CONCAT_INNER(a, b)
#if DUALITY_PROFILER_COMPILED
#define DUALITY_PROFILE_SCOPE(name) ::Duality::ProfilerScope DUALITY_PROFILE_CONCAT(dualityProfileScope_, __LINE__)(name)
#define DUALITY_PROFILE_COUNTER(name, value) do { if (::Duality::Profiler::IsRecording()) ::Duality::Profiler::SetCounter(name, static_cast<double>(value)); } while (0)
#else
#define DUALITY_PROFILE_SCOPE(name) ((void)0)
#define DUALITY_PROFILE_COUNTER(name, value) ((void)0)
#endif
