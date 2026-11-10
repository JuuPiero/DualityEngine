// Material/shader system + scene lighting: .mat format v2 round trip, migration of pre-shader
// (v1) materials without changing their look, render-state rules, per-scene LightingSettings,
// Point/Spot light components, per-object light selection (the PICA200 8-light cap), render-
// queue ordering of the shared mesh pass, the shared lighting curves the 3DS samples into LUTs,
// and the tangents generated for normal mapping.
#include "TestFramework.h"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Asset/AssetDatabase.h"
#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"
#include "DualityEngine/Renderer/LightingMath.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"

using namespace Duality;

namespace {
    class RecordingRenderer final : public IRenderer3D {
    public:
        void Init() override {}
        void Shutdown() override {}
        void BeginScene(Screen, ProjectionType, const glm::vec3&, const glm::vec3&, float, float, float, float, float, const glm::vec4&, bool) override {}
        void BeginScene(const RenderView& view, const glm::vec4&, bool) override { View = view; }
        void EndScene() override {}
        void DrawMesh(MeshPrimitive, uint32_t, uint32_t, const glm::vec3&, const glm::vec3&, const glm::vec3&, const glm::vec4&, uint32_t) override {}
        void DrawMesh(const MeshDrawCommand& command) override { Commands.push_back(command); }
        uint32_t GetSubMeshCount(uint32_t) const override { return 1; }
        uint32_t LoadTexture(const std::string&) override { return 7; }
        uint32_t LoadMesh(const std::string&) override { return 0; }
        void UnloadAllTextures() override {}
        void UnloadAllMeshes() override {}
        uint32_t GetDrawCallCount() const override { return static_cast<uint32_t>(Commands.size()); }

        RenderView View;
        std::vector<MeshDrawCommand> Commands;
    };

    std::string TempPath(const char* name) {
        return (std::filesystem::temp_directory_path() / name).string();
    }

    void WriteText(const std::string& path, const std::string& text) {
        std::ofstream file(path);
        file << text;
    }

    // Registers a material file under a fake guid so MeshRenderer slots can reference it.
    std::string RegisterMaterial(const char* fileName, const Material& material) {
        const std::string path = TempPath(fileName);
        MaterialLoader::Save(path, material);
        const std::string guid = std::string("test-material-") + fileName;
        AssetDatabase::Register(guid, path);
        return guid;
    }

    Entity AddCube(Scene& scene, const char* name, const glm::vec3& position, const std::string& materialGuid) {
        Entity entity = scene.CreateEntity(name);
        auto& mesh = entity.AddComponent<MeshRendererComponent>();
        entity.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Cube);
        if (!materialGuid.empty())
            mesh.Materials.push_back(AssetRef{ materialGuid });
        entity.GetComponent<TransformComponent>().Translation = position;
        return entity;
    }

    Entity AddPerspectiveCamera(Scene& scene) {
        Entity camera = scene.CreateEntity("Camera");
        auto& component = camera.AddComponent<CameraComponent>();
        component.Screen = Screen::Top;
        component.Primary = true;
        component.Projection = ProjectionType::Perspective;
        component.FovDegrees = 60.0f;
        component.NearPlane = 0.1f;
        component.FarPlane = 100.0f;
        return camera;
    }
}

