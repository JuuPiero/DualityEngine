#include "DualityEditor/Panels/LightingPanel.h"

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/SceneOps.h"
#include "DualityEngine/Renderer/BuiltinShaders.h"
#include "DualityEngine/Renderer/RenderSettings.h"
#include "DualityEngine/Scene/Components.h"

namespace Duality {

    bool& LightingPanel::IsOpen() {
        static bool open = false;
        return open;
    }

    void LightingPanel::OnImGuiRender(EditorContext& ctx) {
        bool& open = IsOpen();
        if (!open)
            return;
        ImGui::SetNextWindowSize(ImVec2(520.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Lighting", &open)) {
            ImGui::End();
            return;
        }

        Scene& scene = ctx.SceneRef;
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
            LightingSettings& settings = scene.GetLightingSettings();
            bool changed = false;
            for (const FieldHandle& field : LightingSettings::Fields())
                changed |= DrawFieldWidget(field, &settings, &scene);
            if (settings.AmbientIntensity < 0.0f)
                settings.AmbientIntensity = 0.0f;
            if (changed)
                MarkSceneDirty(ctx);
            const glm::vec3 ambient = settings.EffectiveAmbient();
            ImGui::TextDisabled("Effective ambient: (%.2f, %.2f, %.2f) -- saved with the scene.", ambient.r, ambient.g, ambient.b);
            ImGui::TextDisabled("Flat color only: PICA200 has a single global ambient register.");
        }

        if (ImGui::CollapsingHeader("Scene Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            int directional = 0, point = 0, spot = 0;
            Entity mainLight;
            for (Entity entity : scene.GetHierarchyTraversalOrder()) {
                if (!scene.IsEffectivelyActive(entity))
                    continue;
                if (auto* light = entity.TryGetComponent<DirectionalLightComponent>(); light && light->Enabled) {
                    if (!mainLight)
                        mainLight = entity;
                    ++directional;
                }
                if (auto* light = entity.TryGetComponent<PointLightComponent>(); light && light->Enabled)
                    ++point;
                if (auto* light = entity.TryGetComponent<SpotLightComponent>(); light && light->Enabled)
                    ++spot;
            }
            ImGui::Text("Directional: %d   Point: %d   Spot: %d", directional, point, spot);
            if (mainLight) {
                ImGui::Text("Main light: %s", mainLight.GetComponent<NameComponent>().Name.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Select"))
                    ctx.Selected = mainLight;
            } else {
                ImGui::TextDisabled("No Directional Light: Mobile/VertexLit renders unlit, no blob shadows.");
            }
            const int total = directional + point + spot;
            ImGui::PushTextWrapPos(0.0f);
            if (total > MaxLightsPerDraw)
                ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                    "%d lights: each object is lit by its %d most important ones (PICA200 hardware limit); "
                    "directional lights first, then point/spot by brightness and distance.", total, MaxLightsPerDraw);
            else
                ImGui::TextDisabled("Each object is lit by up to %d lights (PICA200 limit) -- this scene fits.", MaxLightsPerDraw);
            ImGui::PopTextWrapPos();
        }

        if (ImGui::CollapsingHeader("Shadows", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* names[] = { "Off", "Blob Shadows", "Shadow Maps (Experimental)" };
            const int mode = static_cast<int>(RenderSettings::GetShadowMode());
            ImGui::Text("Project technique: %s", mode >= 0 && mode < 3 ? names[mode] : "?");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Cast by the main Directional Light when its Cast Shadows is on (Project Settings > Rendering). "
                "Desktop renders a 512x512 shadow map (it darkens that light's contribution on Lit/Toon materials); "
                "the 3DS always uses projected blob shadows on Plane receivers.");
            ImGui::PopTextWrapPos();
        }

        if (ImGui::CollapsingHeader("Last Game Frame", ImGuiTreeNodeFlags_DefaultOpen)) {
            const SceneRenderStats& stats = ctx.GameRenderStats;
            ImGui::Text("Mesh draws: %u   lit (per-pixel): %u   vertex-lit: %u", stats.MeshDrawCalls, stats.LitDrawCalls, stats.VertexLitDrawCalls);
            ImGui::Text("Transparent/additive draws: %u   blob shadows: %u", stats.TransparentDrawCalls, stats.BlobShadowDrawCalls);
            const float average = stats.LitDrawCalls > 0 ? static_cast<float>(stats.LightsPerDrawTotal) / static_cast<float>(stats.LitDrawCalls) : 0.0f;
            ImGui::Text("Lights in view: %u   lights per lit draw: avg %.1f, max %u", stats.ActiveLights, average, stats.MaxLightsInDraw);
        }

        ImGui::End();
    }

}
