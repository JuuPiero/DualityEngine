#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "DualityEngine/Reflection/Field.h"
#include "DualityEngine/Scene/Scene.h" // Entity's HasComponent<T>/GetComponent<T>/... templates are
                                       // *defined* here (after Scene is complete), not in Entity.h --
                                       // Register<T>() below needs the definitions visible to
                                       // instantiate them, not just the declarations.

namespace Duality {

    // Unity-style Add Component / Component menu folders, in menu order. A component's folder is
    // chosen where it is registered (Source/Scene/Components/<Domain>Components.cpp).
    namespace ComponentCategory {
        inline constexpr const char* Rendering = "Rendering";
        inline constexpr const char* Effects = "Effects";
        inline constexpr const char* Animation = "Animation";
        inline constexpr const char* Physics2D = "Physics 2D";
        inline constexpr const char* Physics = "Physics";
        inline constexpr const char* Audio = "Audio";
        inline constexpr const char* UI = "UI";
        inline constexpr const char* Camera = "Camera";
        inline constexpr const char* Miscellaneous = "Miscellaneous";

        inline const std::vector<const char*>& All() {
            static const std::vector<const char*> categories = {
                Rendering, Effects, Animation, Physics2D, Physics, Audio, UI, Camera, Miscellaneous
            };
            return categories;
        }
    }

    // Everything the Properties panel and SceneSerializer need to handle one
    // component type generically: how to check for it / add / remove it on
    // an entity, and its field list.
    struct ComponentTypeInfo {
        std::string DisplayName;
        bool Mandatory = false;
        std::function<bool(Entity)> Has;
        std::function<void*(Entity)> GetPtr;
        std::function<void(Entity)> AddDefault;
        std::function<void(Entity)> Remove;
        std::vector<FieldHandle> Fields;

        // Add Component / Component menu folder (ComponentCategory); empty = not offered there.
        std::string Category;
        // Edited in the Inspector header (Layer dropdown, prefab bar) instead of as a card, and
        // therefore not offered by the Inspector's Add Component button.
        bool HeaderComponent = false;
    };

    class TypeRegistry {
    public:
        static std::vector<ComponentTypeInfo>& All();
        static ComponentTypeInfo* Find(const std::string& displayName);

        // Registers T under `displayName` (its serialized name). Returns the entry so the caller
        // can set the remaining metadata; see Source/Scene/Components/ComponentRegistration.h.
        template<typename T>
        static ComponentTypeInfo& Register(const std::string& displayName, bool mandatory, std::vector<FieldHandle> fields) {
            ComponentTypeInfo info;
            info.DisplayName = displayName;
            info.Mandatory = mandatory;
            info.Has = [](Entity e) { return e.HasComponent<T>(); };
            info.GetPtr = [](Entity e) -> void* { return &e.GetComponent<T>(); };
            info.AddDefault = [](Entity e) { if (!e.HasComponent<T>()) e.AddComponent<T>(); };
            info.Remove = [](Entity e) { e.RemoveComponent<T>(); };
            info.Fields = std::move(fields);
            All().push_back(std::move(info));
            return All().back();
        }
    };

}
