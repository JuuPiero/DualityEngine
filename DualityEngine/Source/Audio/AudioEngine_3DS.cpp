#include "DualityEngine/Audio/AudioEngine.h"

#include <cstring>

#include <3ds.h>

#include "DualityEngine/Audio/WavLoader.h"
#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        // A small fixed pool of ndsp channels -- enough for a few
        // simultaneous SFX plus a dedicated music channel, without the
        // complexity of a real priority/voice-stealing system. One-shot
        // playback only (the whole clip is decoded and queued up front,
        // not the streaming template's double-buffered refill) -- simpler,
        // sufficient for short SFX and reasonably-sized music clips; real
        // streaming for long tracks is a documented ROADMAP follow-up.
        constexpr int ChannelCount = 8;

        struct ChannelSlot {
            ndspWaveBuf WaveBuf{};
            void* Buffer = nullptr;
            bool InUse = false;
        };

        ChannelSlot s_Channels[ChannelCount];
        int s_NextChannel = 0;
        bool s_Initialized = false;

        void ReleaseChannel(int index) {
            ndspChnWaveBufClear(index);
            if (s_Channels[index].Buffer) {
                linearFree(s_Channels[index].Buffer);
                s_Channels[index].Buffer = nullptr;
            }
            s_Channels[index].InUse = false;
        }
    }

    void AudioEngine::Init() {
        if (R_FAILED(ndspInit())) {
            Log::Error("AudioEngine: ndspInit failed");
            return;
        }
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        for (int i = 0; i < ChannelCount; i++) {
            ndspChnSetInterp(i, NDSP_INTERP_LINEAR);
            float mix[12];
            std::memset(mix, 0, sizeof(mix));
            mix[0] = 1.0f;
            mix[1] = 1.0f;
            ndspChnSetMix(i, mix);
        }
        s_Initialized = true;
    }

    void AudioEngine::Shutdown() {
        if (!s_Initialized)
            return;
        for (int i = 0; i < ChannelCount; i++)
            ReleaseChannel(i);
        ndspExit();
        s_Initialized = false;
    }

    void AudioEngine::Update() {
        // Reclaim finished one-shot buffers' memory -- looping buffers
        // never reach NDSP_WBUF_DONE on their own (ndsp replays them
        // in-place per ndspWaveBuf::looping), so this only ever frees
        // sounds that have actually finished playing.
        for (int i = 0; i < ChannelCount; i++) {
            if (s_Channels[i].InUse && s_Channels[i].WaveBuf.status == NDSP_WBUF_DONE)
                ReleaseChannel(i);
        }
    }

    void AudioEngine::Play(const std::string& path, bool loop) {
        if (!s_Initialized)
            return;

        WavData wav;
        if (!LoadWavFile(path, wav)) {
            Log::Error("AudioEngine: failed to load '" + path + "'");
            return;
        }
        if (wav.BitsPerSample != 16) {
            Log::Error("AudioEngine: '" + path + "' is not 16-bit PCM -- only 16-bit PCM WAV is supported on-device");
            return;
        }

        int channel = -1;
        for (int i = 0; i < ChannelCount; i++) {
            int candidate = (s_NextChannel + i) % ChannelCount;
            if (!s_Channels[candidate].InUse || s_Channels[candidate].WaveBuf.status == NDSP_WBUF_DONE) {
                channel = candidate;
                break;
            }
        }
        if (channel < 0) {
            Log::Warn("AudioEngine: all " + std::to_string(ChannelCount) + " channels busy, dropping '" + path + "'");
            return;
        }

        ReleaseChannel(channel);

        size_t dataSize = wav.Samples.size();
        void* buffer = linearAlloc(dataSize);
        if (!buffer) {
            Log::Error("AudioEngine: linearAlloc failed for '" + path + "'");
            return;
        }
        std::memcpy(buffer, wav.Samples.data(), dataSize);
        DSP_FlushDataCache(buffer, dataSize);

        uint32_t bytesPerFrame = static_cast<uint32_t>(wav.Channels) * (wav.BitsPerSample / 8);
        uint32_t frameCount = bytesPerFrame > 0 ? static_cast<uint32_t>(dataSize / bytesPerFrame) : 0;

        ndspChnSetFormat(channel, wav.Channels >= 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
        ndspChnSetRate(channel, static_cast<float>(wav.SampleRate));

        ChannelSlot& slot = s_Channels[channel];
        slot.WaveBuf = ndspWaveBuf{};
        slot.WaveBuf.data_vaddr = buffer;
        slot.WaveBuf.nsamples = frameCount;
        slot.WaveBuf.looping = loop;
        slot.Buffer = buffer;
        slot.InUse = true;

        ndspChnWaveBufAdd(channel, &slot.WaveBuf);
        s_NextChannel = (channel + 1) % ChannelCount;
    }

    void AudioEngine::StopAll() {
        for (int i = 0; i < ChannelCount; i++)
            ReleaseChannel(i);
    }

}