TEST_CASE("Material v2 round-trips shader, render state and every property") {
    const std::string path = TempPath("duality_material_v2.mat");
    std::filesystem::remove(path);
    MaterialLoader::ClearCache();

    Material original;
    original.SetShader(BuiltinShader::LitSpecular);
    original.Color = { 0.2f, 0.4f, 0.6f, 0.8f };
    original.Texture.Guid = "albedo";
    original.NormalMap.Guid = "normals";
    original.MainTexST = { 2.0f, 3.0f, 0.25f, 0.5f };
    original.Surface = SurfaceType::Cutout;
    original.Cull = CullMode::Front;
    original.ZWrite = ZWriteMode::Off;
    original.RenderQueue = 2600;
    original.Cutoff = 0.3f;
    original.SpecColor = { 0.9f, 0.8f, 0.7f, 1.0f };
    original.Shininess = 0.75f;
    original.EmissionColor = { 0.1f, 0.0f, 0.2f, 1.0f };
    original.ShadeColor = { 0.3f, 0.1f, 0.4f, 1.0f };
    original.ToonSteps = 3;
    original.ToonSmoothness = 0.2f;
    CHECK(MaterialLoader::Save(path, original));

    MaterialLoader::ClearCache();
    Material loaded = MaterialLoader::Load(path);
    CHECK_SOFT(loaded.Shader == BuiltinShader::LitSpecular, "shader survives by name");
    CHECK_SOFT(loaded.ShadingMode == MaterialShadingMode::VertexLit, "legacy ShadingMode mirror says 'lit'");
    CHECK_SOFT(loaded.Color == original.Color && loaded.Texture.Guid == "albedo" && loaded.NormalMap.Guid == "normals", "color and texture slots survive");
    CHECK_SOFT(loaded.MainTexST == original.MainTexST, "tiling/offset survive");
    CHECK_SOFT(loaded.Surface == SurfaceType::Cutout && loaded.Cull == CullMode::Front && loaded.ZWrite == ZWriteMode::Off && loaded.RenderQueue == 2600, "render state survives");
    CHECK_SOFT(std::abs(loaded.Cutoff - 0.3f) < 1e-6f && std::abs(loaded.Shininess - 0.75f) < 1e-6f, "scalar properties survive");
    CHECK_SOFT(loaded.SpecColor == original.SpecColor && loaded.EmissionColor == original.EmissionColor && loaded.ShadeColor == original.ShadeColor, "color properties survive");
    CHECK_SOFT(loaded.ToonSteps == 3 && std::abs(loaded.ToonSmoothness - 0.2f) < 1e-6f, "toon properties survive");

    nlohmann::json json;
    {
        std::ifstream file(path);
        file >> json;
    }
    CHECK_SOFT(json.value("Shader", std::string()) == "Lit/Specular" && json["Properties"].contains("_SpecColor"), "file uses Unity-style names");
    std::filesystem::remove(path);
}

TEST_CASE("Pre-shader .mat files migrate to the shader with an identical look") {
    MaterialLoader::ClearCache();
    Material unlit = MaterialLoader::ParseJsonText(R"({ "Color": [1.0, 0.5, 0.25, 1.0], "Texture": "abc" })");
    CHECK_SOFT(unlit.Shader == BuiltinShader::UnlitTexture, "Color+Texture becomes Unlit/Texture (texture x color x vertex color)");
    CHECK_SOFT(unlit.Color == glm::vec4(1.0f, 0.5f, 0.25f, 1.0f) && unlit.Texture.Guid == "abc", "authored values are kept");
    CHECK_SOFT(unlit.EffectiveSurface() == SurfaceType::Opaque && unlit.Cull == CullMode::Off && unlit.EffectiveZWrite(), "legacy draws were opaque and double-sided");

    Material vertexLit = MaterialLoader::ParseJsonText(R"({ "Color": [0.9, 0.5, 0.2, 1.0], "Shading Mode": "VertexLit", "Texture": "" })");
    CHECK_SOFT(vertexLit.Shader == BuiltinShader::MobileVertexLit && vertexLit.GetLightingModel() == LightingModel::VertexLit,
        "Shading Mode VertexLit becomes Mobile/VertexLit (same per-vertex Lambert path)");
    CHECK_SOFT(vertexLit.Cull == CullMode::Off, "migrated VertexLit stays double-sided");

    // Code that predates shaders and only sets ShadingMode keeps its meaning.
    Material codeBuilt;
    codeBuilt.ShadingMode = MaterialShadingMode::VertexLit;
    codeBuilt.Normalize();
    CHECK_SOFT(codeBuilt.Shader == BuiltinShader::MobileVertexLit, "ShadingMode = VertexLit on a default material selects Mobile/VertexLit");

    // ...but an explicit Unlit choice made through SetShader is not flipped back.
    Material explicitUnlit = Material::CreateDefaultLit();
    explicitUnlit.SetShader(BuiltinShader::UnlitColor);
    explicitUnlit.Normalize();
    CHECK_SOFT(explicitUnlit.Shader == BuiltinShader::UnlitColor, "SetShader keeps the legacy mirror consistent");

    Material unknown = MaterialLoader::ParseJsonText(R"({ "Shader": "Custom/DoesNotExist" })");
    CHECK_SOFT(unknown.Shader == BuiltinShader::UnlitTexture, "an unknown shader name falls back to Unlit/Texture");
    Material garbage = MaterialLoader::ParseJsonText("not json at all");
    CHECK_SOFT(garbage.Shader == BuiltinShader::UnlitTexture && garbage.Color == glm::vec4(1.0f), "unparsable files give the default material");
}

