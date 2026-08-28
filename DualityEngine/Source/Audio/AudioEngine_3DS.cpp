#include "DualityEngine/Audio/AudioEngine.h"

#include <algorithm>
#include <cstring>

#include <3ds.h>

#include "DualityEngine/Audio/WavLoader.h"
#include "DualityEngine/Core/Log.h"

namespace Duality {

    namespace {
        constexpr int ChannelCount = 8;

        struct ChannelSlot {
            ndspWaveBuf WaveBuf{};
            void* Buffer = nullptr;
            bool InUse = false;
            bool Paused = false;
            bool Loop = false;
            float Volume = 1.0f;
            AudioHandle Handle = InvalidAudioHandle;
        };

        ChannelSlot s_Channels[ChannelCount];
        int s_NextChannel = 0;
        uint32_t s_NextHandle = 1;
        bool s_Initialized = false;

        void ReleaseChannel(int index) {
            ndspChnWaveBufClear(index);
            if (s_Channels[index].Buffer) {
                linearFree(s_Channels[index].Buffer);
                s_Channels[index].Buffer = nullptr;
            }
            s_Channels[index] = ChannelSlot{};
        }

        int FindChannelByHandle(AudioHandle handle) {
            for (int i = 0; i < ChannelCount; i++) {
                if (s_Channels[i].InUse && s_Channels[i].Handle == handle)
                    return i;
            }
            return -1;
        }

        void ApplyChannelMix(int channel, float volume) {
            float mix[12];
            std::memset(mix, 0, sizeof(mix));
            mix[0] = volume;
            mix[1] = volume;
            ndspChnSetMix(channel, mix);
        }
    }

    void AudioEngine::Init() {
        if (R_FAILED(ndspInit())) {
            Log::Error("AudioEngine: ndspInit failed");
            return;
        }
        ndspSetOutputMode(NDSP_OUTPUT_STEREO);
        for (int i = 0; i < ChannelCount; i++)
            ndspChnSetInterp(i, NDSP_INTERP_LINEAR);
        s_Initialized = true;
        s_NextHandle = 1;
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
        for (int i = 0; i < ChannelCount; i++) {
            if (s_Channels[i].InUse && !s_Channels[i].Loop && s_Channels[i].WaveBuf.status == NDSP_WBUF_DONE)
                ReleaseChannel(i);
        }
    }

    AudioHandle AudioEngine::Play(const std::string& path, bool loop, float volume) {
        if (!s_Initialized)
            return InvalidAudioHandle;

        WavData wav;
        if (!LoadWavFile(path, wav)) {
            Log::Error("AudioEngine: failed to load '" + path + "'");
            return InvalidAudioHandle;
        }
        if (wav.BitsPerSample != 16) {
            Log::Error("AudioEngine: '" + path + "' is not 16-bit PCM -- only 16-bit PCM WAV is supported on-device");
            return InvalidAudioHandle;
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
            return InvalidAudioHandle;
        }

        ReleaseChannel(channel);

        size_t dataSize = wav.Samples.size();
        void* buffer = linearAlloc(dataSize);
        if (!buffer) {
            Log::Error("AudioEngine: linearAlloc failed for '" + path + "'");
            return InvalidAudioHandle;
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
        slot.Paused = false;
        slot.Loop = loop;
        slot.Volume = volume;
        slot.Handle = s_NextHandle++;
        if (s_NextHandle == InvalidAudioHandle)
            s_NextHandle = 1;

        ApplyChannelMix(channel, volume);
        ndspChnWaveBufAdd(channel, &slot.WaveBuf);
        s_NextChannel = (channel + 1) % ChannelCount;
        return slot.Handle;
    }

    void AudioEngine::Stop(AudioHandle handle) {
        int channel = FindChannelByHandle(handle);
        if (channel >= 0)
            ReleaseChannel(channel);
    }

    void AudioEngine::StopAll() {
        for (int i = 0; i < ChannelCount; i++)
            ReleaseChannel(i);
    }

    void AudioEngine::SetVolume(AudioHandle handle, float volume) {
        int channel = FindChannelByHandle(handle);
        if (channel < 0)
            return;
        s_Channels[channel].Volume = volume;
        ApplyChannelMix(channel, volume);
    }

    void AudioEngine::SetPaused(AudioHandle handle, bool paused) {
        int channel = FindChannelByHandle(handle);
        if (channel < 0)
            return;
        if (s_Channels[channel].Paused == paused)
            return;
        s_Channels[channel].Paused = paused;
        ndspChnSetPaused(channel, paused);
    }

    bool AudioEngine::IsPlaying(AudioHandle handle) {
        int channel = FindChannelByHandle(handle);
        if (channel < 0 || s_Channels[channel].Paused)
            return false;
        if (s_Channels[channel].Loop)
            return true;
        return s_Channels[channel].WaveBuf.status != NDSP_WBUF_DONE;
    }

}
