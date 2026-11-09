#include "DualityEngine/Renderer/N3DS/Citro3DRenderer.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "DualityEngine/Asset/MeshLoader.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Renderer/LightingMath.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"

// Embedded via dkp_add_embedded_binary_library (DualityEngine/CMakeLists.txt) from
// DualityEngine/Assets3DS/*.v.pica's compiled .shbin -- mesh_shbin/mesh_shbin_size etc. are
// generated symbols (bin2s), not something this project's own source defines.
#include "mesh_shbin.h"
#include "mesh_skin_shbin.h"
#include "mesh_lit_shbin.h"
#include "mesh_skin_lit_shbin.h"

namespace Duality {

    // The PICA attribute loaders read MeshVertex fields contiguously, in declaration order:
    // pos(f3) uv(f2) normal(f3) color(f4) tangent(b4) bones(ub4) weights(f4). Any layout change
    // must keep this order (and 4-byte alignment for float attributes).
    static_assert(offsetof(MeshVertex, TexCoord) == 12 && offsetof(MeshVertex, Normal) == 20 &&
        offsetof(MeshVertex, Color) == 32 && offsetof(MeshVertex, Tangent) == 48 &&
        offsetof(MeshVertex, BoneIndices) == 52 && offsetof(MeshVertex, BoneWeights) == 56 &&
        sizeof(MeshVertex) == 72, "Citro3DRenderer's attribute loaders assume this MeshVertex layout");

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
        u8 ToByte(float v) { return static_cast<u8>(v < 0.0f ? 0 : (v > 1.0f ? 255 : v * 255.0f + 0.5f)); }

        u32 ToC3DColor(const glm::vec4& color) {
            return (ToByte(color.r) << 24) | (ToByte(color.g) << 16) | (ToByte(color.b) << 8) | ToByte(color.a);
        }

        // TEV constant color register: R in the LOWEST byte (0xAABBGGRR) -- the opposite of
        // the render-target clear color above.
        u32 ToTevColor(const glm::vec4& color) {
            return ToByte(color.r) | (ToByte(color.g) << 8) | (ToByte(color.b) << 16) | (static_cast<u32>(ToByte(color.a)) << 24);
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

        // citro3d's LUT builders take a plain function pointer plus one float, but our curves
        // (Renderer/LightingMath.h) need two or three parameters: stage them here. Rendering
        // is single-threaded, and LUTs are only built from Citro3DRenderer on that thread.
        float s_LutA = 0.0f, s_LutB = 0.0f;
        float SpecularLutFunc(float x, float) { return LightingMath::Specular(x, s_LutA); }
        float ToonRampLutFunc(float x, float) { return LightingMath::ToonRamp(x, s_LutA, s_LutB); }
        float ToonSpecularLutFunc(float x, float) { return LightingMath::ToonSpecular(x, s_LutA, s_LutB); }
        float SpotLutFunc(float x, float) { return LightingMath::SpotFactor(x, s_LutA, s_LutB); }
        float DistanceLutFunc(float distance, float range, float) { return LightingMath::DistanceAttenuation(distance / range); }

        enum MaterialLutKind { LutSpecular = 1, LutToonRamp = 2, LutToonSpecular = 3 };

        uint64_t LutKey(int kind, float a, float b) {
            // Quantize so float noise doesn't fragment the cache.
            const uint64_t qa = static_cast<uint64_t>(static_cast<int64_t>(a * 1000.0f) & 0xFFFFFF);
            const uint64_t qb = static_cast<uint64_t>(static_cast<int64_t>(b * 1000.0f) & 0xFFFFFF);
            return (static_cast<uint64_t>(kind) << 56) | (qa << 24) | qb;
        }

        bool UsesFragmentLighting(LightingModel model) {
            return model == LightingModel::Lambert || model == LightingModel::BlinnPhong || model == LightingModel::Toon;
        }
    }