TEST_CASE("Switching shaders keeps every authored property (Unity behaviour)") {
    Material material = Material::CreateDefaultLit();
    material.SetShader(BuiltinShader::ToonLit);
    material.ToonSteps = 4;
    material.SpecColor = { 0.1f, 0.2f, 0.3f, 1.0f };
    material.SetShader(BuiltinShader::UnlitColor);
    Material reloaded = MaterialLoader::ParseJsonText(MaterialLoader::ToJsonText(material));
    reloaded.SetShader(BuiltinShader::ToonLit);
    CHECK_SOFT(reloaded.ToonSteps == 4 && reloaded.SpecColor == glm::vec4(0.1f, 0.2f, 0.3f, 1.0f), "values of properties the current shader doesn't use are saved too");
}

TEST_CASE("Surface type drives blend queue and depth writes; locked shaders override it") {
    Material material = Material::CreateDefaultLit();
    CHECK_SOFT(material.EffectiveRenderQueue() == 2000 && material.EffectiveZWrite(), "opaque: queue 2000, ZWrite on");
    material.Surface = SurfaceType::Cutout;
    CHECK_SOFT(material.EffectiveRenderQueue() == 2450 && material.EffectiveZWrite(), "cutout: AlphaTest queue, ZWrite on");
    material.Surface = SurfaceType::Transparent;
    CHECK_SOFT(material.EffectiveRenderQueue() == 3000 && !material.EffectiveZWrite(), "transparent: queue 3000, ZWrite off");
    material.ZWrite = ZWriteMode::On;
    CHECK_SOFT(material.EffectiveZWrite(), "ZWrite can be forced on");
    material.RenderQueue = 3100;
    CHECK_SOFT(material.EffectiveRenderQueue() == 3100, "explicit queue wins");

    Material additive;
    additive.SetShader(BuiltinShader::UnlitAdditive);
    additive.Surface = SurfaceType::Opaque; // ignored: the shader locks its surface
    CHECK_SOFT(additive.EffectiveSurface() == SurfaceType::Additive && !additive.EffectiveZWrite(), "Unlit/Additive is always additive");
    Material newAsset = Material::CreateDefaultLit();
    CHECK_SOFT(newAsset.Shader == BuiltinShader::LitDiffuse && newAsset.Cull == CullMode::Back, "new assets are Lit/Diffuse, back-face culled");
}

TEST_CASE("Scene lighting settings and Point/Spot lights round-trip through the scene file") {
    Scene source;
    source.GetLightingSettings().AmbientColor = { 0.1f, 0.2f, 0.3f, 1.0f };
    source.GetLightingSettings().AmbientIntensity = 1.5f;
    Entity point = source.CreateEntity("Lamp");
    auto& pointData = point.AddComponent<PointLightComponent>();
    pointData.Color = { 1.0f, 0.5f, 0.25f };
    pointData.Intensity = 2.0f;
    pointData.Range = 7.5f;
    Entity spot = source.CreateEntity("Torch");
    auto& spotData = spot.AddComponent<SpotLightComponent>();
    spotData.Range = 12.0f;
    spotData.SpotAngle = 50.0f;
    spotData.InnerSpotAngle = 35.0f;

    nlohmann::json snapshot = SceneSerializer(source).SerializeToJson();
    CHECK_SOFT(snapshot.contains("Lighting"), "the scene file stores its Lighting settings");
    Scene loaded;
    CHECK(SceneSerializer(loaded).DeserializeFromJson(snapshot));
    CHECK_SOFT(loaded.GetLightingSettings().AmbientColor == glm::vec4(0.1f, 0.2f, 0.3f, 1.0f) &&
        loaded.GetLightingSettings().AmbientIntensity == 1.5f, "ambient survives");

    bool foundPoint = false, foundSpot = false;
    for (Entity entity : loaded.GetHierarchyTraversalOrder()) {
        if (auto* light = entity.TryGetComponent<PointLightComponent>()) {
            foundPoint = light->Color == glm::vec3(1.0f, 0.5f, 0.25f) && light->Intensity == 2.0f && light->Range == 7.5f;
        }
        if (auto* light = entity.TryGetComponent<SpotLightComponent>()) {
            foundSpot = light->Range == 12.0f && light->SpotAngle == 50.0f && light->InnerSpotAngle == 35.0f;
        }
    }
    CHECK_SOFT(foundPoint, "Point Light fields survive");
    CHECK_SOFT(foundSpot, "Spot Light fields survive");

    // A scene saved before lighting settings existed keeps the old hard-coded ambient.
    snapshot.erase("Lighting");
    Scene legacy;
    legacy.GetLightingSettings().AmbientColor = glm::vec4(1.0f); // must be reset by the load
    CHECK(SceneSerializer(legacy).DeserializeFromJson(snapshot));
    CHECK_SOFT(std::abs(legacy.GetLightingSettings().EffectiveAmbient().r - 0.18f) < 1e-6f, "old scenes get the previous 0.18 ambient");
}

