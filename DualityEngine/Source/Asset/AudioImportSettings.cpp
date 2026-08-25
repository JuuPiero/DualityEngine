#include "DualityEngine/Asset/AudioImportSettings.h"

#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Duality {

    namespace {
        std::string MetaPathFor(const std::string& assetPath) { return assetPath + ".meta"; }
    }

    AudioImportSettings AudioImportSettings::Load(const std::string& assetPath) {
        AudioImportSettings settings;
        std::ifstream file(MetaPathFor(assetPath));
        if (!file)
            return settings;

        try {
            json root;
            file >> root;
            if (root.contains("Importer"))
                settings.Volume = root["Importer"].value("Volume", 1.0f);
        } catch (const json::parse_error&) {
            // Fall through and return the default (full volume) above.
        }
        return settings;
    }

    bool AudioImportSettings::Save(const std::string& assetPath, const AudioImportSettings& settings) {
        std::string metaPath = MetaPathFor(assetPath);

        json root;
        std::ifstream inFile(metaPath);
        if (inFile) {
            try {
                inFile >> root;
            } catch (const json::parse_error&) {
                root = json::object();
            }
        }

        root["Importer"]["Volume"] = settings.Volume;

        std::ofstream outFile(metaPath);
        if (!outFile)
            return false;
        outFile << root.dump(4);
        return true;
    }

}