    void Citro3DRenderer::InitProgram(Program& program, const void* shbin, u32 size) {
        program.Dvlb = DVLB_ParseFile((u32*)shbin, size);
        shaderProgramInit(&program.Shader);
        shaderProgramSetVsh(&program.Shader, &program.Dvlb->DVLE[0]);
        shaderInstance_s* vs = program.Shader.vertexShader;
        program.Projection = shaderInstanceGetUniformLocation(vs, "projection");
        program.ModelView = shaderInstanceGetUniformLocation(vs, "modelView");
        program.NormalMatrix = shaderInstanceGetUniformLocation(vs, "normalMatrix");
        program.UvTransform = shaderInstanceGetUniformLocation(vs, "uvTransform");
        program.LightVector = shaderInstanceGetUniformLocation(vs, "lightVec");
        program.LightColor = shaderInstanceGetUniformLocation(vs, "lightColor");
        program.AmbientColor = shaderInstanceGetUniformLocation(vs, "ambientColor");
        program.Bones = shaderInstanceGetUniformLocation(vs, "bones");
        program.BoneIndexScale = shaderInstanceGetUniformLocation(vs, "boneIndexScale");
    }

    void Citro3DRenderer::FreeProgram(Program& program) {
        shaderProgramFree(&program.Shader);
        if (program.Dvlb)
            DVLB_Free(program.Dvlb);
        program.Dvlb = nullptr;
    }

    void Citro3DRenderer::Init() {
        // C3D_Init is NOT called here -- see Citro2DRenderer::Init's comment; owned once by
        // the app entry point since it's process-global state shared with citro2d.
        InitProgram(m_UnlitProgram, mesh_shbin, mesh_shbin_size);
        InitProgram(m_SkinProgram, mesh_skin_shbin, mesh_skin_shbin_size);
        InitProgram(m_LitProgram, mesh_lit_shbin, mesh_lit_shbin_size);
        InitProgram(m_SkinLitProgram, mesh_skin_lit_shbin, mesh_skin_lit_shbin_size);

        // One fragment-lighting environment with all 8 hardware light slots allocated up
        // front; DrawMesh only enables/reconfigures them.
        C3D_LightEnvInit(&m_LightEnv);
        C3D_LightEnvClampHighlights(&m_LightEnv, true); // no specular where N.L <= 0 (desktop matches)
        for (int i = 0; i < MaxLightsPerDraw; ++i) {
            C3D_LightInit(&m_Lights[i], &m_LightEnv);
            C3D_LightEnable(&m_Lights[i], false);
            m_SlotEnabled[i] = false;
            m_SlotLight[i] = -1;
        }

        // No C3D_RenderTargetCreate/SetOutput here -- see SetScreenTargets's own comment.

        for (int i = 0; i < static_cast<int>(MeshPrimitive::Count); i++) {
            MeshPrimitive primitive = static_cast<MeshPrimitive>(i);
            const std::vector<MeshVertex>& vertices = GetPrimitiveMesh(primitive);
            size_t byteSize = vertices.size() * sizeof(MeshVertex);
            void* buffer = linearAlloc(byteSize);
            memcpy(buffer, vertices.data(), byteSize);
            m_Meshes[i] = { buffer, static_cast<int>(vertices.size()) };
        }
    }

