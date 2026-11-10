#include "DualityEditor/SceneView/SceneViewState.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "DualityEditor/EditorContext.h"
#include "DualityEngine/Project/Project.h"

namespace Duality::SceneViewState {

    namespace {
        using json = nlohmann::json;

        std::filesystem::path StateFile() {
            auto project = Project::GetActive();
            if (!project)
                return {};
            return std::filesystem::path(project->GetDirectory()) / "Library" / "SceneViewState.json";
        }

        std::string SceneKey(const EditorContext& ctx) {
            auto project = Project::GetActive();
            if (!project || ctx.ScenePath.empty())
                return {};
            std::error_code error;
            const auto relative = std::filesystem::relative(ctx.ScenePath, project->GetAssetsDirectory(), error);
            return (error ? std::filesystem::path(ctx.ScenePath) : relative).generic_string();
        }

        json ReadAll() {
            std::ifstream file(StateFile());
            if (!file.is_open())
                return json::object();
            json all = json::parse(file, nullptr, false);
            return all.is_object() ? all : json::object();
        }

        json PaneToJson(const SceneViewCamera& camera, const SceneViewCamera3D& camera3D, RenderMode mode) {
            return {
                { "Mode", mode == RenderMode::Mode3D ? "3D" : "2D" },
                { "Center2D", { camera.Position.x, camera.Position.y } },
                { "Zoom2D", camera.Zoom },
                { "Pivot", { camera3D.Target.x, camera3D.Target.y, camera3D.Target.z } },
                { "Distance", camera3D.Distance },
                { "Yaw", camera3D.Yaw },
                { "Pitch", camera3D.Pitch },
                { "Orthographic", camera3D.Orthographic },
                { "FlySpeed", camera3D.FlySpeed },
            };
        }

        void PaneFromJson(const json& pane, SceneViewCamera& camera, SceneViewCamera3D& camera3D, RenderMode& mode) {
            try {
                mode = pane.value("Mode", std::string("2D")) == "3D" ? RenderMode::Mode3D : RenderMode::Mode2D;
                if (pane.contains("Center2D") && pane["Center2D"].size() == 2) {
                    camera.Position = { pane["Center2D"][0].get<float>(), pane["Center2D"][1].get<float>() };
                    camera.Zoom = pane.value("Zoom2D", camera.Zoom);
                    camera.Seeded = true;
                }
                if (pane.contains("Pivot") && pane["Pivot"].size() == 3) {
                    camera3D.Target = { pane["Pivot"][0].get<float>(), pane["Pivot"][1].get<float>(), pane["Pivot"][2].get<float>() };
                    camera3D.Distance = pane.value("Distance", camera3D.Distance);
                    camera3D.Yaw = pane.value("Yaw", camera3D.Yaw);
                    camera3D.Pitch = pane.value("Pitch", camera3D.Pitch);
                    camera3D.Orthographic = pane.value("Orthographic", false);
                    camera3D.FlySpeed = pane.value("FlySpeed", 1.0f);
                    camera3D.SyncRotation();
                    camera3D.Seeded = true;
                }
            } catch (const json::exception&) {
                // A malformed entry keeps the default view.
            }
        }
    }

    void Load(EditorContext& ctx) {
        const std::string key = SceneKey(ctx);
        if (key.empty())
            return;
        const json all = ReadAll();
        if (!all.contains(key) || !all[key].is_object())
            return;
        const json& scene = all[key];
        if (scene.contains("Top"))
            PaneFromJson(scene["Top"], ctx.TopSceneView, ctx.TopSceneView3D, ctx.TopRenderMode);
        if (scene.contains("Bottom"))
            PaneFromJson(scene["Bottom"], ctx.BottomSceneView, ctx.BottomSceneView3D, ctx.BottomRenderMode);
    }

    void Save(EditorContext& ctx) {
        const std::string key = SceneKey(ctx);
        const std::filesystem::path file = StateFile();
        if (key.empty() || file.empty())
            return;
        json all = ReadAll();
        all[key] = {
            { "Top", PaneToJson(ctx.TopSceneView, ctx.TopSceneView3D, ctx.TopRenderMode) },
            { "Bottom", PaneToJson(ctx.BottomSceneView, ctx.BottomSceneView3D, ctx.BottomRenderMode) },
        };
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        std::ofstream out(file);
        if (out.is_open())
            out << all.dump(2);
    }

}
