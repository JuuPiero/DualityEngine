#include "DualityEngine/Renderer/N3DS/Citro3DRenderer.h"

#include <cstdio>
#include <cstring>

#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"

// Embedded via dkp_add_embedded_binary_library (DualityEngine/CMakeLists.txt) from
// DualityEngine/Assets3DS/mesh.v.pica's compiled .shbin -- mesh_shbin/mesh_shbin_size are
// generated symbols (bin2s), not something this project's own source defines.
#include "mesh_shbin.h"

namespace Duality {

    namespace {
        // Raw RGBA8 packing for C3D_RenderTargetClear's own clearColor parameter, kept local
        // so this file has no citro2d dependency at all -- the user explicitly asked for this
        // renderer to use citro3d directly, not layered through citro2d.
        //
        // (R<<24)|(G<<16)|(B<<8)|A -- NOT citro2d's own C2D_Color32(r,g,b,a) convention
        // (r|(g<<8)|(b<<16)|(a<<24), R in the lowest byte), which is the opposite order and
        // was a real, confirmed-on-device bug here before this fix (a dark maroon clearColor
        // rendered as bright saturated red). Confirmed against every devkitPro gpu/ example's
        // own `#define CLEAR_COLOR 0x68B0D8FF` -- 0x68/0xB0/0xD8 is a light sky blue only under
        // this (R highest byte, A lowest byte) interpretation.
        u32 ToC3DColor(const glm::vec4& color) {
            auto toByte = [](float v) { return static_cast<u8>(v < 0.0f ? 0 : (v > 1.0f ? 255 : v * 255.0f + 0.5f)); };
            return (toByte(color.r) << 24) | (toByte(color.g) << 16) | (toByte(color.b) << 8) | toByte(color.a);
        }

        // T * Rz * Ry * Rx * S, matching this engine's TransformComponent convention (scale
        // applied innermost, then X/Y/Z rotation in that nested order, then translation
        // outermost) -- the exact same composition order both this function and
        // OpenGLRenderer3D's GLM equivalent must use, or a multi-axis rotation would look
        // different across platforms. Mtx_RotateX/Y/Z take plain radians (confirmed against
        // References/devkitpro-3ds-templates/graphics/gpu/textured_cube's own frame-by-frame
        // angle accumulation in M_PI/180 units, not citro3d's separate 0..1 "turns" angle
        // format used only by Mtx_PerspTilt's fovy parameter).
        void ComposeWorldMtx(C3D_Mtx* out, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale) {
            Mtx_Identity(out);
            Mtx_Translate(out, translation.x, translation.y, translation.z, true);
            Mtx_RotateZ(out, glm::radians(rotationDegrees.z), true);
            Mtx_RotateY(out, glm::radians(rotationDegrees.y), true);
            Mtx_RotateX(out, glm::radians(rotationDegrees.x), true);
            Mtx_Scale(out, scale.x, scale.y, scale.z);
        }
    }

    void Citro3DRenderer::Init() {
        // C3D_Init is NOT called here -- see Citro2DRenderer::Init's comment; owned once by
        // the app entry point since it's process-global state shared with citro2d.
        m_ShaderDvlb = DVLB_ParseFile((u32*)mesh_shbin, mesh_shbin_size);
        shaderProgramInit(&m_ShaderProgram);
        shaderProgramSetVsh(&m_ShaderProgram, &m_ShaderDvlb->DVLE[0]);

        m_UniformProjection = shaderInstanceGetUniformLocation(m_ShaderProgram.vertexShader, "projection");
        m_UniformModelView = shaderInstanceGetUniformLocation(m_ShaderProgram.vertexShader, "modelView");

        // No C3D_RenderTargetCreate/SetOutput here -- see SetScreenTargets's own comment.

        for (int i = 0; i < 3; i++) {
            MeshPrimitive primitive = static_cast<MeshPrimitive>(i);
            const std::vector<MeshVertex>& vertices = GetPrimitiveMesh(primitive);
            size_t byteSize = vertices.size() * sizeof(MeshVertex);
            void* buffer = linearAlloc(byteSize);
            memcpy(buffer, vertices.data(), byteSize);
            m_Meshes[i] = { buffer, static_cast<int>(vertices.size()) };
        }
    }

    void Citro3DRenderer::Shutdown() {
        for (auto& mesh : m_Meshes) {
            if (mesh.VertexBuffer)
                linearFree(mesh.VertexBuffer);
        }
        for (auto& mesh : m_ImportedMeshes) {
            if (mesh.VertexBuffer)
                linearFree(mesh.VertexBuffer);
        }
        m_ImportedMeshes.clear();
        m_MeshCache.clear();
        for (C3D_Tex& tex : m_Textures)
            C3D_TexDelete(&tex);
        m_Textures.clear();
        m_TextureCache.clear();

        shaderProgramFree(&m_ShaderProgram);
        DVLB_Free(m_ShaderDvlb);
        m_ShaderDvlb = nullptr;
        // C3D_Fini is the caller's responsibility, see Init()'s comment.
    }

