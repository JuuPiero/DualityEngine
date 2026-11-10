#include "DualityEditor/BuiltinMeshPreviews.h"

#include <array>
#include <memory>

#include <glm/glm.hpp>

#include "DualityEditor/Framebuffer.h"
#include "DualityEngine/Renderer/IRenderer3D.h"
#include "DualityEngine/Renderer/RenderView.h"

namespace Duality::BuiltinMeshPreviews {

    namespace {
        constexpr uint32_t PreviewSize = 64;

        // Same key light / ambient as the Material Inspector preview, a light-gray material.
        void Render(IRenderer3D& renderer, MeshPrimitive primitive, Framebuffer& target) {
            RenderView view;
            view.Projection = ProjectionType::Perspective;
            view.CameraPosition = { 0.0f, 0.0f, 0.0f };
            view.FovDegrees = 30.0f;
            view.AspectRatio = 1.0f;
            view.NearPlane = 0.1f;
            view.FarPlane = 10.0f;
            view.AmbientColor = glm::vec3(0.42f);
            view.MainLight.Enabled = true;
            view.MainLight.Direction = glm::normalize(glm::vec3(-0.4f, 0.7f, 0.9f));
            view.MainLight.Color = glm::vec3(1.0f);
            view.MainLight.Intensity = 1.0f;
            SceneLight key;
            key.Type = LightType::Directional;
            key.Direction = view.MainLight.Direction;
            key.Color = glm::vec3(1.0f);
            view.Lights = { key };
            view.MainLightIndex = 0;

            MeshDrawCommand command;
            command.Primitive = primitive;
            // A three-quarter view from slightly above, like Unity's mesh previews. Flat meshes
            // get tilted so they don't show edge-on.
            command.Translation = { 0.0f, 0.0f, -3.9f };
            command.RotationDegrees = { 20.0f, -35.0f, 0.0f };
            command.Scale = glm::vec3(1.0f);
            if (primitive == MeshPrimitive::Plane)
                command.RotationDegrees = { 35.0f, -35.0f, 0.0f };
            else if (primitive == MeshPrimitive::Quad)
                command.RotationDegrees = { 0.0f, -30.0f, 0.0f };
            else if (primitive == MeshPrimitive::Capsule || primitive == MeshPrimitive::Cylinder)
                command.Scale = glm::vec3(1.6f);
            command.Color = { 0.78f, 0.78f, 0.8f, 1.0f };
            command.ShadingMode = MaterialShadingMode::VertexLit;

            target.Bind();
            renderer.BeginScene(view, { 0.19f, 0.19f, 0.21f, 1.0f }, true);
            renderer.DrawMesh(command);
            renderer.EndScene();
            target.Unbind();
        }
    }

    uint32_t Get(IRenderer3D& renderer, MeshPrimitive primitive) {
        const int index = static_cast<int>(primitive);
        if (index < 0 || index >= static_cast<int>(MeshPrimitive::Count))
            return 0;
        // Never destroyed: the GL objects must not be released after the context is gone.
        static std::array<Framebuffer*, static_cast<size_t>(MeshPrimitive::Count)> previews{};
        if (!previews[static_cast<size_t>(index)]) {
            previews[static_cast<size_t>(index)] = new Framebuffer(PreviewSize, PreviewSize);
            Render(renderer, primitive, *previews[static_cast<size_t>(index)]);
        }
        return previews[static_cast<size_t>(index)]->GetColorAttachment();
    }

}
