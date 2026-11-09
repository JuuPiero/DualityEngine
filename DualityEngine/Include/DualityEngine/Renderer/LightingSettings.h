#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "DualityEngine/Reflection/Field.h"

namespace Duality {

    // Per-scene environment lighting (Unity: Window > Rendering > Lighting > Environment).
    // Stored in the scene file under "Lighting" (SceneSerializer), edited in the Editor's
    // Lighting window, and copied into every RenderView of that scene. A scene saved before
    // this existed loads with these defaults, which equal the old hard-coded ambient (0.18
    // grey), so lit materials in old scenes look exactly as before.
    //
    // Only a flat ambient color is offered: PICA200 fragment lighting has a single global
    // ambient register, so Unity's Gradient/Skybox ambient modes have no hardware equivalent.
    struct LightingSettings {
        glm::vec4 AmbientColor{ 0.18f, 0.18f, 0.18f, 1.0f };
        float AmbientIntensity = 1.0f;

        glm::vec3 EffectiveAmbient() const {
            return glm::clamp(glm::vec3(AmbientColor) * AmbientIntensity, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        static std::vector<FieldHandle> Fields() {
            return {
                MakeColorField("Ambient Color", &LightingSettings::AmbientColor),
                MakeField("Ambient Intensity", &LightingSettings::AmbientIntensity),
            };
        }
    };

}
