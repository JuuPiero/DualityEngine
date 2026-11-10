// MeshFilter + MeshRenderer (Unity's split): built-in mesh references, loading scenes/prefabs
// saved before the split (Mesh Renderer {Primitive, Mesh}), rendering and Mesh Collider rules.
#include <algorithm>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "TestFramework.h"

#include "DualityEngine/Asset/MeshReference.h"
#include "DualityEngine/Reflection/TypeRegistry.h"
#include "DualityEngine/Renderer/BuiltinMeshes.h"
#include "DualityEngine/Renderer/PrimitiveMeshes.h"
#include "DualityEngine/Renderer/SceneRenderer.h"
#include "DualityEngine/Scene/Components.h"
#include "DualityEngine/Scene/PrefabSerializer.h"
#include "DualityEngine/Scene/Scene.h"
#include "DualityEngine/Scene/SceneSerializer.h"
#include "DualityEngine/Scripting/MeshFilter.h"
#include "DualityEngine/Scripting/MeshRenderer.h"
// The scripting umbrella header must compile next to the engine headers (script API surface).
#include "DualityEngine/Scripting/Scripting.h"

using namespace Duality;
using json = nlohmann::json;

namespace {
    class RecordingRenderer3D final : public IRenderer3D {
    public:
        void Init() override {}
        void Shutdown() override {}
        void BeginScene(Screen, ProjectionType, const glm::vec3&, const glm::vec3&, float, float, float, float, float, const glm::vec4&, bool) override {}
        void BeginScene(const RenderView&, const glm::vec4&, bool) override {}
        void EndScene() override {}
        void DrawMesh(MeshPrimitive, uint32_t, uint32_t, const glm::vec3&, const glm::vec3&, const glm::vec3&, const glm::vec4&, uint32_t) override {}
        void DrawMesh(const MeshDrawCommand& command) override { Commands.push_back(command); }
        uint32_t GetSubMeshCount(uint32_t) const override { return 1; }
        uint32_t LoadTexture(const std::string&) override { return 0; }
        uint32_t LoadMesh(const std::string&) override { return 0; }
        void UnloadAllTextures() override {}
        void UnloadAllMeshes() override {}
        uint32_t GetDrawCallCount() const override { return static_cast<uint32_t>(Commands.size()); }
        std::vector<MeshDrawCommand> Commands;
    };

    // One entity as an old (pre-MeshFilter) scene file stored it.
    json LegacyEntity(const std::string& name, json meshRenderer) {
        return json{
            { "Name", { { "Name", name } } },
            { "Transform", { { "Translation", { 0.0, 0.0, -5.0 } }, { "Rotation", { 0.0, 0.0, 0.0 } }, { "Scale", { 1.0, 1.0, 1.0 } } } },
            { "Mesh Renderer", std::move(meshRenderer) },
            { "Parent", -1 },
        };
    }

    json LegacyScene(std::vector<json> entities) {
        return json{ { "WorldUnitsVersion", 2 }, { "CanvasUIVersion", 1 }, { "Entities", std::move(entities) } };
    }

    Entity FindByName(Scene& scene, const std::string& name) {
        for (Entity entity : scene.GetHierarchyTraversalOrder())
            if (entity.GetComponent<NameComponent>().Name == name)
                return entity;
        return Entity{};
    }

    std::string MeshOf(Entity entity) {
        return entity && entity.HasComponent<MeshFilterComponent>() ? entity.GetComponent<MeshFilterComponent>().Mesh.Guid : std::string("<no filter>");
    }
}

