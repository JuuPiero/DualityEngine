#include "DualityEngine/Asset/AnimationClipLoader.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
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

        template<typename Key>
        void SortKeys(std::vector<Key>& keys) {
            std::stable_sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.Time < b.Time; });
        }

        bool ParseRoot(const json& root, AnimationClipData& clip) {
            clip = {};
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
            // Animation-window curves -- all optional.
            if (root.contains("SampleRate") && root["SampleRate"].is_number())
                clip.SampleRate = std::max(1.0f, root["SampleRate"].get<float>());
            if (root.contains("Loop") && root["Loop"].is_boolean())
                clip.Loop = root["Loop"].get<bool>();
            for (const auto& source : root.value("Curves", json::array())) {
                if (!source.is_object())
                    continue;
                AnimationFloatCurve curve;
                curve.Path = source.value("Path", std::string());
                curve.Component = source.value("Component", std::string());
                curve.Property = source.value("Property", std::string());
                curve.Interpolation = source.value("Interpolation", std::string()) == "Constant" ? AnimationInterpolation::Constant : AnimationInterpolation::Linear;
                for (const auto& key : source.value("Keys", json::array()))
                    if (key.is_array() && key.size() == 2 && key[0].is_number() && key[1].is_number())
                        curve.Keys.push_back({ key[0].get<float>(), key[1].get<float>() });
                SortKeys(curve.Keys);
                clip.Curves.push_back(std::move(curve));
            }
            for (const auto& source : root.value("ObjectCurves", json::array())) {
                if (!source.is_object())
                    continue;
                AnimationObjectCurve curve;
                curve.Path = source.value("Path", std::string());
                curve.Component = source.value("Component", std::string());
                curve.Property = source.value("Property", std::string());
                for (const auto& key : source.value("Keys", json::array()))
                    if (key.is_array() && key.size() == 2 && key[0].is_number() && key[1].is_string())
                        curve.Keys.push_back({ key[0].get<float>(), key[1].get<std::string>() });
                SortKeys(curve.Keys);
                clip.ObjectCurves.push_back(std::move(curve));
            }
            return true;
        }
    }

    float AnimationClipData::LastKeyTime() const {
        float last = 0.0f;
        for (const AnimationChannel& channel : Channels) {
            if (!channel.PositionKeys.empty()) last = std::max(last, channel.PositionKeys.back().Time);
            if (!channel.RotationKeys.empty()) last = std::max(last, channel.RotationKeys.back().Time);
            if (!channel.ScaleKeys.empty()) last = std::max(last, channel.ScaleKeys.back().Time);
        }
        for (const AnimationFloatCurve& curve : Curves)
            if (!curve.Keys.empty()) last = std::max(last, curve.Keys.back().Time);
        for (const AnimationObjectCurve& curve : ObjectCurves)
            if (!curve.Keys.empty()) last = std::max(last, curve.Keys.back().Time);
        return last;
    }

    bool AnimationClipLoader::ParseJson(const std::string& text, AnimationClipData& out) {
        try {
            return ParseRoot(json::parse(text), out);
        } catch (const json::exception&) {
            out = {};
            return false;
        }
    }

    std::string AnimationClipLoader::ToJson(const AnimationClipData& clip) {
        json root = json::object();
        root["Duration"] = clip.Duration;
        json channels = json::array();
        for (const AnimationChannel& channel : clip.Channels) {
            json entry = json::object();
            entry["Node"] = channel.NodeName;
            json position = json::array(), rotation = json::array(), scale = json::array();
            for (const auto& key : channel.PositionKeys)
                position.push_back(json::array({ key.Time, json::array({ key.Value.x, key.Value.y, key.Value.z }) }));
            for (const auto& key : channel.RotationKeys)
                rotation.push_back(json::array({ key.Time, json::array({ key.Value.x, key.Value.y, key.Value.z, key.Value.w }) }));
            for (const auto& key : channel.ScaleKeys)
                scale.push_back(json::array({ key.Time, json::array({ key.Value.x, key.Value.y, key.Value.z }) }));
            entry["Position"] = std::move(position);
            entry["Rotation"] = std::move(rotation);
            entry["Scale"] = std::move(scale);
            channels.push_back(std::move(entry));
        }
        root["Channels"] = std::move(channels);
        root["SampleRate"] = clip.SampleRate;
        if (clip.Loop)
            root["Loop"] = true;
        if (!clip.Curves.empty()) {
            json curves = json::array();
            for (const AnimationFloatCurve& curve : clip.Curves) {
                json entry = json::object();
                entry["Path"] = curve.Path;
                entry["Component"] = curve.Component;
                entry["Property"] = curve.Property;
                if (curve.Interpolation == AnimationInterpolation::Constant)
                    entry["Interpolation"] = "Constant";
                json keys = json::array();
                for (const AnimationFloatKey& key : curve.Keys)
                    keys.push_back(json::array({ key.Time, key.Value }));
                entry["Keys"] = std::move(keys);
                curves.push_back(std::move(entry));
            }
            root["Curves"] = std::move(curves);
        }
        if (!clip.ObjectCurves.empty()) {
            json curves = json::array();
            for (const AnimationObjectCurve& curve : clip.ObjectCurves) {
                json entry = json::object();
                entry["Path"] = curve.Path;
                entry["Component"] = curve.Component;
                entry["Property"] = curve.Property;
                json keys = json::array();
                for (const AnimationObjectKey& key : curve.Keys)
                    keys.push_back(json::array({ key.Time, key.Value }));
                entry["Keys"] = std::move(keys);
                curves.push_back(std::move(entry));
            }
            root["ObjectCurves"] = std::move(curves);
        }
        return root.dump(2);
    }

    const AnimationClipData& AnimationClipLoader::Load(const std::string& path) {
        auto existing = s_Cache.find(path);
        if (existing != s_Cache.end()) return existing->second;
        AnimationClipData clip;
        std::ifstream input(path);
        if (input) {
            const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            ParseJson(text, clip);
        }
        return s_Cache.emplace(path, std::move(clip)).first->second;
    }

    void AnimationClipLoader::Store(const std::string& path, const AnimationClipData& clip) {
        s_Cache[path] = clip;
    }

    bool AnimationClipLoader::Save(const std::string& path, const AnimationClipData& clip) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output << ToJson(clip);
        s_Cache[path] = clip;
        return static_cast<bool>(output);
    }

    void AnimationClipLoader::Invalidate(const std::string& path) {
        if (path.empty())
            s_Cache.clear();
        else
            s_Cache.erase(path);
    }

    const AnimationChannel* AnimationClipLoader::FindChannel(const AnimationClipData& clip, const std::string& nodeName) {
        for (const AnimationChannel& channel : clip.Channels)
            if (channel.NodeName == nodeName) return &channel;
        return clip.Channels.size() == 1 ? &clip.Channels.front() : nullptr;
    }

    // Baked model clips carry one key per sample (30-60 per second per channel), and a rig
    // samples dozens of channels every frame on the 3DS: find the segment by binary search.
    // Same segment as a linear "first key with time < key.Time" scan.
    glm::vec3 AnimationClipLoader::Sample(const std::vector<AnimationVec3Key>& keys, float time, const glm::vec3& fallback) {
        if (keys.empty()) return fallback;
        if (time <= keys.front().Time) return keys.front().Value;
        const auto next = std::upper_bound(keys.begin() + 1, keys.end(), time,
            [](float t, const AnimationVec3Key& key) { return t < key.Time; });
        if (next == keys.end()) return keys.back().Value;
        const AnimationVec3Key& a = *(next - 1);
        const float span = next->Time - a.Time;
        return glm::mix(a.Value, next->Value, span > 0.0f ? (time - a.Time) / span : 0.0f);
    }

    glm::quat AnimationClipLoader::Sample(const std::vector<AnimationQuatKey>& keys, float time, const glm::quat& fallback) {
        if (keys.empty()) return fallback;
        if (time <= keys.front().Time) return keys.front().Value;
        const auto next = std::upper_bound(keys.begin() + 1, keys.end(), time,
            [](float t, const AnimationQuatKey& key) { return t < key.Time; });
        if (next == keys.end()) return keys.back().Value;
        const AnimationQuatKey& a = *(next - 1);
        const float span = next->Time - a.Time;
        return glm::slerp(a.Value, next->Value, span > 0.0f ? (time - a.Time) / span : 0.0f);
    }

    float AnimationClipLoader::Sample(const AnimationFloatCurve& curve, float time, float fallback) {
        const auto& keys = curve.Keys;
        if (keys.empty()) return fallback;
        if (time <= keys.front().Time) return keys.front().Value;
        for (size_t i = 1; i < keys.size(); ++i) if (time < keys[i].Time) {
            if (curve.Interpolation == AnimationInterpolation::Constant)
                return keys[i - 1].Value;
            const float span = keys[i].Time - keys[i - 1].Time;
            const float t = span > 0.0f ? (time - keys[i - 1].Time) / span : 0.0f;
            return keys[i - 1].Value + (keys[i].Value - keys[i - 1].Value) * t;
        }
        return keys.back().Value;
    }

    const std::string* AnimationClipLoader::Sample(const AnimationObjectCurve& curve, float time) {
        if (curve.Keys.empty())
            return nullptr;
        const std::string* value = &curve.Keys.front().Value;
        for (const AnimationObjectKey& key : curve.Keys) {
            if (key.Time > time)
                break;
            value = &key.Value;
        }
        return value;
    }

    void AnimationClipLoader::SetKey(AnimationFloatCurve& curve, float time, float value, float epsilon) {
        for (AnimationFloatKey& key : curve.Keys) {
            if (std::fabs(key.Time - time) <= epsilon) {
                key.Value = value;
                return;
            }
        }
        curve.Keys.push_back({ time, value });
        SortKeys(curve.Keys);
    }

    void AnimationClipLoader::SetKey(AnimationObjectCurve& curve, float time, const std::string& value, float epsilon) {
        for (AnimationObjectKey& key : curve.Keys) {
            if (std::fabs(key.Time - time) <= epsilon) {
                key.Value = value;
                return;
            }
        }
        curve.Keys.push_back({ time, value });
        SortKeys(curve.Keys);
    }

    AnimationFloatCurve* AnimationClipLoader::FindCurve(AnimationClipData& clip, const std::string& path, const std::string& component, const std::string& property) {
        for (AnimationFloatCurve& curve : clip.Curves)
            if (curve.Path == path && curve.Component == component && curve.Property == property)
                return &curve;
        return nullptr;
    }

    AnimationObjectCurve* AnimationClipLoader::FindObjectCurve(AnimationClipData& clip, const std::string& path, const std::string& component, const std::string& property) {
        for (AnimationObjectCurve& curve : clip.ObjectCurves)
            if (curve.Path == path && curve.Component == component && curve.Property == property)
                return &curve;
        return nullptr;
    }
}
