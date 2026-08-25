#include "DualityEngine/Asset/TextureImportSettings.h"

#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Duality {

    namespace {
        std::string MetaPathFor(const std::string& assetPath) { return assetPath + ".meta"; }
    }

    TextureImportSettings TextureImportSettings::Load(const std::string& assetPath) {
        TextureImportSettings settings;
        std::ifstream file(MetaPathFor(assetPath));
        if (!file)
            return settings;

        try {
            json root;
            file >> root;
            if (!root.contains("Importer"))
                return settings;
            auto& importer = root["Importer"];
            settings.FilterMode = importer.value("FilterMode", std::string()) == "Point" ? TextureFilterMode::Point : TextureFilterMode::Bilinear;
            settings.WrapMode = importer.value("WrapMode", std::string()) == "Repeat" ? TextureWrapMode::Repeat : TextureWrapMode::Clamp;
            settings.GenerateMipmaps = importer.value("GenerateMipmaps", false);
        } catch (const json::parse_error&) {
            // Fall through and return the defaults above.
        }
        return settings;
    }

    bool TextureImportSettings::Save(const std::string& assetPath, const TextureImportSettings& settings) {
        std::string metaPath = MetaPathFor(assetPath);

        json root;
        std::ifstream inFile(metaPath);
        if (inFile) {
            try {
                inFile >> root;
            } catch (const json::parse_error&) {
                root = json::object(); // corrupt/empty -- start fresh rather than fail the save
            }
        }

        root["Importer"]["FilterMode"] = settings.FilterMode == TextureFilterMode::Point ? "Point" : "Bilinear";
        root["Importer"]["WrapMode"] = settings.WrapMode == TextureWrapMode::Repeat ? "Repeat" : "Clamp";
        root["Importer"]["GenerateMipmaps"] = settings.GenerateMipmaps;

        std::ofstream outFile(metaPath);
        if (!outFile)
            return false;
        outFile << root.dump(4);
        return true;
    }

}