TEST_CASE("PopulateSceneLighting resolves every light in world space, main light first") {
    Scene scene;
    Entity point = scene.CreateEntity("Point");
    point.AddComponent<PointLightComponent>().Intensity = 3.0f; // white x3 saturates to 1
    point.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
    Entity sun = scene.CreateEntity("Sun");
    sun.AddComponent<DirectionalLightComponent>().CastShadows = true;
    Entity spot = scene.CreateEntity("Spot");
    auto& spotData = spot.AddComponent<SpotLightComponent>();
    spotData.SpotAngle = 60.0f;
    spotData.InnerSpotAngle = 30.0f;
    Entity disabled = scene.CreateEntity("Off");
    disabled.AddComponent<PointLightComponent>().Enabled = false;
    scene.GetLightingSettings().AmbientColor = { 0.5f, 0.5f, 0.5f, 1.0f };
    scene.GetLightingSettings().AmbientIntensity = 4.0f;

    RenderView view;
    PopulateSceneLighting(scene, view);
    CHECK_SOFT(view.Lights.size() == 3, "disabled lights are skipped");
    CHECK_SOFT(view.MainLightIndex == 0 && view.Lights[0].Type == LightType::Directional && view.Lights[0].CastShadows,
        "the main directional light is first even if it comes later in the hierarchy");
    CHECK_SOFT(view.Lights[1].Type == LightType::Point && view.Lights[1].Position == glm::vec3(1.0f, 2.0f, 3.0f), "point light position is world space");
    CHECK_SOFT(view.Lights[1].Color == glm::vec3(1.0f), "color x intensity saturates per channel (PICA200 8-bit registers)");
    CHECK_SOFT(view.Lights[2].Type == LightType::Spot && std::abs(view.Lights[2].CosOuter - std::cos(glm::radians(30.0f))) < 1e-5f &&
        std::abs(view.Lights[2].CosInner - std::cos(glm::radians(15.0f))) < 1e-5f, "spot angles are full cone angles -> half-angle cosines");
    CHECK_SOFT(view.Lights[2].Direction.z < -0.99f, "an unrotated spot points along -Z");
    CHECK_SOFT(view.AmbientColor == glm::vec3(1.0f), "ambient = color x intensity, saturated");
}

TEST_CASE("Per-object light selection honours range, cone and the 8-light PICA200 cap") {
    RenderView view;
    SceneLight sun;
    sun.Type = LightType::Directional;
    sun.Direction = { 0.0f, 1.0f, 0.0f };
    view.Lights.push_back(sun);
    view.MainLightIndex = 0;
    for (int i = 0; i < 10; ++i) {
        SceneLight point;
        point.Type = LightType::Point;
        point.Position = { static_cast<float>(i), 0.0f, 0.0f };
        point.Range = 20.0f; // all ten reach the object: more candidates than hardware slots
        view.Lights.push_back(point);
    }
    SceneLight far;
    far.Type = LightType::Point;
    far.Position = { 100.0f, 0.0f, 0.0f };
    far.Range = 5.0f;
    view.Lights.push_back(far);
    SceneLight spotAway;
    spotAway.Type = LightType::Spot;
    spotAway.Position = { 0.0f, 3.0f, 0.0f };
    spotAway.Direction = { 0.0f, 1.0f, 0.0f }; // points up, away from an object below it
    spotAway.Range = 10.0f;
    spotAway.CosOuter = std::cos(glm::radians(20.0f));
    spotAway.CosInner = std::cos(glm::radians(10.0f));
    view.Lights.push_back(spotAway);

    uint8_t indices[MaxLightsPerDraw];
    const uint8_t count = SelectLightsForBounds(view, glm::vec3(0.0f), 0.5f, indices);
    CHECK_SOFT(count == MaxLightsPerDraw, "never more than 8 lights per draw");
    CHECK_SOFT(indices[0] == 0, "the directional main light is always first");
    bool hasFar = false, hasSpot = false, hasNearest = false;
    for (uint8_t i = 0; i < count; ++i) {
        hasFar |= indices[i] == 11;
        hasSpot |= indices[i] == 12;
        hasNearest |= indices[i] == 1;
    }
    CHECK_SOFT(!hasFar, "a light whose range can't reach the bounds is skipped");
    CHECK_SOFT(!hasSpot, "a spot cone pointing away is skipped");
    CHECK_SOFT(hasNearest, "the closest (brightest at the object) point light is kept");
}

