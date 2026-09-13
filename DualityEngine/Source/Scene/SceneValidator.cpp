#include "DualityEngine/Scene/SceneValidator.h"

#include <cmath>

#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scripting/ScriptRegistry.h"

namespace Duality {

    bool SceneValidationResult::HasErrors() const {
        for (const SceneValidationIssue& issue : Issues) {
            if (issue.Severity == SceneValidationSeverity::Error)
                return true;
        }
        return false;
    }

    namespace {
        std::string EntityName(const Scene& scene, entt::entity handle) {
            const auto& registry = const_cast<Scene&>(scene).Registry();
            if (registry.all_of<NameComponent>(handle))
                return registry.get<NameComponent>(handle).Name;
            return "<unnamed entity>";
        }

        bool HasCanvasAncestor(const Scene& scene, entt::entity handle) {
            const auto& registry = const_cast<Scene&>(scene).Registry();
            Entity current(handle, const_cast<Scene*>(&scene));
            // The hierarchy should be acyclic, but cap the walk so malformed
            // imported data can never turn validation itself into a hang.
            for (int depth = 0; current.IsValid() && depth < 1024; ++depth) {
                if (current.HasComponent<CanvasComponent>())
                    return true;
                const auto* hierarchy = current.TryGetComponent<HierarchyComponent>();
                if (!hierarchy || !hierarchy->Parent.IsValid())
                    break;
                current = hierarchy->Parent;
            }
            return false;
        }
    }

    SceneValidationResult SceneValidator::Validate(const Scene& scene) {
        SceneValidationResult result;
        const auto& registry = const_cast<Scene&>(scene).Registry();

        for (const entt::entity handle : registry.view<BehaviourComponent>()) {
            const BehaviourComponent& behaviours = registry.get<BehaviourComponent>(handle);
            for (const ScriptInstance& script : behaviours.Scripts) {
                if (script.ClassName.empty()) {
                    result.Issues.push_back({ SceneValidationSeverity::Error, EntityName(scene, handle),
                        "has an empty Behaviour script slot." });
                } else if (!ScriptRegistry::IsRegistered(script.ClassName)) {
                    result.Issues.push_back({ SceneValidationSeverity::Error, EntityName(scene, handle),
                        "references script '" + script.ClassName + "', which is not loaded or registered." });
                }
            }
        }

        for (const entt::entity handle : registry.view<UIRectComponent>()) {
            if (!HasCanvasAncestor(scene, handle)) {
                result.Issues.push_back({ SceneValidationSeverity::Error, EntityName(scene, handle),
                    "has a UIRect but is not under a Canvas." });
            }
        }

        for (const entt::entity handle : registry.view<CanvasComponent>()) {
            const CanvasComponent& canvas = registry.get<CanvasComponent>(handle);
            if (!std::isfinite(canvas.ScaleFactor) || canvas.ScaleFactor <= 0.0f) {
                result.Issues.push_back({ SceneValidationSeverity::Error, EntityName(scene, handle),
                    "has an invalid Canvas Scale Factor (it must be greater than zero)." });
            }
        }

        return result;
    }

}
