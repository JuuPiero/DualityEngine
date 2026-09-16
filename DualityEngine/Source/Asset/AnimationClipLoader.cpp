#include "DualityEngine/Asset/AnimationClipLoader.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace Duality {
    namespace {
        std::unordered_map<std::string, AnimationClipData> s_Cache;
        using json = nlohmann::json;

        glm::vec3 ReadVec3(const json& value) {
            return value.is_array() && value.size() == 3
                ? glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()) : glm::vec3{};
        }
        glm::quat ReadQuat(const json& value) {
            return value.is_array() && value.size() == 4
                ? glm::quat(value[3].get<float>(), value[0].get<float>(), value[1].get<float>(), value[2].get<float>())
                : glm::quat{};
        }
    }

    const AnimationClipData& AnimationClipLoader::Load(const std::string& path) {
        auto existing = s_Cache.find(path);
        if (existing != s_Cache.end()) return existing->second;
        AnimationClipData clip;
        std::ifstream input(path);
        try {
            json root; input >> root;
            clip.Duration = std::max(0.0f, root.value("Duration", 0.0f));
            for (const auto& source : root.value("Channels", json::array())) {
                AnimationChannel channel;
                channel.NodeName = source.value("Node", std::string());
                for (const auto& key : source.value("Position", json::array()))
                    if (key.is_array() && key.size() == 2) channel.PositionKeys.push_back({ key[0].get<float>(), ReadVec3(key[1]) });
                for (const auto& key : source.value("Rotation", json::array()))
                    if (key.is_array() && key.size() == 2) channel.RotationKeys.push_back({ key[0].get<float>(), ReadQuat(key[1]) });
                for (const auto& key : source.value("Scale", json::array()))
                    if (key.is_array() && key.size() == 2) channel.ScaleKeys.push_back({ key[0].get<float>(), ReadVec3(key[1]) });
                clip.Channels.push_back(std::move(channel));
            }
        } catch (const json::exception&) { clip = {}; }
        return s_Cache.emplace(path, std::move(clip)).first->second;
    }

    const AnimationChannel* AnimationClipLoader::FindChannel(const AnimationClipData& clip, const std::string& nodeName) {
        for (const AnimationChannel& channel : clip.Channels)
            if (channel.NodeName == nodeName) return &channel;
        return clip.Channels.size() == 1 ? &clip.Channels.front() : nullptr;
    }

    glm::vec3 AnimationClipLoader::Sample(const std::vector<AnimationVec3Key>& keys, float time, const glm::vec3& fallback) {
        if (keys.empty()) return fallback;
        if (time <= keys.front().Time) return keys.front().Value;
        for (size_t i = 1; i < keys.size(); ++i) if (time < keys[i].Time) {
            const float span = keys[i].Time - keys[i - 1].Time;
            return glm::mix(keys[i - 1].Value, keys[i].Value, span > 0.0f ? (time - keys[i - 1].Time) / span : 0.0f);
        }
        return keys.back().Value;
    }

    glm::quat AnimationClipLoader::Sample(const std::vector<AnimationQuatKey>& keys, float time, const glm::quat& fallback) {
        if (keys.empty()) return fallback;
        if (time <= keys.front().Time) return keys.front().Value;
        for (size_t i = 1; i < keys.size(); ++i) if (time < keys[i].Time) {
            const float span = keys[i].Time - keys[i - 1].Time;
            return glm::slerp(keys[i - 1].Value, keys[i].Value, span > 0.0f ? (time - keys[i - 1].Time) / span : 0.0f);
        }
        return keys.back().Value;
    }
}
