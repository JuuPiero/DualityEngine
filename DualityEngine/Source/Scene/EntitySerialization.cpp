#include "EntitySerialization.h"

#include <any>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Reflection/FieldSerialization.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    bool ScriptRegistry::ResolvePendingOverrides(ScriptInstance& script) {
        if (script.UnresolvedOverridesJson.empty() || !IsRegistered(script.ClassName))
            return false;
        json raw = json::parse(script.UnresolvedOverridesJson, nullptr, false);
        script.UnresolvedOverridesJson.clear();
        const std::vector<FieldHandle>& fields = GetFields(script.ClassName);
        if (!raw.is_object() || fields.empty())
            return false;
        Behaviour* scratch = nullptr;
        void (*destroyScratch)(Behaviour*) = nullptr;
        if (!TryCreate(script.ClassName, &scratch, &destroyScratch))
            return false;
        bool resolved = false;
        for (const FieldHandle& field : fields) {
            if (raw.contains(field.Name) && !script.PropertyOverrides.count(field.Name)) {
                script.PropertyOverrides[field.Name] = JsonToFieldValue(raw[field.Name], field.Get(scratch));
                resolved = true;
            }
        }
        destroyScratch(scratch);
        return resolved;
    }

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
                    scriptJson["Enabled"] = script.Enabled;
                    json overridesJson;
                    ScriptRegistry::ResolvePendingOverrides(script);
                    if (!script.UnresolvedOverridesJson.empty()) {
                        json raw = json::parse(script.UnresolvedOverridesJson, nullptr, false);
                        if (raw.is_object())
                            overridesJson = raw;
                    }
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
                    script.Enabled = scriptJson.value("Enabled", true);
                    if (scriptJson.contains("PropertyOverrides")) {
                        const std::vector<FieldHandle>& scriptFields = ScriptRegistry::GetFields(script.ClassName);
                        const json& rawOverrides = scriptJson["PropertyOverrides"];
                        if (!ScriptRegistry::IsRegistered(script.ClassName) && rawOverrides.is_object() && !rawOverrides.empty()) {
                            // Class not available yet (its C++ module or TS bundle failed to
                            // build / hasn't loaded): keep the values verbatim instead of
                            // silently dropping them on the next save.
                            script.UnresolvedOverridesJson = rawOverrides.dump();
                        }
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

            // Pre-RectTransform scene files only have Anchor/Offset/Size -- convert those to the
            // equivalent AnchorMin/AnchorMax/Pivot/AnchoredPosition/SizeDelta so old UI layouts
            // resolve to the exact same pixel rect instead of silently resetting to defaults.
            // Falls through into the generic loop below unconditionally afterward: it's harmless
            // for legacy JSON (won't find the new field names) and is still required to apply
            // Enabled/Screen/Sort Order, which are unchanged either way.
            if (typeName == "UI Rect" && valueJson.contains("Anchor") && !valueJson.contains("Anchor Min")) {
                auto* rect = static_cast<UIRectComponent*>(component);
                FieldValue anchorProto = UIAnchor::TopLeft;
                UIAnchor legacyAnchor = std::get<UIAnchor>(JsonToFieldValue(valueJson["Anchor"], anchorProto));
                glm::vec2 legacyOffset = rect->AnchoredPosition;
                glm::vec2 legacySize = rect->SizeDelta;
                if (valueJson.contains("Offset") && valueJson["Offset"].is_array() && valueJson["Offset"].size() >= 2)
                    legacyOffset = { valueJson["Offset"][0].get<float>(), valueJson["Offset"][1].get<float>() };
                if (valueJson.contains("Size") && valueJson["Size"].is_array() && valueJson["Size"].size() >= 2)
                    legacySize = { valueJson["Size"][0].get<float>(), valueJson["Size"][1].get<float>() };
                LegacyUIAnchorToRectTransform(legacyAnchor, legacyOffset, legacySize,
                    rect->AnchorMin, rect->AnchorMax, rect->Pivot, rect->AnchoredPosition, rect->SizeDelta);
            }

            for (auto& field : type->Fields) {
                if (valueJson.contains(field.Name)) {
                    FieldValue current = field.Get(component);
                    field.Set(component, JsonToFieldValue(valueJson[field.Name], current));
                }
            }
        }
    }

        // FieldSerialization encodes an EntityRef as {"$entity": rawHandle}.  Raw EnTT
        // handles are only valid for this Scene instance, so translate that marker to the
        // compact entity-array index used by this scene file before it reaches disk.
        void EncodeEntityReferenceIndices(json& value, const std::unordered_map<entt::entity, int>& indexOf) {
            if (value.is_object()) {
                if (value.size() == 1 && value.contains("$entity")) {
                    entt::entity handle = static_cast<entt::entity>(value["$entity"].get<uint32_t>());
                    auto it = indexOf.find(handle);
                    value = it == indexOf.end() ? json(-1) : json(it->second);
                    return;
                }
                for (auto& item : value.items())
                    EncodeEntityReferenceIndices(item.value(), indexOf);
            } else if (value.is_array()) {
                for (json& item : value)
                    EncodeEntityReferenceIndices(item, indexOf);
            }
        }

        void ResolveEntityReferenceValue(FieldValue& value, const std::vector<Entity>& orderedEntities) {
            std::visit([&](auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, EntityRef>) {
                    if (item.Handle == EntityRef::Invalid)
                        return;
                    item = item.Handle < orderedEntities.size()
                        ? EntityRef{ static_cast<uint32_t>(orderedEntities[item.Handle].Handle()) }
                        : EntityRef{};
                } else if constexpr (std::is_same_v<T, std::vector<EntityRef>>) {
                    for (EntityRef& reference : item) {
                        if (reference.Handle == EntityRef::Invalid)
                            continue;
                        reference = reference.Handle < orderedEntities.size()
                            ? EntityRef{ static_cast<uint32_t>(orderedEntities[reference.Handle].Handle()) }
                            : EntityRef{};
                    }
                } else if constexpr (std::is_same_v<T, NestedFieldValue>) {
                    auto& values = std::any_cast<std::vector<FieldValue>&>(item.Values);
                    for (FieldValue& nested : values)
                        ResolveEntityReferenceValue(nested, orderedEntities);
                }
            }, value);
        }

        void ResolveLoadedEntityReferences(Scene& scene, const std::vector<Entity>& orderedEntities) {
            ResolveLoadedEntityReferences(scene, orderedEntities, orderedEntities);
        }

        void ResolveLoadedEntityReferences(Scene&, const std::vector<Entity>& entitiesToFix, const std::vector<Entity>& orderedEntities) {
            for (const auto& type : TypeRegistry::All()) {
                for (Entity entity : entitiesToFix) {
                    if (!type.Has(entity))
                        continue;
                    void* component = type.GetPtr(entity);
                    for (const FieldHandle& field : type.Fields) {
                        FieldValue value = field.Get(component);
                        ResolveEntityReferenceValue(value, orderedEntities);
                        field.Set(component, value);
                    }
                }
            }

            for (Entity entity : entitiesToFix) {
                if (!entity.HasComponent<BehaviourComponent>())
                    continue;
                for (ScriptInstance& script : entity.GetComponent<BehaviourComponent>().Scripts)
                    for (auto& override : script.PropertyOverrides)
                        ResolveEntityReferenceValue(override.second, orderedEntities);
            }
        }

}
