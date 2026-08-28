#pragma once

#include <entt.hpp>
#include <vector>

#include "DualityEngine/Scene/Components.h"

namespace Duality {

    class Scene;

    struct TilemapData {
        int Width = 0;
        int Height = 0;
        std::vector<int> Tiles;
    };

    class TilemapLoader {
    public:
        static TilemapData Load(const std::string& path);
    };

    void UpdateSceneRuntimeSystems(Scene& scene, float deltaTime);
    void ClearSceneRuntimeSystems(Scene& scene);

    std::vector<Particle>& GetParticlePool(entt::entity handle);

}
