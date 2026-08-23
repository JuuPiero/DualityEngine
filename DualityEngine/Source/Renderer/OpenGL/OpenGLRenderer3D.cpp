#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer3D.h"

#include <cstdio>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

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
            "in vec3 a_Position;\n"
            "in vec2 a_TexCoord;\n"
            "out vec2 v_TexCoord;\n"
            "void main() {\n"
            "    v_TexCoord = a_TexCoord;\n"
            "    gl_Position = u_ViewProjection * u_Model * vec4(a_Position, 1.0);\n"
            "}\n";

        const char* FragmentShaderSource =
            "#version 130\n"
            "uniform sampler2D u_Texture;\n"
            "uniform vec4 u_Color;\n"
            "in vec2 v_TexCoord;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    FragColor = texture(u_Texture, v_TexCoord) * u_Color;\n"
            "}\n";

        unsigned int CompileShader(unsigned int type, const char* source) {
            unsigned int shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            int success = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
            if (!success) {
                char log[512];
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::fprintf(stderr, "OpenGLRenderer3D: shader compile error: %s\n", log);
            }
            return shader;
        }

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

    void OpenGLRenderer3D::Init() {
        unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, VertexShaderSource);
        unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, FragmentShaderSource);

        m_ShaderProgram = glCreateProgram();
        glAttachShader(m_ShaderProgram, vertexShader);
        glAttachShader(m_ShaderProgram, fragmentShader);
        glLinkProgram(m_ShaderProgram);

        int success = 0;
        glGetProgramiv(m_ShaderProgram, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(m_ShaderProgram, sizeof(log), nullptr, log);
            std::fprintf(stderr, "OpenGLRenderer3D: shader link error: %s\n", log);
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        m_UniformViewProjection = glGetUniformLocation(m_ShaderProgram, "u_ViewProjection");
        m_UniformModel = glGetUniformLocation(m_ShaderProgram, "u_Model");
        m_UniformColor = glGetUniformLocation(m_ShaderProgram, "u_Color");
        m_UniformTexture = glGetUniformLocation(m_ShaderProgram, "u_Texture");
        m_AttribPosition = glGetAttribLocation(m_ShaderProgram, "a_Position");
        m_AttribTexCoord = glGetAttribLocation(m_ShaderProgram, "a_TexCoord");

        for (int i = 0; i < 3; i++) {
            MeshPrimitive primitive = static_cast<MeshPrimitive>(i);
            const std::vector<MeshVertex>& vertices = GetPrimitiveMesh(primitive);

            PrimitiveGpuMesh mesh;
            mesh.VertexCount = static_cast<int>(vertices.size());
            glGenVertexArrays(1, &mesh.Vao);
            glGenBuffers(1, &mesh.Vbo);

            glBindVertexArray(mesh.Vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.Vbo);
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(MeshVertex), vertices.data(), GL_STATIC_DRAW);

            glEnableVertexAttribArray(m_AttribPosition);
            glVertexAttribPointer(m_AttribPosition, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, Position));
            glEnableVertexAttribArray(m_AttribTexCoord);
            glVertexAttribPointer(m_AttribTexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), (void*)offsetof(MeshVertex, TexCoord));

            glBindVertexArray(0);
            m_Meshes[i] = mesh;
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
        for (PrimitiveGpuMesh& mesh : m_Meshes) {
            glDeleteVertexArrays(1, &mesh.Vao);
            glDeleteBuffers(1, &mesh.Vbo);
        }
        glDeleteTextures(1, &m_WhiteTexture);
        for (auto& [path, textureId] : m_TextureCache) {
            unsigned int id = textureId;
            glDeleteTextures(1, &id);
        }
        m_TextureCache.clear();

        glDeleteProgram(m_ShaderProgram);
        m_ShaderProgram = 0;
    }

    void OpenGLRenderer3D::BeginScene(Screen /*screen*/, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor) {
        m_DrawCallCount = 0;

        glEnable(GL_DEPTH_TEST);
        glClearColor(clearColor.r, clearColor.g, clearColor.b, clearColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(fovDegrees), aspectRatio, nearPlane, farPlane);

        // View = inverse of the camera's own world transform (position + rotation, no scale)
        // -- built the exact same T*Rz*Ry*Rx*S order DrawMesh builds a mesh's model matrix
        // with, matching Citro3DRenderer::BeginScene's own approach.
        glm::mat4 cameraWorld = ComposeWorldMtx(cameraPosition, cameraRotationDegrees, glm::vec3(1.0f));
        glm::mat4 view = glm::inverse(cameraWorld);

        m_ViewProjection = projection * view;
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
        glDisable(GL_DEPTH_TEST);
        glUseProgram(0);
    }

    void OpenGLRenderer3D::DrawMesh(MeshPrimitive primitive, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId) {
        m_DrawCallCount++;

        glUseProgram(m_ShaderProgram);

        glm::mat4 model = ComposeWorldMtx(translation, rotationDegrees, scale);
        glUniformMatrix4fv(m_UniformViewProjection, 1, GL_FALSE, &m_ViewProjection[0][0]);
        glUniformMatrix4fv(m_UniformModel, 1, GL_FALSE, &model[0][0]);
        glUniform4f(m_UniformColor, color.r, color.g, color.b, color.a);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureId != 0 ? textureId : m_WhiteTexture);
        glUniform1i(m_UniformTexture, 0);

        const PrimitiveGpuMesh& mesh = m_Meshes[static_cast<int>(primitive)];
        glBindVertexArray(mesh.Vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.VertexCount);
        glBindVertexArray(0);

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    uint32_t OpenGLRenderer3D::LoadTexture(const std::string& path) {
        auto it = m_TextureCache.find(path);
        if (it != m_TextureCache.end())
            return it->second;

        uint32_t texture = GLTextureLoader::LoadTextureFromFile(path);
        m_TextureCache[path] = texture;
        return texture;
    }

}
