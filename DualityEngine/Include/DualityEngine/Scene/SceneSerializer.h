#pragma once

#include <string>

#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    // JSON scene (de)serialization. Hand-written per component for now --
    // a reflection-driven generic version (so adding a component/script
    // field needs zero serializer changes) is a later phase. Scripts
    // (BehaviourComponent) are not serialized yet for the same reason.
    class SceneSerializer {
    public:
        explicit SceneSerializer(Scene& scene) : m_Scene(scene) {}

        bool Serialize(const std::string& path);
        bool Deserialize(const std::string& path);

    private:
        Scene& m_Scene;
    };

}
