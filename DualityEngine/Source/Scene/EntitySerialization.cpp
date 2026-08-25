#include "EntitySerialization.h"

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    void SerializeEntityComponents(Entity entity, json& outEntityJson) {
        for (auto& type : TypeRegistry::All()) {
            if (!type.Has(entity))
                continue;
            void* component = type.GetPtr(entity);
            json fieldsJson;
            for (auto& field : type.Fields)
                fieldsJson[field.Name] = FieldValueToJson(field.Get(component));

            // BehaviourComponent::PropertyOverrides isn't a plain FieldValue (it's a map),
            // so it can't ride the generic field loop above -- same bespoke-field reasoning
            // as HierarchyComponent's own Parent, just nested inside this type's own block
            // instead of a sibling top-level key.
            if (type.DisplayName == "Behaviour") {
                auto* behaviour = static_cast<BehaviourComponent*>(component);
                json overridesJson;
                for (auto& [name, value] : behaviour->PropertyOverrides)
                    overridesJson[name] = FieldValueToJson(value);
                fieldsJson["PropertyOverrides"] = overridesJson;
            }

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

            if (typeName == "Behaviour" && fieldsJson.contains("PropertyOverrides")) {
                auto* behaviour = static_cast<BehaviourComponent*>(component);
                const std::vector<FieldHandle>& scriptFields = ScriptRegistry::GetFields(behaviour->ClassName);
                if (!scriptFields.empty()) {
                    // Need a same-typed prototype FieldValue per field to decode against
                    // (JsonToFieldValue dispatches on the prototype's held alternative) --
                    // there's no live Instance yet at load time, so spin up a throwaway one
                    // just to read each field's default-constructed value, matching the
                    // Properties panel's own approach (see PropertiesPanel.cpp).
                    Behaviour* scratch = nullptr;
                    void (*destroyScratch)(Behaviour*) = nullptr;
                    if (ScriptRegistry::TryCreate(behaviour->ClassName, &scratch, &destroyScratch)) {
                        const json& overridesJson = fieldsJson["PropertyOverrides"];
                        for (auto& field : scriptFields) {
                            if (overridesJson.contains(field.Name))
                                behaviour->PropertyOverrides[field.Name] = JsonToFieldValue(overridesJson[field.Name], field.Get(scratch));
                        }
                        destroyScratch(scratch);
                    }
                }
            }
        }
    }

}
