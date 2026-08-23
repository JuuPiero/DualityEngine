#include "DualityEngine/Asset/AssetDatabase.h"

#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Duality {

    namespace {
        std::unordered_map<std::string, std::string> s_GuidToPath;
    }

    void AssetDatabase::Refresh(const std::string& rootDirectory) {
        s_GuidToPath.clear();

        if (!std::filesystem::exists(rootDirectory))
            return;

        for (auto& entry : std::filesystem::recursive_directory_iterator(rootDirectory)) {
            if (entry.is_directory() || entry.path().extension() != ".meta")
                continue;

            std::ifstream file(entry.path());
            json root;
            try {
                file >> root;
            } catch (const json::parse_error&) {
                continue;
            }

            std::string guid = root.value("guid", std::string());
            if (guid.empty())
                continue;

            // "<asset>.meta" -> "<asset>"
            std::string assetPath = entry.path().string();
            assetPath.erase(assetPath.size() - std::string(".meta").size());
            s_GuidToPath[guid] = assetPath;
        }
    }

    void AssetDatabase::Register(const std::string& guid, const std::string& path) {
        if (!guid.empty())
            s_GuidToPath[guid] = path;
    }

    std::string AssetDatabase::ResolvePath(const std::string& guid) {
        auto it = s_GuidToPath.find(guid);
        return it != s_GuidToPath.end() ? it->second : std::string();
    }

}
