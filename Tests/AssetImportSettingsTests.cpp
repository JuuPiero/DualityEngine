// TextureImportSettings/AudioImportSettings round-trip coverage -- both are Unity/Cocos-style
// ".meta" importer blocks (see DualityEngine/Asset/{Texture,Audio}ImportSettings.h), read/
// written independently of AssetMeta's own "guid" key.
#include "TestFramework.h"

#include <filesystem>

#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/AudioImportSettings.h"
#include "DualityEngine/Asset/TextureImportSettings.h"

using namespace Duality;

namespace {
    std::string TempAssetPath(const char* filename) {
        return (std::filesystem::temp_directory_path() / filename).string();
    }
}

TEST_CASE("TextureImportSettings defaults match GLTextureLoader's original hardcoded behavior") {
    TextureImportSettings settings = TextureImportSettings::Load("this_texture_has_no_meta_file.png");
    CHECK_SOFT(settings.FilterMode == TextureFilterMode::Bilinear, "default FilterMode is Bilinear");
    CHECK_SOFT(settings.WrapMode == TextureWrapMode::Clamp, "default WrapMode is Clamp");
    CHECK_SOFT(!settings.GenerateMipmaps, "default GenerateMipmaps is false");
}

TEST_CASE("TextureImportSettings::Save round-trips and preserves the .meta's guid") {
    std::string assetPath = TempAssetPath("duality_engine_test_texture.png");
    std::string metaPath = assetPath + ".meta";
    std::filesystem::remove(metaPath);

    std::string guid = AssetMeta::EnsureMetaFile(assetPath);
    CHECK(!guid.empty());

    TextureImportSettings settings;
    settings.FilterMode = TextureFilterMode::Point;
    settings.WrapMode = TextureWrapMode::Repeat;
    settings.GenerateMipmaps = true;
    CHECK(TextureImportSettings::Save(assetPath, settings));

    TextureImportSettings reloaded = TextureImportSettings::Load(assetPath);
    CHECK_SOFT(reloaded.FilterMode == TextureFilterMode::Point, "FilterMode round-tripped");
    CHECK_SOFT(reloaded.WrapMode == TextureWrapMode::Repeat, "WrapMode round-tripped");
    CHECK_SOFT(reloaded.GenerateMipmaps, "GenerateMipmaps round-tripped");

    // Save must not clobber the guid AssetMeta wrote first -- a real Content Browser asset
    // always has its guid written before any importer settings are ever edited.
    CHECK_SOFT(AssetMeta::EnsureMetaFile(assetPath) == guid, "guid survived Save() untouched");

    std::filesystem::remove(metaPath);
}

TEST_CASE("AudioImportSettings defaults to full volume and round-trips") {
    AudioImportSettings defaults = AudioImportSettings::Load("this_audio_has_no_meta_file.wav");
    CHECK_SOFT(defaults.Volume == 1.0f, "default Volume is 1.0 (no volume control until edited)");

    std::string assetPath = TempAssetPath("duality_engine_test_audio.wav");
    std::string metaPath = assetPath + ".meta";
    std::filesystem::remove(metaPath);

    std::string guid = AssetMeta::EnsureMetaFile(assetPath);
    AudioImportSettings settings;
    settings.Volume = 0.4f;
    CHECK(AudioImportSettings::Save(assetPath, settings));

    AudioImportSettings reloaded = AudioImportSettings::Load(assetPath);
    CHECK_SOFT(reloaded.Volume == 0.4f, "Volume round-tripped");
    CHECK_SOFT(AssetMeta::EnsureMetaFile(assetPath) == guid, "guid survived Save() untouched");

    std::filesystem::remove(metaPath);
}
