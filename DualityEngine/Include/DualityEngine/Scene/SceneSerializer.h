#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // JSON scene (de)serialization -- generic and reflection-driven (EntitySerialization.cpp,
    // shared with PrefabSerializer): adding a component or a script field never needs
    // serializer changes. BehaviourComponent's ClassName and DUALITY_PROPERTIES overrides
    // both round-trip this way; an EntityRef-typed override is the one exception (see its
    // own comment in Reflection/Field.h) -- it always reloads as "unset".
    class SceneSerializer {
    public:
        explicit SceneSerializer(Scene& scene) : m_Scene(scene) {}

        bool Serialize(const std::string& path);
        bool Deserialize(const std::string& path);

        // In-memory variants (no disk I/O), sharing the exact same entity-walking logic as
        // the file-based methods above (which are now thin wrappers around these) -- used
        // where a real file isn't wanted, e.g. the Editor's Play->Stop scene-state snapshot/
        // restore (GamePanel.cpp). Neither clears m_Scene first -- same contract as
        // Deserialize, the caller decides when a clear (Scene::Clear()) is appropriate.
        nlohmann::json SerializeToJson();
        bool DeserializeFromJson(const nlohmann::json& root);

    private:
        Scene& m_Scene;
    };

}
