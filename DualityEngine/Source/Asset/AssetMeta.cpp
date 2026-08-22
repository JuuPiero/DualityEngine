#include "DualityEngine/Asset/AssetMeta.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Duality {

    static std::string GenerateGuid() {
        static std::mt19937_64 s_Engine{ std::random_device{}() };
        static std::uniform_int_distribution<uint64_t> s_Dist;

        char buffer[33];
        std::snprintf(buffer, sizeof(buffer), "%016llx%016llx",
                      static_cast<unsigned long long>(s_Dist(s_Engine)),
                      static_cast<unsigned long long>(s_Dist(s_Engine)));
        return std::string(buffer);
    }

    void AssetMeta::EnsureMetaFile(const std::filesystem::path& assetPath) {
        std::filesystem::path metaPath = assetPath;
        metaPath += ".meta";

        if (std::filesystem::exists(metaPath))
            return;

        json root;
        root["guid"] = GenerateGuid();

        std::ofstream file(metaPath);
        file << root.dump(4);
    }

}
