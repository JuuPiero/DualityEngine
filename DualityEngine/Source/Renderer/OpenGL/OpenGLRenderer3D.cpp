#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer3D.h"

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Renderer/OpenGL/GLTextureLoader.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"

namespace Duality {

    namespace {
        // #version 130 + in/out + attribute-location-by-query, matching Window.cpp's
        // ImGui_ImplOpenGL3_Init("#version 130") -- proven to work in this exact legacy-
        // compatibility-profile context, see this class's own header comment.
        const char* VertexShaderSource =
            "#version 130\n"
            "uniform mat4 u_ViewProjection;\n"
            "uniform mat4 u_Model;\n"
            "uniform mat3 u_NormalMatrix;\n"
            "uniform int u_ShadingMode;\n"
            "uniform vec3 u_AmbientColor;\n"
            "uniform vec3 u_LightDirection;\n"
            "uniform vec3 u_LightColor;\n"
            "uniform float u_LightIntensity;\n"
            "in vec3 a_Position;\n"
            "in vec2 a_TexCoord;\n"
            "in vec3 a_Normal;\n"
            "out vec2 v_TexCoord;\n"
            "out vec3 v_Lighting;\n"
            "void main() {\n"
            "    v_TexCoord = a_TexCoord;\n"
            "    v_Lighting = vec3(1.0);\n"
            "    if (u_ShadingMode != 0) {\n"
            // Keep the desktop shader at GLSL 1.30, the same compatibility profile used by
            // the editor and ImGui backend. GLSL 1.30 does not reliably provide inverse(),
            // so the inverse-transpose normal matrix is calculated on the CPU per draw.
            "        vec3 normal = normalize(u_NormalMatrix * a_Normal);\n"
            "        float diffuse = max(dot(normal, normalize(u_LightDirection)), 0.0);\n"
            "        v_Lighting = u_AmbientColor + u_LightColor * (u_LightIntensity * diffuse);\n"
            "    }\n"
            "    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);\n"
            "}\n";

        const char* FragmentShaderSource =
            "#version 130\n"
            "uniform sampler2D u_Texture;\n"
            "uniform vec4 u_Color;\n"
            "in vec2 v_TexCoord;\n"
            "in vec3 v_Lighting;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    FragColor = texture(u_Texture, v_TexCoord) * u_Color * vec4(v_Lighting, 1.0);\n"
            "}\n";

