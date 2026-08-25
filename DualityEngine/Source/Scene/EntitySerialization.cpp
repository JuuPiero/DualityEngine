#include "EntitySerialization.h"

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    void SerializeEntityComponents(Entity entity, json& outEntityJson) {
        for (auto& type : TypeRegistry::All()) {
            if (!type.Has(entity))
                continue;
            void* component = type.GetPtr(entity);
            json fieldsJson;
            for (auto& field : type.Fields)
                fieldsJson[field.Name] = FieldValueToJson(field.Get(component));
            outEntityJson[type.DisplayName] = fieldsJson;
        }
    }

    void DeserializeEntityComponents(Entity entity, const json& entityJson) {
        for (auto& [typeName, fieldsJson] : entityJson.items()) {
            if (typeName == "Parent")
                continue; // bespoke hierarchy field, handled by the caller

            auto* type = TypeRegistry::Find(typeName);
            if (!type) {
                Log::Warn("EntitySerialization: unknown component type '" + typeName + "', skipping");
                continue;
            }

            type->AddDefault(entity);
            void* component = type->GetPtr(entity);
            for (auto& field : type->Fields) {
                if (fieldsJson.contains(field.Name)) {
                    FieldValue current = field.Get(component);
                    field.Set(component, JsonToFieldValue(fieldsJson[field.Name], current));
                }
            }
        }
    }

}
