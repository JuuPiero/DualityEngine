#include "DualityEngine/Renderer/OpenGL/OpenGLRenderer3D.h"

#include <algorithm>
#include <cmath>

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
        //
        // One uber-shader for every built-in shader (Renderer/BuiltinShaders.h), branching on
        // u_LightingModel (uniform, so branches are coherent per draw). Lit/Toon math mirrors
        // the PICA200 fragment-lighting equation Citro3DRenderer configures, term for term:
        //   primary   = clamp(ambient [+ toon shade] + sum(diffuse_i * max(N.L,0) * att_i))
        //   secondary = clamp(sum(lightColor_i * specColor * D0(N.H) * att_i))   (N.L > 0 only)
        //   color     = albedo * primary + secondary + emission         (Lit/*)
        //   color     = albedo * clamp(primary + secondary) + emission  (Toon/Lit)
        // where D0/D1/DA/SP are the functions in Renderer/LightingMath.h that the 3DS samples
        // into its LUTs (dl_* below are their GLSL twins).
        const char* VertexShaderSource =
            "#version 130\n"
            "uniform mat4 u_ViewProjection;\n"
            "uniform mat4 u_Model;\n"
            "uniform mat3 u_NormalMatrix;\n"
            "uniform int u_LightingModel;\n"
            "uniform vec3 u_AmbientColor;\n"
            "uniform vec3 u_LightDirection;\n"
            "uniform vec3 u_LightColor;\n"
            "uniform float u_LightIntensity;\n"
            "uniform int u_MainLightEnabled;\n"
            "uniform mat4 u_ShadowMatrix;\n"
            "uniform int u_Skinned;\n"
            "uniform mat4 u_Bones[24];\n"
            "uniform vec4 u_MainTexST;\n"
            "uniform int u_UseVertexColor;\n"
            "in vec3 a_Position;\n"
            "in vec2 a_TexCoord;\n"
            "in vec3 a_Normal;\n"
            "in vec4 a_VertexColor;\n"
            "in vec4 a_Tangent;\n"
            "in vec4 a_BoneIndices;\n"
            "in vec4 a_BoneWeights;\n"
            "out vec2 v_TexCoord;\n"
            "out vec3 v_Lighting;\n"
            "out vec4 v_VertexColor;\n"
            "out vec4 v_ShadowPosition;\n"
            "out vec3 v_WorldPosition;\n"
            "out vec3 v_Normal;\n"
            "out vec3 v_Tangent;\n"
            "void main() {\n"
            "    vec4 localPosition = vec4(a_Position, 1.0);\n"
            "    vec3 localNormal = a_Normal;\n"
            "    vec3 localTangent = a_Tangent.xyz;\n"
            "    if (u_Skinned != 0) {\n"
            "        mat4 skin = a_BoneWeights.x * u_Bones[int(a_BoneIndices.x)] + a_BoneWeights.y * u_Bones[int(a_BoneIndices.y)] + a_BoneWeights.z * u_Bones[int(a_BoneIndices.z)] + a_BoneWeights.w * u_Bones[int(a_BoneIndices.w)];\n"
            "        float total = a_BoneWeights.x + a_BoneWeights.y + a_BoneWeights.z + a_BoneWeights.w;\n"
            "        skin += (1.0 - total) * mat4(1.0);\n" // unweighted vertices keep their bind pose (matches mesh_skin*.v.pica)
            "        localPosition = skin * localPosition;\n"
            "        localNormal = mat3(skin) * localNormal;\n"
            "        localTangent = mat3(skin) * localTangent;\n"
            "    }\n"
            "    vec4 worldPosition = u_Model * localPosition;\n"
            "    v_WorldPosition = worldPosition.xyz;\n"
            // GLSL 1.30 does not reliably provide inverse(), so the inverse-transpose normal
            // matrix is calculated on the CPU per draw (the 3DS shaders receive the same matrix).
            "    v_Normal = normalize(u_NormalMatrix * localNormal);\n"
            "    v_Tangent = mat3(u_Model) * localTangent;\n"
            "    v_TexCoord = a_TexCoord * u_MainTexST.xy + u_MainTexST.zw;\n"
            "    v_VertexColor = u_UseVertexColor != 0 ? a_VertexColor : vec4(1.0);\n"
            "    v_ShadowPosition = u_ShadowMatrix * worldPosition;\n"
            "    v_Lighting = vec3(1.0);\n"
            "    if (u_LightingModel == 1 && u_MainLightEnabled != 0) {\n" // Mobile/VertexLit (Gouraud), unchanged legacy formula
            "        float diffuse = max(dot(v_Normal, normalize(u_LightDirection)), 0.0);\n"
            "        v_Lighting = u_AmbientColor + u_LightColor * (u_LightIntensity * diffuse);\n"
            "    }\n"
            "    gl_Position = u_ViewProjection * worldPosition;\n"
            "}\n";

        const char* FragmentShaderSource =
            "#version 130\n"
            "uniform sampler2D u_Texture;\n"
            "uniform sampler2D u_NormalMap;\n"
            "uniform int u_HasNormalMap;\n"
            "uniform vec4 u_Color;\n"
            "uniform sampler2DShadow u_ShadowMap;\n"
            "uniform int u_UseShadowMap;\n"
            "uniform int u_LightingModel;\n"
            "uniform int u_AlphaTest;\n"
            "uniform float u_Cutoff;\n"
            "uniform vec3 u_AmbientColor;\n"
            "uniform vec3 u_CameraPosition;\n"
            "uniform int u_LightCount;\n"
            "uniform vec4 u_LightPosition[8];\n"  // xyz: position (point/spot) or direction toward the light (directional); w: 0 dir, 1 point, 2 spot
            "uniform vec3 u_LightColors[8];\n"    // color * intensity, saturated
            "uniform vec4 u_LightSpot[8];\n"      // xyz: spot axis, w: cos(outer half angle)
            "uniform vec2 u_LightParams[8];\n"    // x: range, y: cos(inner half angle)
            "uniform int u_ShadowLightSlot;\n"
            "uniform vec3 u_SpecColor;\n"
            "uniform float u_SpecExponent;\n"
            "uniform vec3 u_Emission;\n"
            "uniform vec3 u_ShadeColor;\n"
            "uniform float u_ToonSteps;\n"
            "uniform float u_ToonSmoothness;\n"
            "in vec2 v_TexCoord;\n"
            "in vec3 v_Lighting;\n"
            "in vec4 v_VertexColor;\n"
            "in vec4 v_ShadowPosition;\n"
            "in vec3 v_WorldPosition;\n"
            "in vec3 v_Normal;\n"
            "in vec3 v_Tangent;\n"
            "out vec4 FragColor;\n"
            // GLSL twins of Renderer/LightingMath.h -- keep in sync.
            "float dl_Attenuation(float t) { t = clamp(t, 0.0, 1.0); float w = clamp((1.0 - t) * 5.0, 0.0, 1.0); return w * w / (1.0 + 25.0 * t * t); }\n"
            "float dl_SpotFactor(float x, float cosOuter, float cosInner) { return smoothstep(cosOuter, max(cosInner, cosOuter + 0.0001), x); }\n"
            "float dl_Specular(float x, float e) { return x <= 0.0 ? 0.0 : pow(x, e); }\n"
            "float dl_ToonRamp(float x, float steps, float smoothness) {\n"
            "    x = clamp(x, 0.0, 1.0); steps = max(1.0, steps);\n"
            "    float scaled = x * steps; float band = floor(scaled);\n"
            "    float w = max(0.0005, clamp(smoothness, 0.0, 1.0) * 0.5);\n"
            "    return clamp((band + smoothstep(0.5 - w, 0.5 + w, scaled - band)) / steps, 0.0, 1.0);\n"
            "}\n"
            "float dl_ToonSpecular(float x, float e, float smoothness) {\n"
            "    float w = max(0.0005, clamp(smoothness, 0.0, 1.0) * 0.5);\n"
            "    return smoothstep(0.5 - w, 0.5 + w, dl_Specular(x, e));\n"
            "}\n"
            "float ShadowFactor() {\n"
            "    float shadow = 1.0;\n"
            "    if (u_UseShadowMap != 0 && v_ShadowPosition.w > 0.0) {\n"
            "        vec3 lightClip = v_ShadowPosition.xyz / v_ShadowPosition.w;\n"
            "        vec3 coord = lightClip * 0.5 + 0.5;\n"
            "        if (coord.x >= 0.0 && coord.x <= 1.0 && coord.y >= 0.0 && coord.y <= 1.0 && coord.z >= 0.0 && coord.z <= 1.0)\n"
            "            shadow = texture(u_ShadowMap, vec3(coord.xy, coord.z - 0.0015));\n"
            "    }\n"
            "    return shadow;\n"
            "}\n"
            "void main() {\n"
            "    vec4 albedo = texture(u_Texture, v_TexCoord) * u_Color * v_VertexColor;\n"
            "    if (u_AlphaTest != 0 && albedo.a < u_Cutoff) discard;\n"
            "    if (u_LightingModel <= 1) {\n"
            // Unlit / Mobile/VertexLit: exactly the pre-shader-system output, including the
            // desktop shadow map darkening the whole surface.
            "        FragColor = albedo * vec4(v_Lighting * mix(0.45, 1.0, ShadowFactor()), 1.0);\n"
            "        return;\n"
            "    }\n"
            "    vec3 N = normalize(v_Normal);\n"
            "    if (!gl_FrontFacing) N = -N;\n" // two-sided (Cull Off) materials light their back side like PICA does with a flipped quaternion
            "    if (u_HasNormalMap != 0) {\n"
            // PICA GPU_BUMP_AS_BUMP with z reconstructed from xy (citro3d default).
            "        vec2 xy = texture(u_NormalMap, v_TexCoord).rg * 2.0 - 1.0;\n"
            "        vec3 T = normalize(v_Tangent - N * dot(N, v_Tangent));\n"
            "        vec3 B = cross(N, T);\n"
            "        N = normalize(T * xy.x + B * xy.y + N * sqrt(max(0.0, 1.0 - dot(xy, xy))));\n"
            "    }\n"
            "    vec3 V = normalize(u_CameraPosition - v_WorldPosition);\n"
            "    vec3 diffuse = vec3(0.0);\n"
            "    vec3 secondary = vec3(0.0);\n"
            "    float shadow = ShadowFactor();\n"
            "    for (int i = 0; i < 8; ++i) {\n"
            "        if (i >= u_LightCount) break;\n"
            "        vec3 L; float att = 1.0;\n"
            "        if (u_LightPosition[i].w < 0.5) {\n"
            "            L = normalize(u_LightPosition[i].xyz);\n"
            "        } else {\n"
            "            vec3 toLight = u_LightPosition[i].xyz - v_WorldPosition;\n"
            "            float dist = length(toLight);\n"
            "            L = toLight / max(dist, 0.0001);\n"
            "            att = dl_Attenuation(dist / max(u_LightParams[i].x, 0.0001));\n"
            "            if (u_LightPosition[i].w > 1.5) att *= dl_SpotFactor(dot(-L, normalize(u_LightSpot[i].xyz)), u_LightSpot[i].w, u_LightParams[i].y);\n"
            "        }\n"
            "        if (i == u_ShadowLightSlot) att *= shadow;\n"
            "        float NL = dot(N, L);\n"
            "        vec3 H = normalize(L + V);\n"
            "        float NH = dot(N, H);\n"
            "        vec3 lightColor = u_LightColors[i] * att;\n"
            "        if (u_LightingModel == 4) {\n"
            "            secondary += lightColor * dl_ToonRamp(NL, u_ToonSteps, u_ToonSmoothness);\n"
            "            if (NL > 0.0) secondary += lightColor * u_SpecColor * dl_ToonSpecular(NH, u_SpecExponent, u_ToonSmoothness);\n"
            "        } else {\n"
            "            diffuse += lightColor * max(NL, 0.0);\n"
            "            if (u_LightingModel == 3 && NL > 0.0) secondary += lightColor * u_SpecColor * dl_Specular(NH, u_SpecExponent);\n"
            "        }\n"
            "    }\n"
            "    vec3 rgb;\n"
            "    if (u_LightingModel == 4) {\n"
            "        vec3 primary = clamp(u_AmbientColor + u_ShadeColor, 0.0, 1.0);\n"
            "        rgb = albedo.rgb * clamp(primary + clamp(secondary, 0.0, 1.0), 0.0, 1.0);\n"
            "    } else {\n"
            "        vec3 primary = clamp(u_AmbientColor + diffuse, 0.0, 1.0);\n"
            "        rgb = albedo.rgb * primary + clamp(secondary, 0.0, 1.0);\n"
            "    }\n"
            "    FragColor = vec4(clamp(rgb + u_Emission, 0.0, 1.0), albedo.a);\n"
            "}\n";

        const char* ShadowVertexShaderSource =
            "#version 130\n"
            "uniform mat4 u_ViewProjection;\n"
            "uniform mat4 u_Model;\n"
            "uniform int u_Skinned;\n"
            "uniform mat4 u_Bones[24];\n"
            "in vec3 a_Position;\n"
            "in vec4 a_BoneIndices;\n"
            "in vec4 a_BoneWeights;\n"
            "void main() {\n"
            "  vec4 position = vec4(a_Position, 1.0);\n"
            "  if (u_Skinned != 0) { mat4 skin = a_BoneWeights.x * u_Bones[int(a_BoneIndices.x)] + a_BoneWeights.y * u_Bones[int(a_BoneIndices.y)] + a_BoneWeights.z * u_Bones[int(a_BoneIndices.z)] + a_BoneWeights.w * u_Bones[int(a_BoneIndices.w)]; position = skin * position; }\n"
            "  gl_Position = u_ViewProjection * u_Model * position;\n"
            "}\n";

        const char* ShadowFragmentShaderSource =
            "#version 130\n"
            "void main() {}\n";

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

        int LightingModelIndex(LightingModel model) {
            switch (model) {
                case LightingModel::VertexLit: return 1;
                case LightingModel::Lambert: return 2;
                case LightingModel::BlinnPhong: return 3;
                case LightingModel::Toon: return 4;
                default: return 0;
            }
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
        mesh.AddFloatAttribute(m_AttribVertexColor, 4, sizeof(MeshVertex), offsetof(MeshVertex, Color));
        mesh.AddNormalizedByteAttribute(m_AttribTangent, 4, sizeof(MeshVertex), offsetof(MeshVertex, Tangent));
        mesh.AddUnsignedByteAttribute(m_AttribBoneIndices, 4, sizeof(MeshVertex), offsetof(MeshVertex, BoneIndices));
        mesh.AddFloatAttribute(m_AttribBoneWeights, 4, sizeof(MeshVertex), offsetof(MeshVertex, BoneWeights));
        mesh.Unbind();
        return mesh;
    }

    void OpenGLRenderer3D::Init() {
        m_Shader.Init(VertexShaderSource, FragmentShaderSource, {
            { "a_Position", 0 }, { "a_TexCoord", 1 }, { "a_Normal", 2 }, { "a_VertexColor", 3 },
            { "a_BoneIndices", 4 }, { "a_BoneWeights", 5 }, { "a_Tangent", 6 }
        });
        m_ShadowShader.Init(ShadowVertexShaderSource, ShadowFragmentShaderSource,
            { { "a_Position", 0 }, { "a_BoneIndices", 4 }, { "a_BoneWeights", 5 } });

        const auto uniform = [this](const char* name) { return m_Shader.GetUniformLocation(name); };
        m_UniformViewProjection = uniform("u_ViewProjection");
        m_UniformModel = uniform("u_Model");
        m_UniformNormalMatrix = uniform("u_NormalMatrix");
        m_UniformColor = uniform("u_Color");
        m_UniformTexture = uniform("u_Texture");
        m_UniformNormalMap = uniform("u_NormalMap");
        m_UniformHasNormalMap = uniform("u_HasNormalMap");
        m_UniformLightingModel = uniform("u_LightingModel");
        m_UniformAmbientColor = uniform("u_AmbientColor");
        m_UniformLightDirection = uniform("u_LightDirection");
        m_UniformLightColor = uniform("u_LightColor");
        m_UniformLightIntensity = uniform("u_LightIntensity");
        m_UniformMainLightEnabled = uniform("u_MainLightEnabled");
        m_UniformShadowMatrix = uniform("u_ShadowMatrix");
        m_UniformShadowMap = uniform("u_ShadowMap");
        m_UniformUseShadowMap = uniform("u_UseShadowMap");
        m_UniformSkinned = uniform("u_Skinned");
        m_UniformBones = uniform("u_Bones[0]");
        m_UniformMainTexST = uniform("u_MainTexST");
        m_UniformUseVertexColor = uniform("u_UseVertexColor");
        m_UniformAlphaTest = uniform("u_AlphaTest");
        m_UniformCutoff = uniform("u_Cutoff");
        m_UniformCameraPosition = uniform("u_CameraPosition");
        m_UniformLightCount = uniform("u_LightCount");
        m_UniformLightPositions = uniform("u_LightPosition[0]");
        m_UniformLightColors = uniform("u_LightColors[0]");
        m_UniformLightSpots = uniform("u_LightSpot[0]");
        m_UniformLightParams = uniform("u_LightParams[0]");
        m_UniformShadowLightSlot = uniform("u_ShadowLightSlot");
        m_UniformSpecColor = uniform("u_SpecColor");
        m_UniformSpecExponent = uniform("u_SpecExponent");
        m_UniformEmission = uniform("u_Emission");
        m_UniformShadeColor = uniform("u_ShadeColor");
        m_UniformToonSteps = uniform("u_ToonSteps");
        m_UniformToonSmoothness = uniform("u_ToonSmoothness");
        m_AttribPosition = m_Shader.GetAttribLocation("a_Position");
        m_AttribTexCoord = m_Shader.GetAttribLocation("a_TexCoord");
        m_AttribNormal = m_Shader.GetAttribLocation("a_Normal");
        m_AttribVertexColor = m_Shader.GetAttribLocation("a_VertexColor");
        m_AttribTangent = m_Shader.GetAttribLocation("a_Tangent");
        m_AttribBoneIndices = m_Shader.GetAttribLocation("a_BoneIndices");
        m_AttribBoneWeights = m_Shader.GetAttribLocation("a_BoneWeights");
        m_ShadowUniformViewProjection = m_ShadowShader.GetUniformLocation("u_ViewProjection");
        m_ShadowUniformModel = m_ShadowShader.GetUniformLocation("u_Model");
        m_ShadowUniformSkinned = m_ShadowShader.GetUniformLocation("u_Skinned");
        m_ShadowUniformBones = m_ShadowShader.GetUniformLocation("u_Bones[0]");

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

        glGenTextures(1, &m_ShadowDepthTexture);
        glBindTexture(GL_TEXTURE_2D, m_ShadowDepthTexture);
        constexpr int ShadowResolution = 512;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, ShadowResolution, ShadowResolution, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glGenFramebuffers(1, &m_ShadowFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_ShadowDepthTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void OpenGLRenderer3D::Shutdown() {
        for (GLVertexArray& mesh : m_Meshes)
            mesh.Shutdown();
        for (GLVertexArray& mesh : m_ImportedMeshes)
            mesh.Shutdown();
        m_ImportedMeshes.clear();
        m_MeshCache.clear();
        glDeleteTextures(1, &m_WhiteTexture);
        glDeleteFramebuffers(1, &m_ShadowFramebuffer);
        glDeleteTextures(1, &m_ShadowDepthTexture);
        for (auto& [path, textureId] : m_TextureCache) {
            unsigned int id = textureId;
            glDeleteTextures(1, &id);
        }
        m_TextureCache.clear();

        m_Shader.Shutdown();
        m_ShadowShader.Shutdown();
    }

    void OpenGLRenderer3D::BeginScene(Screen /*screen*/, ProjectionType projection, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float orthoHalfHeight, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor, bool clear) {
        m_RenderView = {};
        m_RenderView.CameraPosition = cameraPosition;
        m_DrawCallCount = 0;

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        // The 3D Scene view uses this renderer for SpriteRenderer's world-space quad
        // preview too. Without alpha blending, transparent texels write their black RGB
        // values directly into the framebuffer, appearing as rectangular borders around
        // sprites. The Game's 2D renderer uses this same straight-alpha convention.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
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
            m_FlipWinding = false;
        } else {
            // top/bottom swapped from the "normal" Y-up convention -- matches this engine's
            // pixel-space Y-down convention (see OpenGLRenderer2D's own glOrtho top/bottom
            // swap), so a mesh and a sprite at the same world Y land on the same screen row
            // when composited together. The mirror also reverses triangle winding on screen,
            // which back-face culling must compensate for (m_FlipWinding).
            float halfWidth = orthoHalfHeight * aspectRatio;
            projectionMtx = glm::ortho(-halfWidth, halfWidth, orthoHalfHeight, -orthoHalfHeight, nearPlane, farPlane);
            m_FlipWinding = true;
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
        glDisable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        // OpenGLRenderer2D runs after this pass through the compatibility pipeline and expects
        // conventional straight-alpha blending for sprites/UI.
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_Shader.Unbind();
    }

    bool OpenGLRenderer3D::BeginDirectionalShadowMap(const ShadowMapPass& pass) {
        if (m_ShadowFramebuffer == 0 || glCheckFramebufferStatus(GL_FRAMEBUFFER) == 0)
            return false;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_PreviousFramebuffer);
        glGetIntegerv(GL_VIEWPORT, m_PreviousViewport);
        glBindFramebuffer(GL_FRAMEBUFFER, m_ShadowFramebuffer);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_PreviousFramebuffer));
            return false;
        }
        glViewport(0, 0, 512, 512);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glCullFace(GL_FRONT);
        m_ActiveShadowPass = pass;
        return true;
    }

    void OpenGLRenderer3D::DrawDirectionalShadowCaster(const MeshDrawCommand& command) {
        m_ShadowShader.Bind();
        const glm::mat4 model = ComposeWorldMtx(command.Translation, command.RotationDegrees, command.Scale);
        m_ShadowShader.SetUniformMat4(m_ShadowUniformViewProjection, m_ActiveShadowPass.ViewProjection);
        m_ShadowShader.SetUniformMat4(m_ShadowUniformModel, model);
        const bool skinned = command.SkinMatrices != nullptr && command.SkinMatrixCount > 0 && command.SkinMatrixCount <= 24;
        glUniform1i(m_ShadowUniformSkinned, skinned ? 1 : 0);
        if (skinned)
            glUniformMatrix4fv(m_ShadowUniformBones, static_cast<GLsizei>(command.SkinMatrixCount), GL_FALSE, &command.SkinMatrices[0][0][0]);
        const GLVertexArray& mesh = (command.MeshHandle != 0) ? m_ImportedMeshes[command.MeshHandle - 1] : m_Meshes[static_cast<int>(command.Primitive)];
        mesh.Bind();
        const std::vector<MeshData::SubMesh>& subMeshes = mesh.GetSubMeshes();
        if (command.MeshHandle != 0 && command.SubMeshIndex < subMeshes.size()) {
            const MeshData::SubMesh& subMesh = subMeshes[command.SubMeshIndex];
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(subMesh.FirstVertex), static_cast<GLsizei>(subMesh.VertexCount));
        } else {
            glDrawArrays(GL_TRIANGLES, 0, mesh.GetVertexCount());
        }
        mesh.Unbind();
    }

    void OpenGLRenderer3D::EndDirectionalShadowMap() {
        glCullFace(GL_BACK);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_PreviousFramebuffer));
        glViewport(m_PreviousViewport[0], m_PreviousViewport[1], m_PreviousViewport[2], m_PreviousViewport[3]);
        m_Shader.Unbind();
    }

    void OpenGLRenderer3D::SetDepthWriteEnabled(bool enabled) {
        glDepthMask(enabled ? GL_TRUE : GL_FALSE);
    }

    void OpenGLRenderer3D::DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, uint32_t subMeshIndex, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId) {
        DrawMesh(MeshDrawCommand{ primitive, meshHandle, subMeshIndex, translation, rotationDegrees, scale, color, textureId, MaterialShadingMode::Unlit });
    }

    void OpenGLRenderer3D::ApplyRenderState(const RenderMaterial& material) {
        // Blend: Opaque/Cutout replace (alpha ignored, as the legacy opaque path did),
        // Transparent = straight alpha, Additive = SrcAlpha One.
        switch (material.Surface) {
            case SurfaceType::Transparent: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
            case SurfaceType::Additive:    glBlendFunc(GL_SRC_ALPHA, GL_ONE); break;
            default:                       glBlendFunc(GL_ONE, GL_ZERO); break;
        }
        glDepthMask(material.ZWrite ? GL_TRUE : GL_FALSE);
        if (material.Cull == CullMode::Off) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glFrontFace(m_FlipWinding ? GL_CW : GL_CCW);
            glCullFace(material.Cull == CullMode::Front ? GL_FRONT : GL_BACK);
        }
    }

    void OpenGLRenderer3D::DrawMesh(const MeshDrawCommand& command) {
        m_DrawCallCount++;
        const RenderMaterial material = ResolveCommandMaterial(command);

        // Blob shadows (legacy commands) are translucent, depth-read-only overlays; ordinary
        // materials restore their own state on every draw.
        ApplyRenderState(material);
        m_Shader.Bind();

        glm::mat4 model = ComposeWorldMtx(command.Translation, command.RotationDegrees, command.Scale);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(model)));
        m_Shader.SetUniformMat4(m_UniformViewProjection, m_ViewProjection);
        m_Shader.SetUniformMat4(m_UniformModel, model);
        glUniformMatrix3fv(m_UniformNormalMatrix, 1, GL_FALSE, &normalMatrix[0][0]);
        m_Shader.SetUniformVec4(m_UniformColor, command.Color);
        glUniform1i(m_UniformLightingModel, LightingModelIndex(material.Model));
        glUniform1i(m_UniformMainLightEnabled, m_RenderView.MainLight.Enabled ? 1 : 0);
        // These shader uniforms are GLSL vec3s. Calling glUniform4f (the vec4 helper) for
        // them is GL_INVALID_OPERATION; the uniforms then retain their all-zero defaults and
        // every VertexLit material is multiplied to black. Keep the upload type exact.
        m_Shader.SetUniformVec3(m_UniformAmbientColor, m_RenderView.AmbientColor);
        m_Shader.SetUniformVec3(m_UniformLightDirection, m_RenderView.MainLight.Direction);
        m_Shader.SetUniformVec3(m_UniformLightColor, m_RenderView.MainLight.Color);
        glUniform1f(m_UniformLightIntensity, m_RenderView.MainLight.Intensity);
        m_Shader.SetUniformMat4(m_UniformShadowMatrix, m_RenderView.ShadowMap.ViewProjection);
        glUniform1i(m_UniformUseShadowMap, m_RenderView.HasShadowMap ? 1 : 0);
        m_Shader.SetUniformVec4(m_UniformMainTexST, material.MainTexST);
        glUniform1i(m_UniformUseVertexColor, material.UseVertexColor ? 1 : 0);
        glUniform1i(m_UniformAlphaTest, material.Surface == SurfaceType::Cutout ? 1 : 0);
        glUniform1f(m_UniformCutoff, material.Cutoff);
        const bool skinned = command.SkinMatrices != nullptr && command.SkinMatrixCount > 0 && command.SkinMatrixCount <= 24;
        glUniform1i(m_UniformSkinned, skinned ? 1 : 0);
        if (skinned)
            glUniformMatrix4fv(m_UniformBones, static_cast<GLsizei>(command.SkinMatrixCount), GL_FALSE, &command.SkinMatrices[0][0][0]);

        // Per-pixel lights (Lit/*, Toon/Lit): the same <= 8 lights SceneRenderer picked for
        // this object, so desktop and 3DS light every object with the same set.
        const bool perPixel = material.Model == LightingModel::Lambert || material.Model == LightingModel::BlinnPhong || material.Model == LightingModel::Toon;
        int lightCount = 0;
        int shadowSlot = -1;
        if (perPixel) {
            glm::vec4 positions[MaxLightsPerDraw];
            glm::vec3 colors[MaxLightsPerDraw];
            glm::vec4 spots[MaxLightsPerDraw];
            glm::vec2 params[MaxLightsPerDraw];
            for (uint8_t i = 0; i < command.LightCount && i < MaxLightsPerDraw; ++i) {
                if (command.LightIndices[i] >= m_RenderView.Lights.size())
                    continue;
                const SceneLight& light = m_RenderView.Lights[command.LightIndices[i]];
                const float type = light.Type == LightType::Directional ? 0.0f : (light.Type == LightType::Point ? 1.0f : 2.0f);
                positions[lightCount] = glm::vec4(light.Type == LightType::Directional ? light.Direction : light.Position, type);
                colors[lightCount] = light.Color;
                spots[lightCount] = glm::vec4(light.Direction, light.CosOuter);
                params[lightCount] = glm::vec2(light.Range, light.CosInner);
                if (light.CastShadows && static_cast<int>(command.LightIndices[i]) == m_RenderView.MainLightIndex)
                    shadowSlot = lightCount;
                ++lightCount;
            }
            if (lightCount > 0) {
                glUniform4fv(m_UniformLightPositions, lightCount, &positions[0].x);
                glUniform3fv(m_UniformLightColors, lightCount, &colors[0].x);
                glUniform4fv(m_UniformLightSpots, lightCount, &spots[0].x);
                glUniform2fv(m_UniformLightParams, lightCount, &params[0].x);
            }
            m_Shader.SetUniformVec3(m_UniformCameraPosition, m_RenderView.CameraPosition);
            m_Shader.SetUniformVec3(m_UniformSpecColor, material.SpecColor);
            glUniform1f(m_UniformSpecExponent, material.SpecExponent);
            m_Shader.SetUniformVec3(m_UniformShadeColor, material.ShadeColor);
            glUniform1f(m_UniformToonSteps, material.ToonSteps);
            glUniform1f(m_UniformToonSmoothness, material.ToonSmoothness);
        }
        glUniform1i(m_UniformLightCount, lightCount);
        glUniform1i(m_UniformShadowLightSlot, shadowSlot);
        m_Shader.SetUniformVec3(m_UniformEmission, perPixel ? material.Emission : glm::vec3(0.0f));

        const bool useTexture = material.UseTexture && command.TextureId != 0;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, useTexture ? command.TextureId : m_WhiteTexture);
        m_Shader.SetUniformInt(m_UniformTexture, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_ShadowDepthTexture);
        m_Shader.SetUniformInt(m_UniformShadowMap, 1);
        const bool normalMapped = perPixel && material.Model != LightingModel::Toon && material.NormalTextureId != 0;
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, normalMapped ? material.NormalTextureId : m_WhiteTexture);
        m_Shader.SetUniformInt(m_UniformNormalMap, 2);
        glUniform1i(m_UniformHasNormalMap, normalMapped ? 1 : 0);
        glActiveTexture(GL_TEXTURE0);

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
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
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

        // Honour the texture's import settings (Wrap Mode Repeat for tiled materials, filter,
        // mipmaps) exactly like Citro3DRenderer does on the 3DS -- this used to load every mesh
        // texture with clamp/bilinear defaults, so _MainTex tiling only worked on-device.
        uint32_t texture = GLTextureLoader::LoadTextureFromFile(path, TextureImportSettings::Load(path));
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

    uint32_t OpenGLRenderer3D::CreateDynamicMesh(const MeshData& data) {
        if (data.Vertices.empty()) return 0;
        m_ImportedMeshes.push_back(UploadGpuMesh(data.Vertices));
        m_ImportedMeshes.back().SetSubMeshes(data.SubMeshes);
        return static_cast<uint32_t>(m_ImportedMeshes.size());
    }

    void OpenGLRenderer3D::UpdateDynamicMesh(uint32_t meshHandle, const MeshData& data) {
        if (meshHandle == 0 || meshHandle > m_ImportedMeshes.size()) return;
        GLVertexArray& mesh = m_ImportedMeshes[meshHandle - 1];
        mesh.Bind();
        mesh.SetVertexData(data.Vertices.data(), data.Vertices.size() * sizeof(MeshVertex), static_cast<int>(data.Vertices.size()));
        mesh.SetSubMeshes(data.SubMeshes);
        mesh.Unbind();
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
