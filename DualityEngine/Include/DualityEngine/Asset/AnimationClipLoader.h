#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Duality {

    struct AnimationVec3Key { float Time = 0.0f; glm::vec3 Value{}; };
    struct AnimationQuatKey { float Time = 0.0f; glm::quat Value{}; };
    struct AnimationChannel {
        std::string NodeName;
        std::vector<AnimationVec3Key> PositionKeys;
        std::vector<AnimationQuatKey> RotationKeys;
        std::vector<AnimationVec3Key> ScaleKeys;
    };

    // ---- Property curves (Animation window) ----------------------------------------------
    // Unity-style generic curves: any reflected component field of the animated entity or one
    // of its descendants. Target = Path (child names joined by '/', "" = the entity with the
    // Animation component) + Component (TypeRegistry display name, e.g. "Sprite Renderer") +
    // Property (field name, plus ".x/.y/.z/.w" for one component of a vector / color).
    enum class AnimationInterpolation { Linear, Constant };
    struct AnimationFloatKey { float Time = 0.0f; float Value = 0.0f; };
    struct AnimationFloatCurve {
        std::string Path;
        std::string Component;
        std::string Property;
        AnimationInterpolation Interpolation = AnimationInterpolation::Linear;
        std::vector<AnimationFloatKey> Keys; // sorted by Time
    };
    // Object-reference curves (Unity's sprite keyframes): AssetRef fields, stepped.
    struct AnimationObjectKey { float Time = 0.0f; std::string Value; };
    struct AnimationObjectCurve {
        std::string Path;
        std::string Component;
        std::string Property;
        std::vector<AnimationObjectKey> Keys; // sorted by Time
    };

    struct AnimationClipData {
        float Duration = 0.0f;
        std::vector<AnimationChannel> Channels; // imported model clips (node-name bound)
        // Optional, added by the Animation window; absent in every pre-existing clip.
        float SampleRate = 60.0f;
        // Model importer clip list "Loop Time" (optional "Loop" key, false when absent). The
        // Animation component's own Loop flag still decides playback; the importer seeds it.
        bool Loop = false;
        std::vector<AnimationFloatCurve> Curves;
        std::vector<AnimationObjectCurve> ObjectCurves;

        // Last key time over every curve / channel.
        float LastKeyTime() const;
    };

    // Reads editor-cooked .anim JSON. It deliberately has no Assimp dependency so the same
    // animation component can run in the 3DS player from romfs.
    //
    // File format: { "Duration": s, "Channels": [ { "Node", "Position": [[t,[x,y,z]]...],
    // "Rotation": [[t,[x,y,z,w]]...], "Scale": [...] } ], "SampleRate": fps,
    // "Curves": [ { "Path", "Component", "Property", "Interpolation": "Constant"?, "Keys": [[t,v]...] } ],
    // "ObjectCurves": [ { "Path", "Component", "Property", "Keys": [[t,"guid#id"]...] } ] }.
    // SampleRate / Curves / ObjectCurves are optional (older clips load unchanged).
    class AnimationClipLoader {
    public:
        static const AnimationClipData& Load(const std::string& path);
        // Editor: replace the cached clip / write it / forget it.
        static void Store(const std::string& path, const AnimationClipData& clip);
        static bool Save(const std::string& path, const AnimationClipData& clip);
        static void Invalidate(const std::string& path = std::string());
        static bool ParseJson(const std::string& text, AnimationClipData& out);
        static std::string ToJson(const AnimationClipData& clip);

        static const AnimationChannel* FindChannel(const AnimationClipData& clip, const std::string& nodeName);
        static glm::vec3 Sample(const std::vector<AnimationVec3Key>& keys, float time, const glm::vec3& fallback);
        static glm::quat Sample(const std::vector<AnimationQuatKey>& keys, float time, const glm::quat& fallback);
        static float Sample(const AnimationFloatCurve& curve, float time, float fallback);
        // Value of the last key at or before `time` (the first key before the first one);
        // null when the curve is empty.
        static const std::string* Sample(const AnimationObjectCurve& curve, float time);

        // Key editing helpers (sorted insert / replace at the same time), shared by the
        // Animation window and tests. Keys closer than `epsilon` count as the same time.
        static void SetKey(AnimationFloatCurve& curve, float time, float value, float epsilon = 0.0005f);
        static void SetKey(AnimationObjectCurve& curve, float time, const std::string& value, float epsilon = 0.0005f);
        static AnimationFloatCurve* FindCurve(AnimationClipData& clip, const std::string& path, const std::string& component, const std::string& property);
        static AnimationObjectCurve* FindObjectCurve(AnimationClipData& clip, const std::string& path, const std::string& component, const std::string& property);
    };
}