TEST_CASE("Built-in mesh references: names, parsing, geometry for all six meshes") {
    CHECK_SOFT(BuiltinMeshes::ReferenceFor(MeshPrimitive::Cube) == "builtin:Cube", "Cube reference");
    MeshPrimitive primitive = MeshPrimitive::Cube;
    CHECK_SOFT(BuiltinMeshes::TryGetPrimitive(std::string("builtin:Cylinder"), primitive) && primitive == MeshPrimitive::Cylinder, "parses Cylinder");
    CHECK_SOFT(!BuiltinMeshes::TryGetPrimitive(std::string("builtin:Teapot"), primitive), "unknown built-in name is rejected");
    CHECK_SOFT(!BuiltinMeshes::TryGetPrimitive(std::string("3cedc98a45109310a1d8bf4968f3046b"), primitive), "a GUID is not built-in");
    CHECK_SOFT(BuiltinMeshes::IsBuiltin(std::string("builtin:Quad")) && !BuiltinMeshes::IsBuiltin(std::string("")), "IsBuiltin");
    int count = 0;
    for (const BuiltinMeshes::Info& info : BuiltinMeshes::All) {
        count++;
        MeshPrimitive parsed;
        CHECK_SOFT(BuiltinMeshes::TryGetPrimitive(BuiltinMeshes::ReferenceFor(info.Primitive), parsed) && parsed == info.Primitive, info.Name);
        const std::vector<MeshVertex>& vertices = GetPrimitiveMesh(info.Primitive);
        CHECK_SOFT(!vertices.empty() && vertices.size() % 3 == 0, (std::string(info.Name) + " has a triangle list").c_str());
        const std::vector<MeshVertex>* resolved = MeshReference::Vertices(BuiltinMeshes::Ref(info.Primitive));
        CHECK_SOFT(resolved == &vertices, (std::string(info.Name) + " resolves through MeshReference").c_str());
        CHECK_SOFT(MeshReference::BoundingRadius(BuiltinMeshes::Ref(info.Primitive)) > 0.49f, "bounding radius");
    }
    CHECK_SOFT(count == 6, "Cube, Sphere, Capsule, Cylinder, Plane, Quad");
    CHECK_SOFT(MeshReference::Vertices(AssetRef{}) == nullptr, "an empty mesh reference has no vertices");

    // Cylinder side normals point away from the axis, caps along +-Y; Quad faces +Z (camera).
    for (const MeshVertex& vertex : GetPrimitiveMesh(MeshPrimitive::Cylinder)) {
        const bool cap = std::abs(vertex.Normal.y) > 0.99f;
        const bool side = std::abs(vertex.Normal.y) < 1e-4f && glm::dot(glm::vec2(vertex.Normal.x, vertex.Normal.z), glm::vec2(vertex.Position.x, vertex.Position.z)) > 0.0f;
        CHECK_SOFT(cap || side, "cylinder normals face outward");
        if (!(cap || side))
            break;
    }
    for (const MeshVertex& vertex : GetPrimitiveMesh(MeshPrimitive::Quad))
        CHECK_SOFT(vertex.Normal.z > 0.99f && vertex.Position.z == 0.0f, "quad is in the XY plane facing +Z");
}

TEST_CASE("Old scenes: Mesh Renderer Primitive/Mesh migrates into a Mesh Filter with the same mesh") {
    const json old = LegacyScene({
        LegacyEntity("Sphere", { { "Enabled", true }, { "Materials", { "aaaa" } }, { "Mesh", "" }, { "Primitive", "Sphere" }, { "Sort Order", 3 } }),
        LegacyEntity("Imported", { { "Enabled", false }, { "Materials", json::array() }, { "Mesh", "3cedc98a45109310a1d8bf4968f3046b" }, { "Primitive", "Plane" }, { "Sort Order", 0 } }),
        LegacyEntity("DefaultCube", { { "Enabled", true }, { "Mesh", "" } }),
        LegacyEntity("Capsule", { { "Primitive", "Capsule" } }),
        LegacyEntity("BadName", { { "Primitive", "Teapot" }, { "Mesh", "" } }),
    });
    Scene scene;
    CHECK(SceneSerializer(scene).DeserializeFromJson(old));
    CHECK_SOFT(MeshOf(FindByName(scene, "Sphere")) == "builtin:Sphere", "Primitive Sphere -> builtin:Sphere");
    CHECK_SOFT(MeshOf(FindByName(scene, "Imported")) == "3cedc98a45109310a1d8bf4968f3046b", "an imported Mesh wins over Primitive");
    CHECK_SOFT(MeshOf(FindByName(scene, "DefaultCube")) == "builtin:Cube", "no Primitive key = the old default Cube");
    CHECK_SOFT(MeshOf(FindByName(scene, "Capsule")) == "builtin:Capsule", "Primitive without a Mesh key");
    CHECK_SOFT(MeshOf(FindByName(scene, "BadName")) == "builtin:Cube", "an unknown Primitive name reads as Cube (old behaviour)");

    const auto& sphere = FindByName(scene, "Sphere").GetComponent<MeshRendererComponent>();
    CHECK_SOFT(sphere.Materials.size() == 1 && sphere.Materials[0].Guid == "aaaa" && sphere.SortOrder == 3 && sphere.Enabled, "renderer keeps its materials / sort order / enabled");
    CHECK_SOFT(!FindByName(scene, "Imported").GetComponent<MeshRendererComponent>().Enabled, "a disabled renderer stays disabled");

    // Saving writes the new format: no Primitive/Mesh in Mesh Renderer, a Mesh Filter beside it.
    const json saved = SceneSerializer(scene).SerializeToJson();
    CHECK_SOFT(saved.value("MeshFilterVersion", 0) == 1, "scene is stamped with MeshFilterVersion 1");
    bool clean = true;
    for (const json& entity : saved["Entities"]) {
        if (!entity.contains("Mesh Renderer"))
            continue;
        clean &= !entity["Mesh Renderer"].contains("Primitive") && !entity["Mesh Renderer"].contains("Mesh");
        clean &= entity.contains("Mesh Filter") && entity["Mesh Filter"].contains("Mesh");
    }
    CHECK_SOFT(clean, "new format on save");
    Scene reloaded;
    SceneSerializer(reloaded).DeserializeFromJson(saved);
    CHECK_SOFT(SceneSerializer(reloaded).SerializeToJson() == saved, "the new format round-trips unchanged");
}

