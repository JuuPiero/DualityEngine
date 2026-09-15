#pragma once

#include <string>

#include "DualityEngine/ECS/Entity.h"
#include "DualityEngine/Renderer/Screen.h"
#include "DualityEngine/Scene/SceneManager.h"
#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // Unity-style scene/entity utilities for scripts -- cross-screen lookup, prefab/UI
    // instantiation, and ScriptableObject loading. Uses ScriptContext's bound Scene.
    class ScriptScene {
    public:
        // Unity-style deferred scene requests. Calling these from a Behaviour uses the
        // EngineServices bridge, so they are safe across the desktop GameScripts DLL boundary.
        static void LoadScene(const std::string& assetsRelativePath, LoadSceneMode mode = LoadSceneMode::Single) {
            SceneManager::RequestLoadScene(assetsRelativePath, mode);
        }
        static void LoadSceneAdditive(const std::string& assetsRelativePath) {
            LoadScene(assetsRelativePath, LoadSceneMode::Additive);
        }
        static void UnloadScene(const std::string& assetsRelativePath) {
            SceneManager::RequestUnloadScene(assetsRelativePath);
        }

        static Entity FindEntityInScreen(Screen screen, const std::string& name) {
            const EngineServices* services = ScriptContext::Services();
            void* scene = ScriptContext::Scene();
            if (!services || !scene)
                return Entity{};

            unsigned int handle = 0;
            if (!services->FindEntityInScreen(scene, static_cast<int>(screen), name.c_str(), &handle))
                return Entity{};

            return Entity(static_cast<entt::entity>(handle), static_cast<Scene*>(scene));
        }

        static Entity FindEntityInTopScreen(const std::string& name) { return FindEntityInScreen(Screen::Top, name); }
        static Entity FindEntityInBottomScreen(const std::string& name) { return FindEntityInScreen(Screen::Bottom, name); }

        // Spawns a new copy of the Prefab asset referenced by `prefabAssetGuid` as a root
        // entity in ScriptContext's bound Scene. Returns an empty Entity if the guid doesn't
        // resolve to a loadable prefab.
        static Entity Instantiate(const std::string& prefabAssetGuid) {
            const EngineServices* services = ScriptContext::Services();
            void* scene = ScriptContext::Scene();
            if (!services || !scene)
                return Entity{};

            unsigned int handle = 0;
            if (!services->Instantiate(scene, prefabAssetGuid.c_str(), &handle))
                return Entity{};

            return Entity(static_cast<entt::entity>(handle), static_cast<Scene*>(scene));
        }

        // Unity's ScriptableObject data-asset lookup -- caller supplies concrete type T since
        // the engine side only hands back an opaque void* across the DLL boundary.
        template<typename T>
        static T* LoadScriptableObject(const std::string& assetGuid) {
            const EngineServices* services = ScriptContext::Services();
            if (!services || assetGuid.empty())
                return nullptr;
            return static_cast<T*>(services->LoadScriptableObject(assetGuid.c_str()));
        }
    };

}
