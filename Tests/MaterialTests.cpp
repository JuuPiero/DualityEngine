// Material save/load round-trip -- regression coverage for MaterialLoader's refactor to the
// same generic Material::Fields() + FieldValueToJson/JsonToFieldValue machinery
// ScriptableObjectLoader uses (previously a one-off hand-written {r,g,b,a} JSON shape).
#include "TestFramework.h"

#include <filesystem>

#include "DualityEngine/Asset/Material.h"
#include "DualityEngine/Asset/MaterialLoader.h"

using namespace Duality;

TEST_CASE("Material save+load round-trips Color and Texture") {
    std::string path = (std::filesystem::temp_directory_path() / "duality_engine_test_material.mat").string();
    std::filesystem::remove(path);

    Material original;
    original.Color = { 0.25f, 0.5f, 0.75f, 1.0f };
    original.Texture.Guid = "abc123";
    CHECK(MaterialLoader::Save(path, original));

    Material loaded = MaterialLoader::Load(path);
    CHECK_SOFT(loaded.Color.r == 0.25f && loaded.Color.g == 0.5f && loaded.Color.b == 0.75f && loaded.Color.a == 1.0f, "Color round-tripped");
    CHECK_SOFT(loaded.Texture.Guid == "abc123", "Texture guid round-tripped");

    std::filesystem::remove(path);
}

TEST_CASE("Material::Load returns defaults for a nonexistent file") {
    Material material = MaterialLoader::Load("this_material_does_not_exist.mat");
    CHECK_SOFT(material.Color.r == 1.0f && material.Color.g == 1.0f && material.Color.b == 1.0f && material.Color.a == 1.0f, "default Color is white");
    CHECK_SOFT(material.Texture.Guid.empty(), "default Texture is unassigned");
}