        // T * Rz * Ry * Rx * S -- must match Citro3DRenderer::ComposeWorldMtx's composition
        // order exactly (see that function's own comment), or a multi-axis rotation would look
        // different across platforms even though each backend builds its matrix with its own
        // native math library.
        glm::mat4 ComposeWorldMtx(const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), translation);
            m = glm::rotate(m, glm::radians(rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
            m = glm::rotate(m, glm::radians(rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::rotate(m, glm::radians(rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            m = glm::scale(m, scale);
            return m;
        }
    }

    GLVertexArray OpenGLRenderer3D::UploadGpuMesh(const std::vector<MeshVertex>& vertices) {
        GLVertexArray mesh;
        mesh.Init();
        mesh.Bind();
        mesh.SetVertexData(vertices.data(), vertices.size() * sizeof(MeshVertex), static_cast<int>(vertices.size()));
        mesh.AddFloatAttribute(m_AttribPosition, 3, sizeof(MeshVertex), offsetof(MeshVertex, Position));
        mesh.AddFloatAttribute(m_AttribTexCoord, 2, sizeof(MeshVertex), offsetof(MeshVertex, TexCoord));
        mesh.AddFloatAttribute(m_AttribNormal, 3, sizeof(MeshVertex), offsetof(MeshVertex, Normal));
        mesh.Unbind();
        return mesh;
    }

    void OpenGLRenderer3D::Init() {
        m_Shader.Init(VertexShaderSource, FragmentShaderSource);

        m_UniformViewProjection = m_Shader.GetUniformLocation("u_ViewProjection");
        m_UniformModel = m_Shader.GetUniformLocation("u_Model");
        m_UniformNormalMatrix = m_Shader.GetUniformLocation("u_NormalMatrix");
        m_UniformColor = m_Shader.GetUniformLocation("u_Color");
        m_UniformTexture = m_Shader.GetUniformLocation("u_Texture");
        m_UniformShadingMode = m_Shader.GetUniformLocation("u_ShadingMode");
        m_UniformAmbientColor = m_Shader.GetUniformLocation("u_AmbientColor");
        m_UniformLightDirection = m_Shader.GetUniformLocation("u_LightDirection");
        m_UniformLightColor = m_Shader.GetUniformLocation("u_LightColor");
        m_UniformLightIntensity = m_Shader.GetUniformLocation("u_LightIntensity");
        m_AttribPosition = m_Shader.GetAttribLocation("a_Position");
        m_AttribTexCoord = m_Shader.GetAttribLocation("a_TexCoord");
        m_AttribNormal = m_Shader.GetAttribLocation("a_Normal");

        for (int i = 0; i < static_cast<int>(MeshPrimitive::Count); i++) {
            const std::vector<MeshVertex>& vertices = GetPrimitiveMesh(static_cast<MeshPrimitive>(i));
            m_Meshes[i] = UploadGpuMesh(vertices);
        }

        unsigned char whitePixel[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &m_WhiteTexture);
        glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void OpenGLRenderer3D::Shutdown() {
        for (GLVertexArray& mesh : m_Meshes)
            mesh.Shutdown();
        for (GLVertexArray& mesh : m_ImportedMeshes)
            mesh.Shutdown();
        m_ImportedMeshes.clear();
        m_MeshCache.clear();
        glDeleteTextures(1, &m_WhiteTexture);
        for (auto& [path, textureId] : m_TextureCache) {
            unsigned int id = textureId;
            glDeleteTextures(1, &id);
        }
        m_TextureCache.clear();

        m_Shader.Shutdown();
    }

    void OpenGLRenderer3D::BeginScene(Screen /*screen*/, ProjectionType projection, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float orthoHalfHeight, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor, bool clear) {
        m_RenderView = {};
        m_DrawCallCount = 0;

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        // The 3D Scene view uses this renderer for SpriteRenderer's world-space quad
        // preview too. Without alpha blending, transparent texels write their black RGB
        // values directly into the framebuffer, appearing as rectangular borders around
        // sprites. The Game's 2D renderer uses this same straight-alpha convention.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // Color is only cleared when nothing else will (see IRenderer3D.h's own doc comment on
        // this parameter) -- but the depth buffer always needs a fresh clear regardless, for
        // this renderer's own meshes to depth-test correctly against each other;
        // OpenGLRenderer2D's sprite pass never touches depth.
        if (clear) {
            glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        } else {
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        glm::mat4 projectionMtx;
        if (projection == ProjectionType::Perspective) {
            projectionMtx = glm::perspective(glm::radians(fovDegrees), aspectRatio, nearPlane, farPlane);
        } else {
            // top/bottom swapped from the "normal" Y-up convention -- matches this engine's
            // pixel-space Y-down convention (see OpenGLRenderer2D's own glOrtho top/bottom
            // swap), so a mesh and a sprite at the same world Y land on the same screen row
            // when composited together.
            float halfWidth = orthoHalfHeight * aspectRatio;
            projectionMtx = glm::ortho(-halfWidth, halfWidth, orthoHalfHeight, -orthoHalfHeight, nearPlane, farPlane);
        }

        // View = inverse of the camera's own world transform (position + rotation, no scale)
        // -- built the exact same T*Rz*Ry*Rx*S order DrawMesh builds a mesh's model matrix
        // with, matching Citro3DRenderer::BeginScene's own approach.
        glm::mat4 cameraWorld = ComposeWorldMtx(cameraPosition, cameraRotationDegrees, glm::vec3(1.0f));
        glm::mat4 view = glm::inverse(cameraWorld);

        m_ViewProjection = projectionMtx * view;
    }

    void OpenGLRenderer3D::BeginScene(const RenderView& view, const glm::vec4& clearColor, bool clear) {
        BeginScene(view.TargetScreen, view.Projection, view.CameraPosition, view.CameraRotationDegrees,
            view.FovDegrees, view.OrthoHalfHeight, view.AspectRatio, view.NearPlane, view.FarPlane, clearColor, clear);
        m_RenderView = view;
    }

    void OpenGLRenderer3D::EndScene() {
        // Both reset rather than left set -- OpenGLRenderer2D's own draws (this screen's Scene
        // view pane, or the other screen's Game/Scene view) run later in the same frame via the
        // legacy fixed-function pipeline (glBegin/glVertex2f, no shader of its own). Leaving
        // this class's shader program bound made those later immediate-mode calls render
        // through it instead of fixed-function -- confirmed by a real visual bug (the Scene
        // view's 2D panes showing 3D-shaded garbage) before this fix, the desktop-side analog
        // of Citro3DRenderer's own "re-bind everything, assume nothing persists" coexistence
        // rule, just in the opposite direction (3D must clean up after itself instead of before).
        glDepthMask(GL_TRUE);
        glDisable(GL_DEPTH_TEST);
        // OpenGLRenderer2D runs after this pass through the compatibility pipeline and expects
        // conventional straight-alpha blending for sprites/UI.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_Shader.Unbind();
    }

    void OpenGLRenderer3D::SetDepthWriteEnabled(bool enabled) {
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    void OpenGLRenderer3D::DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, uint32_t subMeshIndex, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId) {
        DrawMesh(MeshDrawCommand{ primitive, meshHandle, subMeshIndex, translation, rotationDegrees, scale, color, textureId, MaterialShadingMode::Unlit });
    }

    void OpenGLRenderer3D::DrawMesh(const MeshDrawCommand& command) {
        m_DrawCallCount++;

        // The scene submits projected blob shadows as translucent, depth-tested overlays. Keep
        // their depth writes disabled so a shadow cannot occlude sprites/meshes drawn later;
        // ordinary mesh commands restore the opaque default on their next draw.
        glDepthMask(command.DepthWrite ? GL_TRUE : GL_FALSE);
        glBlendFunc(command.AlphaBlend ? GL_SRC_ALPHA : GL_ONE,
            command.AlphaBlend ? GL_ONE_MINUS_SRC_ALPHA : GL_ZERO);
        m_Shader.Bind();

        glm::mat4 model = ComposeWorldMtx(command.Translation, command.RotationDegrees, command.Scale);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        m_Shader.SetUniformMat4(m_UniformViewProjection, m_ViewProjection);
        m_Shader.SetUniformMat4(m_UniformModel, model);
        glUniformMatrix3fv(m_UniformNormalMatrix, 1, GL_FALSE, &normalMatrix[0][0]);
        m_Shader.SetUniformVec4(m_UniformColor, command.Color);
        glUniform1i(m_UniformShadingMode, command.ShadingMode == MaterialShadingMode::VertexLit && m_RenderView.MainLight.Enabled ? 1 : 0);
        // These shader uniforms are GLSL vec3s. Calling glUniform4f (the vec4 helper) for
        // them is GL_INVALID_OPERATION; the uniforms then retain their all-zero defaults and
        // every VertexLit material is multiplied to black. Keep the upload type exact.
        m_Shader.SetUniformVec3(m_UniformAmbientColor, m_RenderView.AmbientColor);
        m_Shader.SetUniformVec3(m_UniformLightDirection, m_RenderView.MainLight.Direction);
        m_Shader.SetUniformVec3(m_UniformLightColor, m_RenderView.MainLight.Color);
        glUniform1f(m_UniformLightIntensity, m_RenderView.MainLight.Intensity);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, command.TextureId != 0 ? command.TextureId : m_WhiteTexture);
        m_Shader.SetUniformInt(m_UniformTexture, 0);

        const GLVertexArray& mesh = (command.MeshHandle != 0) ? m_ImportedMeshes[command.MeshHandle - 1] : m_Meshes[static_cast<int>(command.Primitive)];
        mesh.Bind();
        // An imported mesh with real submesh ranges draws only that one slice; everything else
        // (procedural primitives, or an imported mesh with no material-group boundaries at all,
        // i.e. GetSubMeshes() empty) draws as one whole mesh, exactly like before this feature.
        const std::vector<MeshData::SubMesh>& subMeshes = mesh.GetSubMeshes();
        if (command.MeshHandle != 0 && command.SubMeshIndex < subMeshes.size()) {
            const MeshData::SubMesh& subMesh = subMeshes[command.SubMeshIndex];
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(subMesh.FirstVertex), static_cast<GLsizei>(subMesh.VertexCount));
        } else {
            glDrawArrays(GL_TRIANGLES, 0, mesh.GetVertexCount());
        }
        mesh.Unbind();

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    uint32_t OpenGLRenderer3D::GetSubMeshCount(uint32_t meshHandle) const {
        if (meshHandle == 0)
            return 1; // procedural primitive -- always one whole-mesh draw
        const std::vector<MeshData::SubMesh>& subMeshes = m_ImportedMeshes[meshHandle - 1].GetSubMeshes();
        return subMeshes.empty() ? 1 : static_cast<uint32_t>(subMeshes.size());
    }

    uint32_t OpenGLRenderer3D::LoadTexture(const std::string& path) {
        auto it = m_TextureCache.find(path);
        if (it != m_TextureCache.end())
            return it->second;

        uint32_t texture = GLTextureLoader::LoadTextureFromFile(path);
        m_TextureCache[path] = texture;
        return texture;
    }

    uint32_t OpenGLRenderer3D::LoadMesh(const std::string& path) {
        auto it = m_MeshCache.find(path);
        if (it != m_MeshCache.end())
            return it->second;

        uint32_t meshHandle = 0;
        const MeshData& data = MeshLoader::Load(path);
        if (!data.Vertices.empty()) {
            GLVertexArray mesh = UploadGpuMesh(data.Vertices);
            mesh.SetSubMeshes(data.SubMeshes);
            m_ImportedMeshes.push_back(std::move(mesh));
            meshHandle = static_cast<uint32_t>(m_ImportedMeshes.size()); // 1-based, 0 reserved for "none"
        }

        m_MeshCache[path] = meshHandle; // cache failures too, matching LoadTexture above
        return meshHandle;
    }

    void OpenGLRenderer3D::UnloadAllTextures() {
        for (auto& [path, textureId] : m_TextureCache) {
            if (textureId != 0) {
                GLuint id = textureId;
                glDeleteTextures(1, &id);
            }
        }
        m_TextureCache.clear();
    }

    void OpenGLRenderer3D::UnloadAllMeshes() {
        // m_Meshes[3] (the built-in procedural primitives) is untouched -- only
        // m_ImportedMeshes (LoadMesh's own uploads) is ever freed here.
        for (auto& mesh : m_ImportedMeshes)
            mesh.Shutdown();
        m_ImportedMeshes.clear();
        m_MeshCache.clear();
    }

}
