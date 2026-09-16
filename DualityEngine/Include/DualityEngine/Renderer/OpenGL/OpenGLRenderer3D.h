#pragma once

#include <unordered_map>
#include <vector>

#include "DualityEngine/Renderer/IRenderer3D.h"
#include "DualityEngine/Renderer/OpenGL/GLShaderProgram.h"
#include "DualityEngine/Renderer/OpenGL/GLVertexArray.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"

namespace Duality {

    // Desktop OpenGL implementation of IRenderer3D -- modern GL (shader program, VAO/VBO), in
    // the exact same legacy-compatibility-profile context OpenGLRenderer2D's fixed-function
    // calls already run in (confirmed safe: Dear ImGui's own OpenGL3 backend already runs a
    // real "#version 130" shader + VAO/VBO in this context every frame, see Window.cpp's
    // ImGui_ImplOpenGL3_Init("#version 130") call -- this class matches that same GLSL version
    // and attribute-location-by-query convention rather than assuming a newer one).
    //
    // Caller is responsible for binding whatever render target (e.g. an FBO) and glViewport it
    // wants rendered into before calling BeginScene, same convention as OpenGLRenderer2D.
    class OpenGLRenderer3D final : public IRenderer3D {
    public:
        void Init() override;
        void Shutdown() override;

        void BeginScene(Screen screen, ProjectionType projection, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float orthoHalfHeight, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor, bool clear) override;
        void BeginScene(const RenderView& view, const glm::vec4& clearColor, bool clear) override;
        void EndScene() override;
        bool BeginDirectionalShadowMap(const ShadowMapPass& pass) override;
        void DrawDirectionalShadowCaster(const MeshDrawCommand& command) override;
        void EndDirectionalShadowMap() override;

        // ScenePanel draws transparent SpriteRenderer quads after opaque meshes. Keeping
        // depth testing on but disabling depth writes for that pass prevents transparent
        // texels from blocking later sprites behind the quad's rectangular bounds.
        // Desktop-editor convenience; the platform-neutral renderer contract stays minimal.
        void SetDepthWriteEnabled(bool enabled);

        void DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, uint32_t subMeshIndex, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId = 0) override;
        void DrawMesh(const MeshDrawCommand& command) override;
        uint32_t GetSubMeshCount(uint32_t meshHandle) const override;

        uint32_t LoadTexture(const std::string& path) override;
        uint32_t LoadMesh(const std::string& path) override;
        uint32_t CreateDynamicMesh(const MeshData& mesh) override;
        void UpdateDynamicMesh(uint32_t meshHandle, const MeshData& mesh) override;
        void UnloadAllTextures() override;
        void UnloadAllMeshes() override;
        uint32_t GetDrawCallCount() const override { return m_DrawCallCount; }

    private:
        // Shared by Init()'s 3 built-in primitives and LoadMesh's imported meshes -- uploads
        // `vertices` to a fresh GLVertexArray using this instance's already-queried attribute
        // locations (m_AttribPosition/m_AttribTexCoord).
        GLVertexArray UploadGpuMesh(const std::vector<MeshVertex>& vertices);

        GLShaderProgram m_Shader;
        int m_UniformViewProjection = -1;
        int m_UniformModel = -1;
        int m_UniformNormalMatrix = -1;
        int m_UniformColor = -1;
        int m_UniformTexture = -1;
        int m_UniformShadingMode = -1;
        int m_UniformAmbientColor = -1;
        int m_UniformLightDirection = -1;
        int m_UniformLightColor = -1;
        int m_UniformLightIntensity = -1;
        int m_UniformShadowMatrix = -1;
        int m_UniformShadowMap = -1;
        int m_UniformUseShadowMap = -1;
        int m_UniformSkinned = -1;
        int m_UniformBones = -1;
        int m_AttribPosition = -1;
        int m_AttribTexCoord = -1;
        int m_AttribNormal = -1;
        int m_AttribVertexColor = -1;
        int m_AttribBoneIndices = -1;
        int m_AttribBoneWeights = -1;

        // 1x1 white pixel bound when textureId == 0, so DrawMesh can always sample the texture
        // uniformly in the shader instead of branching on whether one is bound.
        unsigned int m_WhiteTexture = 0;
        unsigned int m_ShadowFramebuffer = 0;
        unsigned int m_ShadowDepthTexture = 0;
        GLShaderProgram m_ShadowShader;
        int m_ShadowUniformViewProjection = -1;
        int m_ShadowUniformModel = -1;
        int m_ShadowUniformSkinned = -1;
        int m_ShadowUniformBones = -1;
        ShadowMapPass m_ActiveShadowPass{};
        int m_PreviousFramebuffer = 0;
        int m_PreviousViewport[4]{ 0, 0, 0, 0 };

        GLVertexArray m_Meshes[static_cast<int>(MeshPrimitive::Count)]; // indexed by static_cast<int>(MeshPrimitive)

        // Same 1-based-handle/0-reserved convention as m_TextureCache below, for LoadMesh-
        // imported meshes (see MeshLoader.h) -- separate from m_Meshes since that one is
        // fixed-size (indexed by MeshPrimitive), while this one grows per distinct imported file.
        std::vector<GLVertexArray> m_ImportedMeshes;
        std::unordered_map<std::string, uint32_t> m_MeshCache;

        // Recomputed once per BeginScene, reused by every DrawMesh call in that bracket.
        glm::mat4 m_ViewProjection{ 1.0f };
        RenderView m_RenderView{};

        std::unordered_map<std::string, uint32_t> m_TextureCache;
        uint32_t m_DrawCallCount = 0;
    };

}