    void Citro3DRenderer::SetScreenTargets(C3D_RenderTarget* top, C3D_RenderTarget* bottom) {
        m_TopTarget = top;
        m_BottomTarget = bottom;
    }

    C3D_RenderTarget* Citro3DRenderer::TargetFor(Screen screen) const {
        return screen == Screen::Top ? m_TopTarget : m_BottomTarget;
    }

    void Citro3DRenderer::BeginScene(Screen screen, ProjectionType projection, const glm::vec3& cameraPosition, const glm::vec3& cameraRotationDegrees, float fovDegrees, float orthoHalfHeight, float aspectRatio, float nearPlane, float farPlane, const glm::vec4& clearColor, bool clear) {
        // No IRenderer3D::BeginFrame -- there's no shared cross-screen GPU frame concept
        // exposed at this interface level (that's now the caller's job, see
        // Citro2DRenderer::Init's comment), so BeginScene/EndScene is the natural draw-call
        // reporting granularity for this renderer instead.
        m_DrawCallCount = 0;

        C3D_RenderTarget* target = TargetFor(screen);
        // Color is only cleared when nothing else will (see this method's own IRenderer3D.h
        // doc comment) -- but the depth buffer always needs a fresh clear regardless, for this
        // renderer's own meshes to depth-test correctly against each other; citro2d's sprite
        // pass never touches depth.
        C3D_RenderTargetClear(target, clear ? C3D_CLEAR_ALL : C3D_CLEAR_DEPTH, ToC3DColor(clearColor), 0);
        C3D_FrameDrawOn(target);

        if (projection == ProjectionType::Perspective) {
            Mtx_PerspTilt(&m_Projection, C3D_AngleFromDegrees(fovDegrees), aspectRatio, nearPlane, farPlane, false);
        } else {
            // top/bottom swapped from the "normal" Y-up convention -- matches this engine's
            // pixel-space Y-down convention (see Citro2DRenderer/OpenGLRenderer2D's own
            // glOrtho-style top/bottom swap), so a mesh and a sprite at the same world Y land
            // on the same screen row when composited together.
            float halfWidth = orthoHalfHeight * aspectRatio;
            Mtx_OrthoTilt(&m_Projection, -halfWidth, halfWidth, orthoHalfHeight, -orthoHalfHeight, nearPlane, farPlane, false);
        }

        // View = inverse of the camera's own world transform (position + rotation, no
        // scale) -- built the exact same way DrawMesh builds a mesh's model matrix.
        C3D_Mtx cameraWorld;
        ComposeWorldMtx(&cameraWorld, cameraPosition, cameraRotationDegrees, { 1.0f, 1.0f, 1.0f });
        Mtx_Inverse(&cameraWorld);
        m_View = cameraWorld;

        // GPU_GREATER (not the OpenGL-typical GPU_LESS) -- PICA200's own reversed depth-range
        // convention, confirmed against devkitPro's own composite_scene example. Disabled again
        // in EndScene so citro2d's sprite pass (which never writes/tests depth) isn't affected.
        C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    }

    void Citro3DRenderer::EndScene() {
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
        // citro3d submits draw calls immediately against the target selected by the last
        // C3D_FrameDrawOn -- nothing to flush explicitly here, matching Citro2DRenderer's own
        // EndScene (this bracket exists for a future batched implementation to flush from).
    }