TEST_CASE("Migration never overwrites an existing Mesh Filter, and new-format renderers stay filterless") {
    json entity = LegacyEntity("Both", { { "Primitive", "Sphere" }, { "Mesh", "" }, { "Materials", json::array() } });
    entity["Mesh Filter"] = { { "Mesh", "builtin:Quad" } };
    json plain = LegacyEntity("NoFilter", { { "Enabled", true }, { "Materials", json::array() }, { "Sort Order", 0 } });
    Scene scene;
    SceneSerializer(scene).DeserializeFromJson(LegacyScene({ entity, plain }));
    CHECK_SOFT(MeshOf(FindByName(scene, "Both")) == "builtin:Quad", "existing Mesh Filter kept");
    CHECK_SOFT(!FindByName(scene, "NoFilter").HasComponent<MeshFilterComponent>(), "a new-format Mesh Renderer alone does not get a filter");
}

TEST_CASE("Old prefabs and clipboard data migrate too; duplicate keeps the Mesh Filter") {
    const std::filesystem::path prefabPath = std::filesystem::temp_directory_path() / "duality_meshfilter_test.prefab";
    {
        std::ofstream(prefabPath) << LegacyScene({ LegacyEntity("Rock", { { "Primitive", "Sphere" }, { "Mesh", "" }, { "Materials", { "m1" } } }) }).dump(2);
    }
    Scene scene;
    Entity instance = PrefabSerializer::Instantiate(scene, prefabPath.string());
    std::filesystem::remove(prefabPath);
    CHECK(instance);
    CHECK_SOFT(MeshOf(instance) == "builtin:Sphere", "prefab instance migrated");

    Entity copy = PrefabSerializer::Duplicate(scene, instance);
    CHECK_SOFT(MeshOf(copy) == "builtin:Sphere" && copy.GetComponent<MeshRendererComponent>().Materials.size() == 1, "duplicate keeps Mesh Filter + materials");

    json clipboard = PrefabSerializer::CopyToJson(scene, { instance });
    CHECK_SOFT(clipboard.value("MeshFilterVersion", 0) == 1, "clipboard is stamped");
    // An old clipboard (copied by an older editor build) still pastes with its mesh.
    json oldClipboard = clipboard;
    for (json& entityJson : oldClipboard["Entities"]) {
        entityJson.erase("Mesh Filter");
        entityJson["Mesh Renderer"]["Primitive"] = "Capsule";
        entityJson["Mesh Renderer"]["Mesh"] = "";
    }
    const std::vector<Entity> pasted = PrefabSerializer::PasteFromJson(scene, oldClipboard, Entity{});
    CHECK_SOFT(pasted.size() == 1 && MeshOf(pasted[0]) == "builtin:Capsule", "old clipboard pastes with its primitive");
}

TEST_CASE("Mesh Renderer draws its Mesh Filter's mesh, and nothing without one") {
    Scene scene;
    Entity camera = scene.CreateEntity("Camera");
    auto& cameraData = camera.AddComponent<CameraComponent>();
    cameraData.Projection = ProjectionType::Perspective;

    Entity quad = scene.CreateEntity("Quad");
    quad.GetComponent<TransformComponent>().Translation = { 0.0f, 0.0f, -5.0f };
    quad.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Quad);
    quad.AddComponent<MeshRendererComponent>();

    Entity bare = scene.CreateEntity("No Filter");
    bare.GetComponent<TransformComponent>().Translation = { 0.5f, 0.0f, -5.0f };
    bare.AddComponent<MeshRendererComponent>();

    Entity empty = scene.CreateEntity("Empty Mesh");
    empty.GetComponent<TransformComponent>().Translation = { -0.5f, 0.0f, -5.0f };
    empty.AddComponent<MeshFilterComponent>();
    empty.AddComponent<MeshRendererComponent>();

    Entity missing = scene.CreateEntity("Missing Mesh");
    missing.GetComponent<TransformComponent>().Translation = { -0.5f, 0.5f, -5.0f };
    missing.AddComponent<MeshFilterComponent>().Mesh = AssetRef{ "ffffffffffffffffffffffffffffffff" };
    missing.AddComponent<MeshRendererComponent>();

    Entity filterOnly = scene.CreateEntity("Filter Only");
    filterOnly.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Cube);

    RecordingRenderer3D renderer;
    RenderScreen3D(renderer, scene, Screen::Top, { 0.0f, 0.0f, 0.0f, 1.0f });
    CHECK_SOFT(renderer.Commands.size() == 1, "only the renderer with a valid Mesh Filter mesh is drawn");
    CHECK_SOFT(!renderer.Commands.empty() && renderer.Commands[0].Primitive == MeshPrimitive::Quad && renderer.Commands[0].MeshHandle == 0, "built-in Quad is drawn as the Quad primitive");
}

