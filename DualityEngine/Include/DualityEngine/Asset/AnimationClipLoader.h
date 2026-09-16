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
    struct AnimationClipData {
        float Duration = 0.0f;
        std::vector<AnimationChannel> Channels;
    };

    // Reads editor-cooked .anim JSON. It deliberately has no Assimp dependency so the same
    // animation component can run in the 3DS player from romfs.
    class AnimationClipLoader {
    public:
        static const AnimationClipData& Load(const std::string& path);
        static const AnimationChannel* FindChannel(const AnimationClipData& clip, const std::string& nodeName);
        static glm::vec3 Sample(const std::vector<AnimationVec3Key>& keys, float time, const glm::vec3& fallback);
        static glm::quat Sample(const std::vector<AnimationQuatKey>& keys, float time, const glm::quat& fallback);
    };
}
