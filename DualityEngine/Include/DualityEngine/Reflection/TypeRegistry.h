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
    };

    class TypeRegistry {
    public:
        static std::vector<ComponentTypeInfo>& All();
        static ComponentTypeInfo* Find(const std::string& displayName);

        template<typename T>
        static void Register(const std::string& displayName, bool mandatory, std::vector<FieldHandle> fields) {
            ComponentTypeInfo info;
            info.DisplayName = displayName;
            info.Mandatory = mandatory;
            info.Has = [](Entity e) { return e.HasComponent<T>(); };
            info.GetPtr = [](Entity e) -> void* { return &e.GetComponent<T>(); };
            info.AddDefault = [](Entity e) { if (!e.HasComponent<T>()) e.AddComponent<T>(); };
            info.Remove = [](Entity e) { e.RemoveComponent<T>(); };
            info.Fields = std::move(fields);
            All().push_back(std::move(info));
        }
    };

}
