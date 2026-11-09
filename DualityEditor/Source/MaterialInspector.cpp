#include "DualityEditor/MaterialInspector.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <imgui.h>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/FieldEditorWidget.h"
#include "DualityEditor/Framebuffer.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Renderer/LightingMath.h"
#include "DualityEngine/Renderer/SceneRenderer.h"

namespace Duality {

    namespace {
        constexpr uint32_t PreviewSize = 160;

        // Shader dropdown order (grouped like Unity's Shader menu).
        const BuiltinShader ShaderMenu[] = {
            BuiltinShader::LitDiffuse, BuiltinShader::LitSpecular, BuiltinShader::ToonLit,
            BuiltinShader::MobileVertexLit,
            BuiltinShader::UnlitColor, BuiltinShader::UnlitTexture, BuiltinShader::UnlitTransparent,
            BuiltinShader::UnlitCutout, BuiltinShader::UnlitAdditive, BuiltinShader::UnlitVertexColor,
        };

        bool AssetSlot(const char* label, AssetRef& ref) {
            FieldValue value = ref;
            if (!DrawFieldValueWidget(label, value))
                return false;
            ref = std::get<AssetRef>(value);
            return true;
        }

        bool ColorRgb(const char* label, glm::vec4& color) {
            return ImGui::ColorEdit3(label, &color.x);
        }