    void Citro3DRenderer::Shutdown() {
        C3D_LightEnvBind(nullptr);
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
        m_MaterialLuts.clear();
        m_DistanceLuts.clear();
        m_SpotLuts.clear();

        FreeProgram(m_UnlitProgram);
        FreeProgram(m_SkinProgram);
        FreeProgram(m_LitProgram);
        FreeProgram(m_SkinLitProgram);
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
        m_RenderView = {};
        m_RenderView.CameraPosition = cameraPosition;
        // No IRenderer3D::BeginFrame -- there's no shared cross-screen GPU frame concept
        // exposed at this interface level (that's now the caller's job, see
        // Citro2DRenderer::Init's comment), so BeginScene/EndScene is the natural draw-call
        // reporting granularity for this renderer instead.
        m_DrawCallCount = 0;
        // Light positions are uploaded in view space: a new camera invalidates every slot.
        ++m_ViewSerial;
        // citro2d (other screen / sprite pass) re-binds its own program between our scenes.
        m_BoundProgram = nullptr;

        C3D_RenderTarget* target = TargetFor(screen);
        // Color is only cleared when nothing else will (see this method's own IRenderer3D.h
        // doc comment) -- but the depth buffer always needs a fresh clear regardless, for this
        // renderer's own meshes to depth-test correctly against each other; citro2d's sprite
        // pass never touches depth.
        C3D_RenderTargetClear(target, clear ? C3D_CLEAR_ALL : C3D_CLEAR_DEPTH, ToC3DColor(clearColor), 0);
        C3D_FrameDrawOn(target);

        if (projection == ProjectionType::Perspective) {
            Mtx_PerspTilt(&m_Projection, C3D_AngleFromDegrees(fovDegrees), aspectRatio, nearPlane, farPlane, false);
            m_FlipWinding = false;
        } else {
            // top/bottom swapped from the "normal" Y-up convention -- matches this engine's
            // pixel-space Y-down convention (see Citro2DRenderer/OpenGLRenderer2D's own
            // glOrtho-style top/bottom swap), so a mesh and a sprite at the same world Y land
            // on the same screen row when composited together. The mirror reverses on-screen
            // winding, so back-face culling swaps its CCW/CW sense (m_FlipWinding).
            float halfWidth = orthoHalfHeight * aspectRatio;
            Mtx_OrthoTilt(&m_Projection, -halfWidth, halfWidth, orthoHalfHeight, -orthoHalfHeight, nearPlane, farPlane, false);
            m_FlipWinding = true;
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

    void Citro3DRenderer::BeginScene(const RenderView& view, const glm::vec4& clearColor, bool clear) {
        BeginScene(view.TargetScreen, view.Projection, view.CameraPosition, view.CameraRotationDegrees,
            view.FovDegrees, view.OrthoHalfHeight, view.AspectRatio, view.NearPlane, view.FarPlane, clearColor, clear);
        m_RenderView = view;
    }

    void Citro3DRenderer::EndScene() {
        // Leave the global GPU state the way citro2d expects it: no fragment lighting, no
        // culling/alpha test, plain alpha blending, and TEV stages 1-5 passing through.
        if (m_LightEnvBound) {
            C3D_LightEnvBind(nullptr);
            m_LightEnvBound = false;
        }
        for (int stage = 1; stage < m_UsedTevStages; ++stage)
            C3D_TexEnvInit(C3D_GetTexEnv(stage));
        m_UsedTevStages = 1;
        C3D_CullFace(GPU_CULL_NONE);
        C3D_AlphaTest(false, GPU_ALWAYS, 0);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD,
            GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
        m_BoundProgram = nullptr;
        // citro3d submits draw calls immediately against the target selected by the last
        // C3D_FrameDrawOn -- nothing to flush explicitly here, matching Citro2DRenderer's own
        // EndScene (this bracket exists for a future batched implementation to flush from).
    }

    void Citro3DRenderer::DrawMesh(MeshPrimitive primitive, uint32_t meshHandle, uint32_t subMeshIndex, const glm::vec3& translation, const glm::vec3& rotationDegrees, const glm::vec3& scale, const glm::vec4& color, uint32_t textureId) {
        DrawMesh(MeshDrawCommand{ primitive, meshHandle, subMeshIndex, translation, rotationDegrees, scale, color, textureId, MaterialShadingMode::Unlit });
    }

    C3D_LightLut* Citro3DRenderer::GetMaterialLut(int kind, float a, float b, float /*c*/) {
        const uint64_t key = LutKey(kind, a, b);
        auto found = m_MaterialLuts.find(key);
        if (found != m_MaterialLuts.end())
            return found->second.get();
        auto lut = std::make_unique<C3D_LightLut>();
        s_LutA = a;
        s_LutB = b;
        switch (kind) {
            // N.H LUTs use the absolute-value input range [0,1] (256 entries of precision for
            // tight highlights); N.H is never negative where N.L > 0 and highlights are clamped.
            case LutSpecular:     LightLut_FromFunc(lut.get(), SpecularLutFunc, 0.0f, false); break;
            case LutToonSpecular: LightLut_FromFunc(lut.get(), ToonSpecularLutFunc, 0.0f, false); break;
            // L.N must keep its sign (range [-1,1]) or the dark side would mirror the lit side.
            case LutToonRamp:     LightLut_FromFunc(lut.get(), ToonRampLutFunc, 0.0f, true); break;
            default: break;
        }
        C3D_LightLut* result = lut.get();
        m_MaterialLuts.emplace(key, std::move(lut));
        return result;
    }

    C3D_LightLutDA* Citro3DRenderer::GetDistanceLut(float range) {
        const uint32_t key = static_cast<uint32_t>(range * 100.0f);
        auto found = m_DistanceLuts.find(key);
        if (found != m_DistanceLuts.end())
            return found->second.get();
        auto lut = std::make_unique<C3D_LightLutDA>();
        // Hardware input = clamp((distance - 0) / range): beyond the range it reads the last
        // entry, which DistanceAttenuation forces to 0.
        LightLutDA_Create(lut.get(), DistanceLutFunc, 0.0f, range, range, 0.0f);
        C3D_LightLutDA* result = lut.get();
        m_DistanceLuts.emplace(key, std::move(lut));
        return result;
    }

    C3D_LightLut* Citro3DRenderer::GetSpotLut(float cosOuter, float cosInner) {
        const uint64_t key = LutKey(9, cosOuter, cosInner);
        auto found = m_SpotLuts.find(key);
        if (found != m_SpotLuts.end())
            return found->second.get();
        auto lut = std::make_unique<C3D_LightLut>();
        s_LutA = cosOuter;
        s_LutB = cosInner;
        // Spot input = cos(angle to the spot axis) over [-1,1] (same as LightLut_Spotlight).
        LightLut_FromFunc(lut.get(), SpotLutFunc, 0.0f, true);
        C3D_LightLut* result = lut.get();
        m_SpotLuts.emplace(key, std::move(lut));
        return result;
    }

    void Citro3DRenderer::ConfigureLightSlot(int slot, int sceneLightIndex) {
        if (m_SlotLight[slot] == sceneLightIndex && m_SlotSerial[slot] == m_ViewSerial)
            return;
        m_SlotLight[slot] = sceneLightIndex;
        m_SlotSerial[slot] = m_ViewSerial;
        m_FrameStats.LightSlotUpdates++;
        C3D_Light* light = &m_Lights[slot];

        if (sceneLightIndex < 0) {
            // Placeholder for draws with no lights: the hardware always evaluates >= 1 light,
            // so give it a black one -- primary color = ambient (+ toon shade) only.
            C3D_LightColor(light, 0.0f, 0.0f, 0.0f);
            C3D_FVec direction = FVec4_New(0.0f, 0.0f, 1.0f, 0.0f);
            C3D_LightPosition(light, &direction);
            C3D_LightDistAttnEnable(light, false);
            C3D_LightSpotEnable(light, false);
            return;
        }

        const SceneLight& source = m_RenderView.Lights[static_cast<size_t>(sceneLightIndex)];
        C3D_LightColor(light, source.Color.r, source.Color.g, source.Color.b);
        if (source.Type == LightType::Directional) {
            // w = 0 -> directional; xyz = direction toward the light, in view space.
            C3D_FVec direction = FVec3_Normalize(Mtx_MultiplyFVec3(&m_View, FVec3_New(source.Direction.x, source.Direction.y, source.Direction.z)));
            direction.w = 0.0f;
            C3D_LightPosition(light, &direction);
            C3D_LightDistAttnEnable(light, false);
            C3D_LightSpotEnable(light, false);
            return;
        }

        C3D_FVec position = Mtx_MultiplyFVec4(&m_View, FVec4_New(source.Position.x, source.Position.y, source.Position.z, 1.0f));
        position.w = 1.0f;
        C3D_LightPosition(light, &position);
        C3D_LightDistAttn(light, GetDistanceLut(source.Range));
        if (source.Type == LightType::Spot) {
            C3D_FVec axis = FVec3_Normalize(Mtx_MultiplyFVec3(&m_View, FVec3_New(source.Direction.x, source.Direction.y, source.Direction.z)));
            C3D_LightSpotDir(light, axis.x, axis.y, axis.z);
            C3D_LightSpotLut(light, GetSpotLut(source.CosOuter, source.CosInner));
        } else {
            C3D_LightSpotEnable(light, false);
        }
    }

    void Citro3DRenderer::ConfigureLighting(const MeshDrawCommand& command, const RenderMaterial& material) {
        if (!m_LightEnvBound) {
            C3D_LightEnvBind(&m_LightEnv);
            m_LightEnvBound = true;
        }

        if (m_BoundAmbient != m_RenderView.AmbientColor) {
            C3D_LightEnvAmbient(&m_LightEnv, m_RenderView.AmbientColor.r, m_RenderView.AmbientColor.g, m_RenderView.AmbientColor.b);
            m_BoundAmbient = m_RenderView.AmbientColor;
        }

        // Material colors (citro3d multiplies them with each light's colors):
        //   primary   = emission + ambient*mtl.ambient + sum(light * mtl.diffuse * max(N.L,0) * att)
        //   secondary = sum(light * (mtl.specular0 * D0 + mtl.specular1 * D1) * att)
        // The albedo (texture x vertex color x _Color) is multiplied in by the TEV instead, so
        // a per-object tint never touches the light environment.
        C3D_Material mtl{};
        mtl.ambient[0] = mtl.ambient[1] = mtl.ambient[2] = 1.0f;
        C3D_LightLut* d0 = nullptr;
        C3D_LightLut* d1 = nullptr;
        if (material.Model == LightingModel::Toon) {
            // Banded diffuse lives in specular1 x D1(L.N) so the TEV can add it to the shade.
            mtl.specular1[0] = mtl.specular1[1] = mtl.specular1[2] = 1.0f;
            mtl.specular0[0] = material.SpecColor.r; mtl.specular0[1] = material.SpecColor.g; mtl.specular0[2] = material.SpecColor.b;
            mtl.emission[0] = material.ShadeColor.r; mtl.emission[1] = material.ShadeColor.g; mtl.emission[2] = material.ShadeColor.b;
            d1 = GetMaterialLut(LutToonRamp, material.ToonSteps, material.ToonSmoothness, 0.0f);
            d0 = GetMaterialLut(LutToonSpecular, material.SpecExponent, material.ToonSmoothness, 0.0f);
        } else {
            mtl.diffuse[0] = mtl.diffuse[1] = mtl.diffuse[2] = 1.0f;
            if (material.Model == LightingModel::BlinnPhong) {
                mtl.specular0[0] = material.SpecColor.r; mtl.specular0[1] = material.SpecColor.g; mtl.specular0[2] = material.SpecColor.b;
                d0 = GetMaterialLut(LutSpecular, material.SpecExponent, 0.0f, 0.0f);
            }
        }
        if (!m_HasBoundMaterial || std::memcmp(&mtl, &m_BoundMaterial, sizeof(mtl)) != 0) {
            C3D_LightEnvMaterial(&m_LightEnv, &mtl);
            m_BoundMaterial = mtl;
            m_HasBoundMaterial = true;
        }
        if (d0 != m_BoundD0) {
            C3D_LightEnvLut(&m_LightEnv, GPU_LUT_D0, GPU_LUTINPUT_NH, false, d0);
            m_BoundD0 = d0;
            m_FrameStats.MaterialLutChanges++;
        }
        if (d1 != m_BoundD1) {
            C3D_LightEnvLut(&m_LightEnv, GPU_LUT_D1, GPU_LUTINPUT_LN, true, d1);
            m_BoundD1 = d1;
            m_FrameStats.MaterialLutChanges++;
        }

        // Normal map: hardware bump mapping reads texture unit 1 (UV = texcoord1).
        const bool bump = material.Model != LightingModel::Toon && material.NormalTextureId != 0 &&
            material.NormalTextureId <= m_Textures.size();
        if (bump)
            C3D_TexBind(1, &m_Textures[material.NormalTextureId - 1]);
        if (bump != m_BumpEnabled) {
            C3D_LightEnvBumpMode(&m_LightEnv, bump ? GPU_BUMP_AS_BUMP : GPU_BUMP_NOT_USED);
            C3D_LightEnvBumpSel(&m_LightEnv, 1);
            m_BumpEnabled = bump;
        }

        int lightCount = 0;
        for (uint8_t i = 0; i < command.LightCount && i < MaxLightsPerDraw; ++i) {
            const int index = command.LightIndices[i];
            if (index >= static_cast<int>(m_RenderView.Lights.size()))
                continue;
            ConfigureLightSlot(lightCount++, index);
        }
        if (lightCount == 0)
            ConfigureLightSlot(lightCount++, -1);
        for (int slot = 0; slot < MaxLightsPerDraw; ++slot) {
            const bool enabled = slot < lightCount;
            if (enabled != m_SlotEnabled[slot]) {
                C3D_LightEnable(&m_Lights[slot], enabled);
                m_SlotEnabled[slot] = enabled;
            }
        }
    }

    void Citro3DRenderer::ConfigureTexEnv(const RenderMaterial& material, bool hasTexture, const glm::vec4& tint) {
        int stages = 1;
        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        // Stage 0 = albedo: texture x (vertex color x _Color) -- the vertex shader outputs that
        // product as the primary color. Unlit/Color ignores vertex colors: the tint comes from
        // the TEV constant instead.
        GPU_TEVSRC colorSource = GPU_PRIMARY_COLOR;
        if (!material.UseVertexColor && material.Model == LightingModel::Unlit) {
            colorSource = GPU_CONSTANT;
            C3D_TexEnvColor(env, ToTevColor(tint));
        }
        if (hasTexture) {
            C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, colorSource);
            C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        } else {
            C3D_TexEnvSrc(env, C3D_Both, colorSource);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        }

        if (UsesFragmentLighting(material.Model)) {
            env = C3D_GetTexEnv(1);
            C3D_TexEnvInit(env);
            if (material.Model == LightingModel::Toon) {
                // albedo * min(primary + secondary, 1): shade/ambient + banded light + highlight
                C3D_TexEnvSrc(env, C3D_RGB, GPU_FRAGMENT_PRIMARY_COLOR, GPU_FRAGMENT_SECONDARY_COLOR, GPU_PREVIOUS);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD_MULTIPLY);
            } else {
                // albedo * primary (ambient + diffuse) + secondary (specular, not tinted by albedo)
                C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_FRAGMENT_PRIMARY_COLOR, GPU_FRAGMENT_SECONDARY_COLOR);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
            }
            C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS);
            C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
            stages = 2;
            if (material.Emission.r > 0.0f || material.Emission.g > 0.0f || material.Emission.b > 0.0f) {
                env = C3D_GetTexEnv(2);
                C3D_TexEnvInit(env);
                C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);
                C3D_TexEnvColor(env, ToTevColor(glm::vec4(material.Emission, 1.0f)));
                stages = 3;
            }
        }
        for (int stage = stages; stage < m_UsedTevStages; ++stage)
            C3D_TexEnvInit(C3D_GetTexEnv(stage));
        m_UsedTevStages = stages;
    }

    void Citro3DRenderer::DrawMesh(const MeshDrawCommand& command) {
        m_DrawCallCount++;
        m_FrameStats.DrawCalls++;
        const RenderMaterial material = ResolveCommandMaterial(command);
        const bool lit = UsesFragmentLighting(material.Model);
        const bool skinned = command.SkinMatrices != nullptr && command.SkinMatrixCount > 0 && command.SkinMatrixCount <= 24;

        const Program& program = lit ? (skinned ? m_SkinLitProgram : m_LitProgram) : (skinned ? m_SkinProgram : m_UnlitProgram);
        // Re-bind everything -- citro2d's own draws on the other screen this same frame will
        // have mutated this exact same global C3D state in between (see this class's own
        // header comment). Within one scene the program only changes with the shader family.
        if (m_BoundProgram != &program) {
            C3D_BindProgram(const_cast<shaderProgram_s*>(&program.Shader));
            m_BoundProgram = &program;
            m_FrameStats.ProgramBinds++;
        }

        // Render state. Opaque/Cutout replace (alpha ignored, like the legacy path);
        // Transparent = straight alpha; Additive = SrcAlpha One. Depth writes follow the
        // material; the state is explicitly set per command because Citro2D/Citro3D share
        // global GPU state.
        GPU_BLENDFACTOR srcFactor = GPU_ONE, dstFactor = GPU_ZERO;
        if (material.Surface == SurfaceType::Transparent) { srcFactor = GPU_SRC_ALPHA; dstFactor = GPU_ONE_MINUS_SRC_ALPHA; }
        else if (material.Surface == SurfaceType::Additive) { srcFactor = GPU_SRC_ALPHA; dstFactor = GPU_ONE; }
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, srcFactor, dstFactor, srcFactor, dstFactor);
        C3D_DepthTest(true, GPU_GREATER, material.ZWrite ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
        if (material.Surface == SurfaceType::Cutout)
            C3D_AlphaTest(true, GPU_GEQUAL, static_cast<int>(material.Cutoff * 255.0f + 0.5f));
        else
            C3D_AlphaTest(false, GPU_ALWAYS, 0);
        GPU_CULLMODE cull = GPU_CULL_NONE;
        if (material.Cull == CullMode::Back)
            cull = m_FlipWinding ? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
        else if (material.Cull == CullMode::Front)
            cull = m_FlipWinding ? GPU_CULL_BACK_CCW : GPU_CULL_FRONT_CCW;
        C3D_CullFace(cull);

        // Vertex attributes, loaded contiguously from MeshVertex (see the static_assert above).
        // The tangent (GPU_BYTE x4) is only needed by the lit programs, but a skinned draw
        // must load it anyway to reach the bone data behind it (mesh_skin.v.pica ignores v4).
        C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
        AttrInfo_Init(attrInfo);
        AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0 = position
        AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2); // v1 = texcoord
        AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 3); // v2 = normal
        AttrInfo_AddLoader(attrInfo, 3, GPU_FLOAT, 4); // v3 = imported/baked vertex color
        int loaders = 4;
        int tintRegister = 4;
        if (lit || skinned) {
            AttrInfo_AddLoader(attrInfo, 4, GPU_BYTE, 4); // v4 = tangent (x127)
            loaders = 5;
            tintRegister = 5;
        }
        if (skinned) {
            AttrInfo_AddLoader(attrInfo, 5, GPU_UNSIGNED_BYTE, 4); // v5 = local palette bone ids
            AttrInfo_AddLoader(attrInfo, 6, GPU_FLOAT, 4);         // v6 = influence weights
            loaders = 7;
            tintRegister = 7;
        }
        AttrInfo_AddFixed(attrInfo, tintRegister);                  // material color (_Color)
        C3D_FixedAttribSet(tintRegister, command.Color.r, command.Color.g, command.Color.b, command.Color.a);

        const PrimitiveGpuMesh& mesh = (command.MeshHandle != 0) ? m_ImportedMeshes[command.MeshHandle - 1] : m_Meshes[static_cast<int>(command.Primitive)];
        C3D_BufInfo* bufInfo = C3D_GetBufInfo();
        BufInfo_Init(bufInfo);
        const u64 permutation = loaders == 7 ? 0x6543210 : (loaders == 5 ? 0x43210 : 0x3210);
        BufInfo_Add(bufInfo, mesh.VertexBuffer, sizeof(MeshVertex), loaders, permutation);

        C3D_Mtx model;
        ComposeWorldMtx(&model, command.Translation, command.RotationDegrees, command.Scale);
        C3D_Mtx modelView;
        Mtx_Multiply(&modelView, &m_View, &model);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, program.Projection, &m_Projection);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, program.ModelView, &modelView);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, program.UvTransform, material.MainTexST.x, material.MainTexST.y, material.MainTexST.z, material.MainTexST.w);

        if (lit) {
            // Inverse-transpose of the model-view 3x3: correct normals under non-uniform scale
            // (the same matrix the desktop shader uses, in world instead of view space).
            C3D_Mtx normalMatrix = modelView;
            Mtx_Inverse(&normalMatrix);
            Mtx_Transpose(&normalMatrix);
            C3D_FVUnifMtx3x4(GPU_VERTEX_SHADER, program.NormalMatrix, &normalMatrix);
            ConfigureLighting(command, material);
            m_FrameStats.LitDrawCalls++;
        } else {
            if (m_LightEnvBound) {
                C3D_LightEnvBind(nullptr);
                m_LightEnvBound = false;
            }
            const bool vertexLit = material.Model == LightingModel::VertexLit && m_RenderView.MainLight.Enabled;
            const glm::vec3 ambient = vertexLit ? m_RenderView.AmbientColor : glm::vec3(1.0f);
            const glm::vec3 lightColor = vertexLit ? m_RenderView.MainLight.Color * m_RenderView.MainLight.Intensity : glm::vec3(0.0f);
            // mesh.v.pica evaluates normals after modelView, therefore the directional-light
            // vector must be in view space too.  w=0 makes this a direction: camera translation
            // cannot move it, only camera rotation can.
            glm::vec3 lightDirection(0.0f, 1.0f, 0.0f);
            if (vertexLit) {
                C3D_FVec viewSpaceLight = Mtx_MultiplyFVec3(
                    &m_View, FVec4_New(m_RenderView.MainLight.Direction.x, m_RenderView.MainLight.Direction.y,
                        m_RenderView.MainLight.Direction.z, 0.0f));
                lightDirection = glm::normalize(glm::vec3(viewSpaceLight.x, viewSpaceLight.y, viewSpaceLight.z));
            }
            C3D_FVUnifSet(GPU_VERTEX_SHADER, program.LightVector, lightDirection.x, lightDirection.y, lightDirection.z, 0.0f);
            C3D_FVUnifSet(GPU_VERTEX_SHADER, program.LightColor, lightColor.x, lightColor.y, lightColor.z, 1.0f);
            C3D_FVUnifSet(GPU_VERTEX_SHADER, program.AmbientColor, ambient.x, ambient.y, ambient.z, 1.0f);
        }

        if (skinned) {
            C3D_FVUnifSet(GPU_VERTEX_SHADER, program.BoneIndexScale, 3.0f, 3.0f, 3.0f, 3.0f);
            for (uint32_t bone = 0; bone < command.SkinMatrixCount; ++bone) {
                const glm::mat4& matrix = command.SkinMatrices[bone];
                // GLM is column-major; PICA's dp4 rows need the transposed scalar view.
                for (int row = 0; row < 3; ++row)
                    C3D_FVUnifSet(GPU_VERTEX_SHADER, program.Bones + static_cast<int>(bone * 3 + row),
                        matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]);
            }
        }

        const bool hasTexture = material.UseTexture && command.TextureId != 0 && command.TextureId <= m_Textures.size();
        if (hasTexture)
            C3D_TexBind(0, &m_Textures[command.TextureId - 1]);
        ConfigureTexEnv(material, hasTexture, command.Color);

        // An imported mesh with real submesh ranges draws only that one slice; everything else
        // (procedural primitives, or an imported mesh with no material-group boundaries at all)
        // draws as one whole mesh, exactly like before this feature.
        int first = 0, count = mesh.VertexCount;
        if (command.MeshHandle != 0 && command.SubMeshIndex < mesh.SubMeshes.size()) {
            const MeshData::SubMesh& subMesh = mesh.SubMeshes[command.SubMeshIndex];
            first = static_cast<int>(subMesh.FirstVertex);
            count = static_cast<int>(subMesh.VertexCount);
        }
        C3D_DrawArrays(GPU_TRIANGLES, first, count);
        m_FrameStats.Vertices += static_cast<uint32_t>(count);
        if (lit)
            m_FrameStats.LitVertices += static_cast<uint32_t>(count);
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

    uint32_t Citro3DRenderer::CreateDynamicMesh(const MeshData& data) {
        if (data.Vertices.empty()) return 0;
        const size_t byteSize = data.Vertices.size() * sizeof(MeshVertex);
        void* buffer = linearAlloc(byteSize);
        if (!buffer) return 0;
        memcpy(buffer, data.Vertices.data(), byteSize);
        m_ImportedMeshes.push_back({ buffer, static_cast<int>(data.Vertices.size()), data.SubMeshes });
        return static_cast<uint32_t>(m_ImportedMeshes.size());
    }

    void Citro3DRenderer::UpdateDynamicMesh(uint32_t meshHandle, const MeshData& data) {
        if (meshHandle == 0 || meshHandle > m_ImportedMeshes.size()) return;
        auto& mesh = m_ImportedMeshes[meshHandle - 1];
        if (static_cast<int>(data.Vertices.size()) != mesh.VertexCount) return;
        memcpy(mesh.VertexBuffer, data.Vertices.data(), data.Vertices.size() * sizeof(MeshVertex));
        mesh.SubMeshes = data.SubMeshes;
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