TEST_CASE("Mesh Collider with no mesh uses the Mesh Filter's; C++ MeshRenderer compat forwards to it") {
    Scene scene;
    Entity floor = scene.CreateEntity("Floor");
    floor.AddComponent<MeshFilterComponent>().Mesh = BuiltinMeshes::Ref(MeshPrimitive::Plane);
    floor.AddComponent<MeshRendererComponent>();
    floor.AddComponent<MeshColliderComponent>();
    Entity noFilter = scene.CreateEntity("Collider Only");
    noFilter.AddComponent<MeshColliderComponent>();
    scene.OnRuntimeStart();
    CHECK_SOFT(scene.Raycast3D({ 0.0f, -5.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, 20.0f).HitEntity == floor, "ray hits the plane from the Mesh Filter");
    scene.OnRuntimeStop();

    Entity legacy = scene.CreateEntity("Legacy Script Target");
    legacy.AddComponent<MeshRendererComponent>();
    MeshRenderer renderer(legacy);
    renderer.SetPrimitive(MeshPrimitive::Sphere);
    CHECK_SOFT(MeshOf(legacy) == "builtin:Sphere", "SetPrimitive adds a Mesh Filter with the built-in mesh");
    CHECK_SOFT(renderer.GetPrimitive() == MeshPrimitive::Sphere && renderer.GetMesh().Guid == "builtin:Sphere", "getters read the Mesh Filter");
    MeshFilter filter(legacy);
    filter.SetMesh(MeshPrimitive::Cylinder);
    MeshPrimitive primitive;
    CHECK_SOFT(filter.IsBuiltinMesh(&primitive) && primitive == MeshPrimitive::Cylinder, "MeshFilter script wrapper");
}

TEST_CASE("Every SampleProject Mesh Renderer loads with the mesh its old Primitive/Mesh named") {
    const std::filesystem::path assets = std::filesystem::path(DUALITY_REPO_ROOT) / "SampleProject" / "Assets";
    int checked = 0;
    for (const auto& file : std::filesystem::recursive_directory_iterator(assets)) {
        if (!file.is_regular_file() || (file.path().extension() != ".scene" && file.path().extension() != ".prefab"))
            continue;
        std::ifstream stream(file.path());
        const json source = json::parse(stream, nullptr, false);
        if (source.is_discarded() || !source.contains("Entities"))
            continue;
        Scene scene;
        if (file.path().extension() == ".scene")
            SceneSerializer(scene).DeserializeFromJson(source);
        else
            PrefabSerializer::Instantiate(scene, file.path().string());
        // The serializers keep file order: entity i of the file is the i-th created entity.
        std::vector<Entity> created;
        for (auto handle : scene.Registry().view<NameComponent>())
            created.push_back(Entity(handle, &scene));
        std::sort(created.begin(), created.end(), [](Entity a, Entity b) { return a.Handle() < b.Handle(); });
        for (size_t i = 0; i < source["Entities"].size() && i < created.size(); ++i) {
            const json& entityJson = source["Entities"][i];
            if (!entityJson.contains("Mesh Renderer"))
                continue;
            const json& renderer = entityJson["Mesh Renderer"];
            std::string expected;
            if (entityJson.contains("Mesh Filter"))
                expected = entityJson["Mesh Filter"].value("Mesh", std::string());
            else if (renderer.value("Mesh", std::string()).empty())
                expected = "builtin:" + renderer.value("Primitive", std::string("Cube"));
            else
                expected = renderer.value("Mesh", std::string());
            CHECK_SOFT(MeshOf(created[i]) == expected, (file.path().filename().string() + ": " + created[i].GetComponent<NameComponent>().Name).c_str());
            checked++;
        }
    }
    CHECK_SOFT(checked >= 20, "the sample scenes contain Mesh Renderers to check");
}
