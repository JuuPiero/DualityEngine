#include "DualityEngine/Reflection/TypeRegistry.h"

namespace Duality {

    std::vector<ComponentTypeInfo>& TypeRegistry::All() {
        static std::vector<ComponentTypeInfo> types;
        return types;
    }

    ComponentTypeInfo* TypeRegistry::Find(const std::string& displayName) {
        for (auto& type : All()) {
            if (type.DisplayName == displayName)
                return &type;
        }
        return nullptr;
    }

}