TEST_CASE("Mesh pass: materials reach the backend, lit draws carry lights, queues are sorted") {
    MaterialLoader::ClearCache();
    Scene scene;
    AddPerspectiveCamera(scene);
    Entity sun = scene.CreateEntity("Sun");
    sun.AddComponent<DirectionalLightComponent>();

    Material lit = Material::CreateDefaultLit();
    lit.SetShader(BuiltinShader::LitSpecular);
    lit.Shininess = 0.5f;
    Material glass = Material::CreateDefaultLit();
    glass.Surface = SurfaceType::Transparent;
    Material unlit;
    unlit.SetShader(BuiltinShader::UnlitColor);
    const std::string litGuid = RegisterMaterial("duality_pass_lit.mat", lit);
    const std::string glassGuid = RegisterMaterial("duality_pass_glass.mat", glass);
    const std::string unlitGuid = RegisterMaterial("duality_pass_unlit.mat", unlit);

    AddCube(scene, "GlassNear", { 0.0f, 0.0f, -4.0f }, glassGuid);
    AddCube(scene, "GlassFar", { 0.0f, 0.0f, -9.0f }, glassGuid);
    AddCube(scene, "Lit", { 1.0f, 0.0f, -6.0f }, litGuid);
    AddCube(scene, "Unlit", { -1.0f, 0.0f, -6.0f }, unlitGuid);

    RecordingRenderer renderer;
    const SceneRenderStats stats = RenderScreen3D(renderer, scene, Screen::Top, { 0.0f, 0.0f, 0.0f, 1.0f });
    CHECK(renderer.Commands.size() == 4);
    const MeshDrawCommand& first = renderer.Commands[0];
    const MeshDrawCommand& second = renderer.Commands[1];
    const MeshDrawCommand& third = renderer.Commands[2];
    const MeshDrawCommand& fourth = renderer.Commands[3];
    CHECK_SOFT(first.Material.Surface == SurfaceType::Opaque && second.Material.Surface == SurfaceType::Opaque, "opaque queue (2000) draws first");
    CHECK_SOFT(third.Material.Surface == SurfaceType::Transparent && fourth.Material.Surface == SurfaceType::Transparent, "transparent queue (3000) draws last");
    CHECK_SOFT(third.Translation.z < fourth.Translation.z, "transparent draws are back to front");
    CHECK_SOFT(!third.Material.ZWrite, "transparent draws don't write depth");

    const MeshDrawCommand* litCommand = nullptr;
    const MeshDrawCommand* unlitCommand = nullptr;
    for (const MeshDrawCommand& command : renderer.Commands) {
        if (command.Material.Shader == BuiltinShader::LitSpecular) litCommand = &command;
        if (command.Material.Shader == BuiltinShader::UnlitColor) unlitCommand = &command;
    }
    CHECK(litCommand && unlitCommand);
    CHECK_SOFT(litCommand->HasMaterial && litCommand->Material.Model == LightingModel::BlinnPhong, "the shader's lighting model reaches the backend");
    CHECK_SOFT(litCommand->LightCount == 1 && renderer.View.Lights.size() == 1, "lit draws carry their selected lights");
    CHECK_SOFT(std::abs(litCommand->Material.SpecExponent - 64.0f) < 1e-4f, "shininess 0.5 -> Blinn-Phong exponent 64");
    CHECK_SOFT(litCommand->Material.Cull == CullMode::Back, "render state reaches the backend");
    CHECK_SOFT(unlitCommand->LightCount == 0 && !unlitCommand->Material.UseVertexColor && !unlitCommand->Material.UseTexture,
        "Unlit/Color has no lights, vertex colors or texture");
    CHECK_SOFT(stats.LitDrawCalls == 3 && stats.TransparentDrawCalls == 2, "stats count lit and transparent draws");

    for (const char* name : { "duality_pass_lit.mat", "duality_pass_glass.mat", "duality_pass_unlit.mat" })
        std::filesystem::remove(TempPath(name));
}

