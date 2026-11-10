#include "DualityEngine/Scene/AnimationPlayback.h"

#include <cmath>
#include <unordered_map>

#include <glm/gtc/quaternion.hpp>

#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"

namespace Duality {

    namespace AnimationPlayback {

        namespace {
            // "Field.y" -> ("Field", 1); "Field" -> ("Field", -1).
            void SplitProperty(const std::string& property, std::string& field, int& axis) {
                field = property;
                axis = -1;
                const size_t dot = property.rfind('.');
                if (dot == std::string::npos || dot + 2 != property.size())
                    return;
                const char c = property[dot + 1];
                const int index = c == 'x' || c == 'r' ? 0 : c == 'y' || c == 'g' ? 1 : c == 'z' || c == 'b' ? 2 : c == 'w' || c == 'a' ? 3 : -1;
                if (index < 0)
                    return;
                field = property.substr(0, dot);
                axis = index;
            }

            const FieldHandle* FindField(ComponentTypeInfo& type, const std::string& name) {
                for (const FieldHandle& field : type.Fields)
                    if (field.Name == name)
                        return &field;
                return nullptr;
            }

            bool Resolve(Entity target, const std::string& component, const std::string& property,
                ComponentTypeInfo*& outType, const FieldHandle*& outField, int& outAxis) {
                if (!target)
                    return false;
                outType = TypeRegistry::Find(component);
                if (!outType || !outType->Has(target))
                    return false;
                std::string fieldName;
                SplitProperty(property, fieldName, outAxis);
                outField = FindField(*outType, fieldName);
                if (!outField) {
                    // A field literally named "X.y" (never in practice) -- try the whole string.
                    outField = FindField(*outType, property);
                    outAxis = -1;
                }
                return outField != nullptr;
            }
        }

        Entity FindTarget(Scene& scene, Entity root, const std::string& path) {
            (void)scene;
            if (!root)
                return {};
            Entity current = root;
            size_t start = 0;
            while (start < path.size()) {
                size_t end = path.find('/', start);
                if (end == std::string::npos)
                    end = path.size();
                const std::string name = path.substr(start, end - start);
                Entity next;
                for (Entity child : current.GetComponent<HierarchyComponent>().Children) {
                    if (child && child.GetComponent<NameComponent>().Name == name) {
                        next = child;
                        break;
                    }
                }
                if (!next)
                    return {};
                current = next;
                start = end + 1;
            }
            return current;
        }

        bool RelativePath(Entity root, Entity target, std::string& outPath) {
            outPath.clear();
            for (Entity current = target; current; current = current.GetComponent<HierarchyComponent>().Parent) {
                if (current == root)
                    return true;
                const std::string& name = current.GetComponent<NameComponent>().Name;
                outPath = outPath.empty() ? name : name + "/" + outPath;
            }
            outPath.clear();
            return false;
        }