    void Citro3DRenderer::DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, uint32_t subMeshIndex, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId) {
        m_DrawCallCount++;

        // Re-bind everything -- citro2d's own draws on the other screen this same frame will
        // have mutated this exact same global C3D state in between (see this class's own
        // header comment).
        C3D_BindProgram(&m_ShaderProgram);

        C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
        AttrInfo_Init(attrInfo);
        AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0 = position
        AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2); // v1 = texcoord
        AttrInfo_AddFixed(attrInfo, 2);                // v2 = color (flat, not loaded from the buffer)
        C3D_FixedAttribSet(2, color.r, color.g, color.b, color.a);

        const PrimitiveGpuMesh& mesh = (meshHandle != 0) ? m_ImportedMeshes[meshHandle - 1] : m_Meshes[static_cast<int>(primitive)];
        C3D_BufInfo* bufInfo = C3D_GetBufInfo();
        BufInfo_Init(bufInfo);
        BufInfo_Add(bufInfo, mesh.VertexBuffer, sizeof(MeshVertex), 2, 0x10);

        if (textureId != 0) {
            C3D_TexBind(0, &m_Textures[textureId - 1]);
            C3D_TexEnv* env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        } else {
            C3D_TexEnv* env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        }

        C3D_Mtx model;
        ComposeWorldMtx(&model, translation, rotationDegrees, scale);
        C3D_Mtx modelView;
        Mtx_Multiply(&modelView, &m_View, &model);

        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, m_UniformProjection, &m_Projection);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, m_UniformModelView, &modelView);

        // An imported mesh with real submesh ranges draws only that one slice; everything else
        // (procedural primitives, or an imported mesh with no material-group boundaries at all)
        // draws as one whole mesh, exactly like before this feature.
        if (meshHandle != 0 && subMeshIndex < mesh.SubMeshes.size()) {
            const MeshData::SubMesh& subMesh = mesh.SubMeshes[subMeshIndex];
            C3D_DrawArrays(GPU_TRIANGLES, static_cast<int>(subMesh.FirstVertex), static_cast<int>(subMesh.VertexCount));
        } else {
            C3D_DrawArrays(GPU_TRIANGLES, 0, mesh.VertexCount);
        }
    }

    uint32_t Citro3DRenderer::GetSubMeshCount(uint32_t meshHandle) const {
        if (meshHandle == 0)
            return 1; // procedural primitive -- always one whole-mesh draw
        const std::vector<MeshData::SubMesh>& subMeshes = m_ImportedMeshes[meshHandle - 1].SubMeshes;
        return subMeshes.empty() ? 1 : static_cast<uint32_t>(subMeshes.size());
    }

    uint32_t Citro3DRenderer::LoadTexture(const std::string& path) {
        auto it = m_TextureCache.find(path);
        if (it != m_TextureCache.end())
            return it->second;

        uint32_t textureId = 0;

        FILE* file = fopen(path.c_str(), "rb");
        if (file) {
            fseek(file, 0, SEEK_END);
            long size = ftell(file);
            fseek(file, 0, SEEK_SET);
            std::vector<u8> data(static_cast<size_t>(size));
            fread(data.data(), 1, data.size(), file);
            fclose(file);

            C3D_Tex tex{};
            Tex3DS_Texture t3x = Tex3DS_TextureImport(data.data(), data.size(), &tex, nullptr, false);
            if (t3x) {
                Tex3DS_TextureFree(t3x);

                // The cooked .t3x has mip levels only when its source texture
                // requested GenerateMipmaps in the Editor. Match the sampler
                // settings used by Citro2DRenderer: mip-filtering is what stops
                // distant, minified 3D surfaces from shimmering as the camera
                // moves, rather than merely making the base level bilinear.
                TextureImportSettings settings = TextureImportSettings::Load(path);
                GPU_TEXTURE_FILTER_PARAM filter = settings.FilterMode == TextureFilterMode::Point ? GPU_NEAREST : GPU_LINEAR;
                GPU_TEXTURE_WRAP_PARAM wrap = settings.WrapMode == TextureWrapMode::Repeat ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
                C3D_TexSetFilter(&tex, filter, filter);
                C3D_TexSetFilterMipmap(&tex, settings.GenerateMipmaps ? filter : GPU_NEAREST);
                C3D_TexSetWrap(&tex, wrap, wrap);
                m_Textures.push_back(tex);
                textureId = static_cast<uint32_t>(m_Textures.size()); // 1-based, 0 reserved for "none"
            }
        }

        m_TextureCache[path] = textureId; // cache failures too, matching every other LoadTexture in this codebase
        return textureId;
    }

    uint32_t Citro3DRenderer::LoadMesh(const std::string& path) {
        auto it = m_MeshCache.find(path);
        if (it != m_MeshCache.end())
            return it->second;

        uint32_t meshHandle = 0;
        const MeshData& data = MeshLoader::Load(path);
        if (!data.Vertices.empty()) {
            size_t byteSize = data.Vertices.size() * sizeof(MeshVertex);
            void* buffer = linearAlloc(byteSize);
            memcpy(buffer, data.Vertices.data(), byteSize);
            m_ImportedMeshes.push_back({ buffer, static_cast<int>(data.Vertices.size()), data.SubMeshes });
            meshHandle = static_cast<uint32_t>(m_ImportedMeshes.size()); // 1-based, 0 reserved for "none"
        }

        m_MeshCache[path] = meshHandle; // cache failures too, matching LoadTexture above
        return meshHandle;
    }

    void Citro3DRenderer::UnloadAllTextures() {
        for (C3D_Tex& tex : m_Textures)
            C3D_TexDelete(&tex);
        m_Textures.clear();
        m_TextureCache.clear();
    }

    void Citro3DRenderer::UnloadAllMeshes() {
        // m_Meshes[3] (the built-in procedural primitives) is untouched -- only
        // m_ImportedMeshes (LoadMesh's own linearAlloc'd uploads) is ever freed here.
        for (auto& mesh : m_ImportedMeshes) {
            if (mesh.VertexBuffer)
                linearFree(mesh.VertexBuffer);
        }
        m_ImportedMeshes.clear();
        m_MeshCache.clear();
    }

}
