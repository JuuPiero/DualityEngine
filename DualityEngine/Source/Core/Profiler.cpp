#include "DualityEngine/Core/Profiler.h"

#include <chrono>

namespace Duality {

    bool Profiler::s_Recording = false;

    namespace {
        using Clock = std::chrono::steady_clock;

        std::deque<Profiler::Frame> s_Frames;
        Profiler::Frame s_Current;
        Clock::time_point s_FrameStart = Clock::now();
        std::vector<int> s_Open; // indices of open samples in s_Current.Samples
        uint64_t s_FrameIndex = 0;
        bool s_InFrame = false;

        double MsSinceFrameStart() {
            return std::chrono::duration<double, std::milli>(Clock::now() - s_FrameStart).count();
        }
    }

    double Profiler::Frame::TotalMs(const char* name) const {
        double total = 0.0;
        const std::string wanted(name);
        for (const Sample& sample : Samples)
            if (wanted == sample.Name)
                total += sample.DurationMs;
        return total;
    }

    double Profiler::Frame::CounterValue(const std::string& name, double fallback) const {
        for (const Counter& counter : Counters)
            if (counter.Name == name)
                return counter.Value;
        return fallback;
    }

    void Profiler::SetRecording(bool recording) {
        s_Recording = recording;
        if (!recording) {
            s_Open.clear();
            s_InFrame = false;
        }
    }

    void Profiler::BeginFrame() {
        if (!s_Recording)
            return;
        s_Current = Frame{};
        s_Current.Index = ++s_FrameIndex;
        s_Open.clear();
        s_FrameStart = Clock::now();
        s_InFrame = true;
    }

    void Profiler::EndFrame() {
        if (!s_Recording || !s_InFrame)
            return;
        while (!s_Open.empty())
            EndSample();
        s_Current.DurationMs = MsSinceFrameStart();
        s_Frames.push_back(std::move(s_Current));
        while (s_Frames.size() > MaxFrames)
            s_Frames.pop_front();
        s_Current = Frame{};
        s_InFrame = false;
    }

    void Profiler::BeginSample(const char* name) {
        if (!s_Recording || !s_InFrame)
            return;
        Sample sample;
        sample.Name = name;
        sample.Depth = static_cast<int>(s_Open.size());
        sample.Parent = s_Open.empty() ? -1 : s_Open.back();
        sample.StartMs = MsSinceFrameStart();
        s_Current.Samples.push_back(sample);
        s_Open.push_back(static_cast<int>(s_Current.Samples.size()) - 1);
    }

    void Profiler::EndSample() {
        if (!s_Recording || !s_InFrame || s_Open.empty())
            return;
        Sample& sample = s_Current.Samples[static_cast<size_t>(s_Open.back())];
        sample.DurationMs = MsSinceFrameStart() - sample.StartMs;
        s_Open.pop_back();
    }

    void Profiler::SetCounter(const std::string& name, double value) {
        if (!s_Recording || !s_InFrame)
            return;
        for (Counter& counter : s_Current.Counters) {
            if (counter.Name == name) {
                counter.Value = value;
                return;
            }
        }
        s_Current.Counters.push_back({ name, value });
    }

    const std::deque<Profiler::Frame>& Profiler::Frames() {
        return s_Frames;
    }

    void Profiler::Clear() {
        s_Frames.clear();
    }

    const Profiler::Frame& Profiler::CurrentFrame() {
        return s_Current;
    }

}