        // Renders the material on a sphere lit by a key directional light, a warm point fill
        // light and the default ambient -- through the same OpenGLRenderer3D path the Scene
        // and Game views use, so the preview is exactly what the desktop draws.
        void DrawPreview(const Material& material, EditorContext& ctx) {
            static std::unique_ptr<Framebuffer> framebuffer;
            if (!framebuffer)
                framebuffer = std::make_unique<Framebuffer>(PreviewSize, PreviewSize);

            RenderView view;
            view.Projection = ProjectionType::Perspective;
            view.CameraPosition = { 0.0f, 0.0f, 2.4f };
            view.FovDegrees = 30.0f;
            view.AspectRatio = 1.0f;
            view.NearPlane = 0.1f;
            view.FarPlane = 10.0f;
            view.AmbientColor = glm::vec3(0.18f);
            view.MainLight.Enabled = true;
            view.MainLight.Direction = glm::normalize(glm::vec3(-0.6f, 0.7f, 0.8f));
            view.MainLight.Color = glm::vec3(1.0f);
            view.MainLight.Intensity = 1.0f;
            SceneLight key;
            key.Type = LightType::Directional;
            key.Direction = view.MainLight.Direction;
            key.Color = glm::vec3(1.0f);
            SceneLight fill;
            fill.Type = LightType::Point;
            fill.Position = { 1.6f, -0.6f, 1.2f };
            fill.Color = { 0.55f, 0.4f, 0.25f };
            fill.Range = 4.0f;
            view.Lights = { key, fill };
            view.MainLightIndex = 0;

            framebuffer->Bind();
            ctx.Renderer3D.BeginScene(view, { 0.13f, 0.13f, 0.15f, 1.0f }, true);
            MeshDrawCommand command;
            command.Primitive = MeshPrimitive::Sphere;
            command.Scale = glm::vec3(1.0f);
            command.Color = material.Color;
            command.ShadingMode = material.ShadingMode;
            command.HasMaterial = true;
            command.Material = BuildRenderMaterial(ctx.Renderer3D, material, command.TextureId);
            command.LightCount = 2;
            command.LightIndices[0] = 0;
            command.LightIndices[1] = 1;
            ctx.Renderer3D.DrawMesh(command);
            ctx.Renderer3D.EndScene();
            framebuffer->Unbind();

            ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(framebuffer->GetColorAttachment())),
                ImVec2(static_cast<float>(PreviewSize), static_cast<float>(PreviewSize)), ImVec2(0, 1), ImVec2(1, 0));
        }

        const char* PlatformNote(const Material& material) {
            switch (material.GetLightingModel()) {
                case LightingModel::Unlit:
                    return "3DS: 1 TEV stage, no lighting hardware -- the cheapest path.";
                case LightingModel::VertexLit:
                    return "3DS: lighting computed in the vertex shader (main Directional Light only).";
                case LightingModel::Lambert:
                    return "3DS: PICA200 fragment lighting (per pixel, up to 8 lights). Normal map uses hardware bump mapping on texture unit 1.";
                case LightingModel::BlinnPhong:
                    return "3DS: PICA200 fragment lighting + D0 specular LUT (256 entries, interpolated).";
                case LightingModel::Toon:
                    return "3DS: ramp in the D1 LUT (L.N), highlight in D0 (N.H). Normal maps are not used by Toon.";
            }
            return "";
        }
    }

    void DrawMaterialInspector(const std::string& path, EditorContext& ctx) {
        Material material = MaterialLoader::Load(path);
        bool changed = false;
        const ShaderInfo& info = material.GetShaderInfo();

        // Header: preview + shader selection.
        DrawPreview(material, ctx);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted("Shader");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##Shader", info.Name)) {
            std::string lastGroup;
            for (BuiltinShader shader : ShaderMenu) {
                const ShaderInfo& option = GetShaderInfo(shader);
                const char* slash = std::strchr(option.Name, '/');
                std::string group = slash ? std::string(option.Name, slash) : std::string(option.Name);
                if (group != lastGroup) {
                    if (!lastGroup.empty())
                        ImGui::Separator();
                    ImGui::TextDisabled("%s", group.c_str());
                    lastGroup = group;
                }
                const bool selected = shader == material.Shader;
                if (ImGui::Selectable(option.Name, selected)) {
                    material.SetShader(shader);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", option.Description);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", material.GetShaderInfo().Description);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        const ShaderInfo& current = material.GetShaderInfo();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Surface Options", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* surfaceNames[] = { "Opaque", "Cutout", "Transparent", "Additive" };
            int surface = static_cast<int>(material.EffectiveSurface());
            ImGui::BeginDisabled(current.SurfaceLocked);
            if (ImGui::Combo("Surface Type", &surface, surfaceNames, IM_ARRAYSIZE(surfaceNames))) {
                material.Surface = static_cast<SurfaceType>(surface);
                changed = true;
            }
            ImGui::EndDisabled();
            if (current.SurfaceLocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Fixed by %s", current.Name);

            const char* cullNames[] = { "Back", "Front", "Off (two-sided)" };
            int cull = static_cast<int>(material.Cull);
            if (ImGui::Combo("Cull", &cull, cullNames, IM_ARRAYSIZE(cullNames))) {
                material.Cull = static_cast<CullMode>(cull);
                changed = true;
            }
            const char* zwriteNames[] = { "Auto", "On", "Off" };
            int zwrite = static_cast<int>(material.ZWrite);
            if (ImGui::Combo("ZWrite", &zwrite, zwriteNames, IM_ARRAYSIZE(zwriteNames))) {
                material.ZWrite = static_cast<ZWriteMode>(zwrite);
                changed = true;
            }
            bool fromShader = material.RenderQueue < 0;
            if (ImGui::Checkbox("Queue From Surface", &fromShader)) {
                material.RenderQueue = fromShader ? -1 : material.EffectiveRenderQueue();
                changed = true;
            }
            ImGui::SameLine();
            int queue = material.EffectiveRenderQueue();
            ImGui::BeginDisabled(fromShader);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragInt("##RenderQueue", &queue, 1.0f, 0, 5000)) {
                material.RenderQueue = std::clamp(queue, 0, 5000);
                changed = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Render Queue: 2000 Geometry, 2450 AlphaTest, 3000 Transparent (sorted back to front).");
        }

        if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            const uint32_t props = current.Properties;
            if (props & ShaderProp_MainTex) {
                changed |= AssetSlot("Main Texture", material.Texture);
                glm::vec2 tiling{ material.MainTexST.x, material.MainTexST.y };
                glm::vec2 offset{ material.MainTexST.z, material.MainTexST.w };
                if (ImGui::DragFloat2("Tiling", &tiling.x, 0.01f)) {
                    material.MainTexST.x = tiling.x; material.MainTexST.y = tiling.y; changed = true;
                }
                if (ImGui::DragFloat2("Offset", &offset.x, 0.01f)) {
                    material.MainTexST.z = offset.x; material.MainTexST.w = offset.y; changed = true;
                }
            }
            if (props & ShaderProp_Color)
                changed |= ImGui::ColorEdit4(current.UsesTexture ? "Color (Tint)" : "Color", &material.Color.x);
            if ((props & ShaderProp_Cutoff) || material.EffectiveSurface() == SurfaceType::Cutout)
                changed |= ImGui::SliderFloat("Alpha Cutoff", &material.Cutoff, 0.0f, 1.0f);
            if (props & ShaderProp_BumpMap) {
                changed |= AssetSlot("Normal Map", material.NormalMap);
                if (!material.NormalMap.Guid.empty())
                    ImGui::TextDisabled("Tangent-space, OpenGL (Y+) convention; uses the Main Texture tiling.");
            }
            if (props & ShaderProp_Toon) {
                changed |= ColorRgb("Shade Color", material.ShadeColor);
                changed |= ImGui::SliderInt("Ramp Steps", &material.ToonSteps, 1, 5);
                changed |= ImGui::SliderFloat("Ramp Smoothness", &material.ToonSmoothness, 0.0f, 1.0f);
            }
            if (props & ShaderProp_Specular) {
                changed |= ColorRgb(material.Shader == BuiltinShader::ToonLit ? "Highlight Color" : "Specular Color", material.SpecColor);
                changed |= ImGui::SliderFloat("Shininess", &material.Shininess, 0.01f, 1.0f);
                ImGui::TextDisabled("Exponent %.0f (Shininess x 128)", LightingMath::ShininessToExponent(material.Shininess));
            }
            if (props & ShaderProp_Emission)
                changed |= ColorRgb("Emission", material.EmissionColor);
        }

        if (ImGui::CollapsingHeader("Platform Notes")) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", PlatformNote(material));
            ImGui::TextDisabled("Every shader multiplies baked vertex colors (OBJ 'v x y z r g b'), except Unlit/Color.");
            ImGui::TextDisabled("Colors saturate at 1.0 per channel on both platforms (PICA200 color registers are 8-bit).");
            ImGui::PopTextWrapPos();
        }

        if (changed)
            MaterialLoader::Save(path, material);
    }

}
