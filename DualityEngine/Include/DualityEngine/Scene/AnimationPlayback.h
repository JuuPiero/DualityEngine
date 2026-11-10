#pragma once

#include <string>
#include <vector>

#include "DualityEngine/Asset/AnimationClipLoader.h"
#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    class Scene;

    // Applies an AnimationClipData to a live scene: the imported node channels (by entity
    // name, as before) and the Animation window's property / object curves (by path relative
    // to `root`). Used by the Animation component at runtime (desktop + 3DS) and by the editor's
    // Animation window preview, so both always agree.
    namespace AnimationPlayback {

        // `root`'s descendant at "Child/Grandchild" ("" = root). Invalid when missing.
        Entity FindTarget(Scene& scene, Entity root, const std::string& path);
        // Path of `target` relative to `root` ("" for root itself, "" + false when not below it).
        bool RelativePath(Entity root, Entity target, std::string& outPath);

        // Reflection access to one animatable scalar: "<Field>" or "<Field>.x|y|z|w" of a
        // float / int / bool / vec2 / vec3 / vec4 / color field.
        bool ReadFloat(Entity target, const std::string& component, const std::string& property, float& outValue);
        bool WriteFloat(Entity target, const std::string& component, const std::string& property, float value);
        // AssetRef fields (sprite keyframes).
        bool ReadObject(Entity target, const std::string& component, const std::string& property, std::string& outValue);
        bool WriteObject(Entity target, const std::string& component, const std::string& property, const std::string& value);

        // Every animatable property of one component instance (vectors expanded per axis):
        // { property name, isObject }.
        struct PropertyInfo { std::string Property; bool IsObject = false; };
        std::vector<PropertyInfo> AnimatableProperties(Entity target, const std::string& component);

        // Samples `clip` at `time` onto `root` and its descendants. `includeChannels` also
        // applies imported node channels (they bind by entity name scene-wide).
        void Sample(Scene& scene, Entity root, const AnimationClipData& clip, float time, bool includeChannels = true);
    }

}