TEST_CASE("Shared lighting curves (sampled into PICA LUTs, mirrored in GLSL)") {
    CHECK_SOFT(std::abs(LightingMath::DistanceAttenuation(0.0f) - 1.0f) < 1e-6f, "full intensity at the light");
    CHECK_SOFT(LightingMath::DistanceAttenuation(1.0f) == 0.0f, "exactly zero at the range (LUT clamps beyond it)");
    CHECK_SOFT(LightingMath::DistanceAttenuation(0.5f) < LightingMath::DistanceAttenuation(0.25f), "monotonic falloff");
    const float cosOuter = std::cos(glm::radians(30.0f)), cosInner = std::cos(glm::radians(20.0f));
    CHECK_SOFT(LightingMath::SpotFactor(1.0f, cosOuter, cosInner) == 1.0f && LightingMath::SpotFactor(0.0f, cosOuter, cosInner) == 0.0f, "spot cone inside/outside");
    CHECK_SOFT(LightingMath::ToonRamp(0.2f, 1.0f, 0.0f) == 0.0f && LightingMath::ToonRamp(0.8f, 1.0f, 0.0f) == 1.0f, "one-step toon ramp: hard terminator at N.L 0.5");
    CHECK_SOFT(std::abs(LightingMath::ToonRamp(0.6f, 2.0f, 0.0f) - 0.5f) < 1e-6f, "two steps give a mid band");
    CHECK_SOFT(LightingMath::ToonRamp(-0.5f, 3.0f, 0.1f) == 0.0f, "the unlit side stays dark");
    CHECK_SOFT(LightingMath::Specular(-0.2f, 32.0f) == 0.0f && std::abs(LightingMath::Specular(1.0f, 32.0f) - 1.0f) < 1e-6f, "Blinn-Phong LUT range");
    CHECK_SOFT(LightingMath::ShininessToExponent(1.0f) == 128.0f, "Unity legacy shininess scale");
}

TEST_CASE("Primitives face outward and carry tangents for normal mapping") {
    const std::vector<MeshVertex>& plane = GetPrimitiveMesh(MeshPrimitive::Plane);
    bool planeUp = true;
    for (size_t i = 0; i + 2 < plane.size(); i += 3) {
        const glm::vec3 faceNormal = glm::cross(plane[i + 1].Position - plane[i].Position, plane[i + 2].Position - plane[i].Position);
        planeUp &= faceNormal.y > 0.0f && plane[i].Normal.y > 0.99f;
    }
    CHECK_SOFT(planeUp, "the Plane is counter-clockwise from above and its normal points up");

    const std::vector<MeshVertex>& sphere = GetPrimitiveMesh(MeshPrimitive::Sphere);
    int outward = 0, faces = 0;
    for (size_t i = 0; i + 2 < sphere.size(); i += 3) {
        const glm::vec3 faceNormal = glm::cross(sphere[i + 1].Position - sphere[i].Position, sphere[i + 2].Position - sphere[i].Position);
        if (glm::dot(faceNormal, faceNormal) < 1e-10f) continue; // degenerate pole triangles
        ++faces;
        outward += glm::dot(faceNormal, sphere[i].Position + sphere[i + 1].Position + sphere[i + 2].Position) > 0.0f ? 1 : 0;
    }
    CHECK_SOFT(faces > 0 && outward == faces, "every sphere triangle is wound counter-clockwise from outside (survives Cull Back)");

    bool orthogonal = true;
    for (const MeshVertex& vertex : GetPrimitiveMesh(MeshPrimitive::Cube)) {
        const glm::vec3 tangent = glm::vec3(vertex.Tangent) / 127.0f;
        orthogonal &= std::abs(glm::dot(tangent, vertex.Normal)) < 0.02f && std::abs(glm::length(tangent) - 1.0f) < 0.02f;
    }
    CHECK_SOFT(orthogonal, "cube tangents are unit length and perpendicular to the normal");
    const glm::vec3 planeTangent = glm::vec3(plane[0].Tangent) / 127.0f;
    CHECK_SOFT(planeTangent.x > 0.99f, "the plane's tangent follows +U (= +X)");
}