        bool ReadFloat(Entity target, const std::string& component, const std::string& property, float& outValue) {
            ComponentTypeInfo* type = nullptr;
            const FieldHandle* field = nullptr;
            int axis = -1;
            if (!Resolve(target, component, property, type, field, axis))
                return false;
            const FieldValue value = field->Get(type->GetPtr(target));
            bool ok = true;
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>) outValue = v;
                else if constexpr (std::is_same_v<T, int>) outValue = static_cast<float>(v);
                else if constexpr (std::is_same_v<T, bool>) outValue = v ? 1.0f : 0.0f;
                else if constexpr (std::is_same_v<T, glm::vec2>) { if (axis >= 0 && axis < 2) outValue = v[axis]; else ok = false; }
                else if constexpr (std::is_same_v<T, glm::vec3>) { if (axis >= 0 && axis < 3) outValue = v[axis]; else ok = false; }
                else if constexpr (std::is_same_v<T, glm::vec4>) { if (axis >= 0 && axis < 4) outValue = v[axis]; else ok = false; }
                else if constexpr (std::is_same_v<T, Color4>) { if (axis >= 0 && axis < 4) outValue = v.Value[axis]; else ok = false; }
                else ok = false;
            }, value);
            return ok;
        }

        bool WriteFloat(Entity target, const std::string& component, const std::string& property, float value) {
            ComponentTypeInfo* type = nullptr;
            const FieldHandle* field = nullptr;
            int axis = -1;
            if (!Resolve(target, component, property, type, field, axis))
                return false;
            void* instance = type->GetPtr(target);
            FieldValue current = field->Get(instance);
            bool ok = true;
            std::visit([&](auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, float>) v = value;
                else if constexpr (std::is_same_v<T, int>) v = static_cast<int>(std::lround(value));
                else if constexpr (std::is_same_v<T, bool>) v = value >= 0.5f;
                else if constexpr (std::is_same_v<T, glm::vec2>) { if (axis >= 0 && axis < 2) v[axis] = value; else ok = false; }
                else if constexpr (std::is_same_v<T, glm::vec3>) { if (axis >= 0 && axis < 3) v[axis] = value; else ok = false; }
                else if constexpr (std::is_same_v<T, glm::vec4>) { if (axis >= 0 && axis < 4) v[axis] = value; else ok = false; }
                else if constexpr (std::is_same_v<T, Color4>) { if (axis >= 0 && axis < 4) v.Value[axis] = value; else ok = false; }
                else ok = false;
            }, current);
            if (ok)
                field->Set(instance, current);
            return ok;
        }

        bool ReadObject(Entity target, const std::string& component, const std::string& property, std::string& outValue) {
            ComponentTypeInfo* type = nullptr;
            const FieldHandle* field = nullptr;
            int axis = -1;
            if (!Resolve(target, component, property, type, field, axis))
                return false;
            const FieldValue value = field->Get(type->GetPtr(target));
            if (const AssetRef* ref = std::get_if<AssetRef>(&value)) {
                outValue = ref->Guid;
                return true;
            }
            return false;
        }

        bool WriteObject(Entity target, const std::string& component, const std::string& property, const std::string& value) {
            ComponentTypeInfo* type = nullptr;
            const FieldHandle* field = nullptr;
            int axis = -1;
            if (!Resolve(target, component, property, type, field, axis))
                return false;
            void* instance = type->GetPtr(target);
            const FieldValue current = field->Get(instance);
            if (!std::holds_alternative<AssetRef>(current))
                return false;
            field->Set(instance, FieldValue(AssetRef{ value }));
            return true;
        }

        std::vector<PropertyInfo> AnimatableProperties(Entity target, const std::string& component) {
            std::vector<PropertyInfo> result;
            ComponentTypeInfo* type = TypeRegistry::Find(component);
            if (!target || !type || !type->Has(target))
                return result;
            void* instance = type->GetPtr(target);
            static const char* axes = "xyzw";
            for (const FieldHandle& field : type->Fields) {
                const FieldValue value = field.Get(instance);
                std::visit([&](const auto& v) {
                    using T = std::decay_t<decltype(v)>;
                    auto expand = [&](int count) {
                        for (int i = 0; i < count; ++i)
                            result.push_back({ field.Name + "." + axes[i], false });
                    };
                    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool>) result.push_back({ field.Name, false });
                    else if constexpr (std::is_same_v<T, glm::vec2>) expand(2);
                    else if constexpr (std::is_same_v<T, glm::vec3>) expand(3);
                    else if constexpr (std::is_same_v<T, glm::vec4> || std::is_same_v<T, Color4>) expand(4);
                    else if constexpr (std::is_same_v<T, AssetRef>) result.push_back({ field.Name, true });
                }, value);
            }
            return result;
        }

        void Sample(Scene& scene, Entity root, const AnimationClipData& clip, float time, bool includeChannels) {
            if (!root)
                return;
            if (includeChannels && !clip.Channels.empty()) {
                // Imported node channels bind by name, preferring the animated object's own
                // hierarchy: two instances of the same model each drive their own skeleton
                // instead of whichever same-named bone the registry lists last. Names missing
                // below `root` still resolve scene-wide (pre-existing single-object clips).
                std::unordered_map<std::string, entt::entity> namedEntities;
                for (auto candidate : scene.Registry().view<NameComponent>())
                    namedEntities[scene.Registry().get<NameComponent>(candidate).Name] = candidate;
                std::vector<Entity> pending{ root };
                std::unordered_map<std::string, entt::entity> ownEntities;
                while (!pending.empty()) {
                    Entity current = pending.back();
                    pending.pop_back();
                    if (!current)
                        continue;
                    ownEntities.emplace(current.GetComponent<NameComponent>().Name, current.Handle());
                    for (Entity child : current.GetComponent<HierarchyComponent>().Children)
                        pending.push_back(child);
                }
                for (auto& [name, handle] : ownEntities)
                    namedEntities[name] = handle;
                for (const AnimationChannel& channel : clip.Channels) {
                    auto found = namedEntities.find(channel.NodeName);
                    if (found == namedEntities.end() || !scene.Registry().all_of<TransformComponent>(found->second)) continue;
                    auto& transform = scene.Registry().get<TransformComponent>(found->second);
                    transform.Translation = AnimationClipLoader::Sample(channel.PositionKeys, time, transform.Translation);
                    transform.Scale = AnimationClipLoader::Sample(channel.ScaleKeys, time, transform.Scale);
                    const glm::quat current = glm::quat(glm::radians(transform.Rotation));
                    transform.Rotation = glm::degrees(glm::eulerAngles(AnimationClipLoader::Sample(channel.RotationKeys, time, current)));
                }
            }
            for (const AnimationFloatCurve& curve : clip.Curves) {
                if (curve.Keys.empty())
                    continue;
                Entity target = FindTarget(scene, root, curve.Path);
                if (!target)
                    continue;
                float fallback = 0.0f;
                ReadFloat(target, curve.Component, curve.Property, fallback);
                WriteFloat(target, curve.Component, curve.Property, AnimationClipLoader::Sample(curve, time, fallback));
            }
            for (const AnimationObjectCurve& curve : clip.ObjectCurves) {
                const std::string* value = AnimationClipLoader::Sample(curve, time);
                if (!value)
                    continue;
                Entity target = FindTarget(scene, root, curve.Path);
                if (target)
                    WriteObject(target, curve.Component, curve.Property, *value);
            }
        }
    }

}
