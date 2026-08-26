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

            // BehaviourComponent now holds a vector of script slots (each with its own
            // ClassName + a PropertyOverrides map, not a plain FieldValue) instead of any
            // TypeRegistry-reflected fields of its own (type.Fields is empty for it -- see
            // Reflection.cpp) -- so it's fully special-cased into a JSON array here rather than
            // riding the generic per-field object loop below.
            if (type.DisplayName == "Scripts") {
                auto* behaviour = static_cast<BehaviourComponent*>(component);
                json scriptsJson = json::array();
                for (auto& script : behaviour->Scripts) {
                    json scriptJson;
                    scriptJson["ClassName"] = script.ClassName;
                    json overridesJson;
                    for (auto& [name, value] : script.PropertyOverrides)
                        overridesJson[name] = FieldValueToJson(value);
                    scriptJson["PropertyOverrides"] = overridesJson;
                    scriptsJson.push_back(scriptJson);
                }
                outEntityJson[type.DisplayName] = scriptsJson;
                continue;
            }

            json fieldsJson;
            for (auto& field : type.Fields)
                fieldsJson[field.Name] = FieldValueToJson(field.Get(component));
            outEntityJson[type.DisplayName] = fieldsJson;
        }
    }

    void DeserializeEntityComponents(Entity entity, const json& entityJson) {
        for (auto& [typeName, valueJson] : entityJson.items()) {
            if (typeName == "Parent")
                continue; // bespoke hierarchy field, handled by the caller

            auto* type = TypeRegistry::Find(typeName);
            if (!type) {
                Log::Warn("EntitySerialization: unknown component type '" + typeName + "', skipping");
                continue;
            }

            type->AddDefault(entity);
            void* component = type->GetPtr(entity);

            if (typeName == "Scripts") {
                auto* behaviour = static_cast<BehaviourComponent*>(component);
                for (auto& scriptJson : valueJson) {
                    ScriptInstance script;
                    script.ClassName = scriptJson.value("ClassName", std::string());
                    if (scriptJson.contains("PropertyOverrides")) {
                        const std::vector<FieldHandle>& scriptFields = ScriptRegistry::GetFields(script.ClassName);
                        if (!scriptFields.empty()) {
                            // Need a same-typed prototype FieldValue per field to decode against
                            // (JsonToFieldValue dispatches on the prototype's held alternative) --
                            // there's no live Instance yet at load time, so spin up a throwaway
                            // one just to read each field's default-constructed value, matching
                            // the Properties panel's own approach (see PropertiesPanel.cpp).
                            Behaviour* scratch = nullptr;
                            void (*destroyScratch)(Behaviour*) = nullptr;
                            if (ScriptRegistry::TryCreate(script.ClassName, &scratch, &destroyScratch)) {
                                const json& overridesJson = scriptJson["PropertyOverrides"];
                                for (auto& field : scriptFields) {
                                    if (overridesJson.contains(field.Name))
                                        script.PropertyOverrides[field.Name] = JsonToFieldValue(overridesJson[field.Name], field.Get(scratch));
                                }
                                destroyScratch(scratch);
                            }
                        }
                    }
                    behaviour->Scripts.push_back(script);
                }
                continue;
            }

            for (auto& field : type->Fields) {
                if (valueJson.contains(field.Name)) {
                    FieldValue current = field.Get(component);
                    field.Set(component, JsonToFieldValue(valueJson[field.Name], current));
                }
            }
        }
    }

}
