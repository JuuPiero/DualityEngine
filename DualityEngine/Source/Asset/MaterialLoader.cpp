#include "DualityEngine/Asset/MaterialLoader.h"

#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Duality {

    namespace {
        std::unordered_map<std::string, Material> s_Cache;
    }

    Material MaterialLoader::Load(const std::string& path) {
        auto it = s_Cache.find(path);
        if (it != s_Cache.end())
            return it->second;

        Material material;
        std::ifstream file(path);
        if (file) {
            try {
                json root;
                file >> root;
                if (root.contains("Color")) {
                    auto& c = root["Color"];
                    material.Color = { c.value("r", 1.0f), c.value("g", 1.0f), c.value("b", 1.0f), c.value("a", 1.0f) };
                }
                material.Texture.Guid = root.value("Texture", std::string());
            } catch (const json::parse_error&) {
                // Fall through and cache the default Material below.
            }
        }

        s_Cache[path] = material;
        return material;
    }

    bool MaterialLoader::Save(const std::string& path, const Material& material) {
        json root;
        root["Color"] = { { "r", material.Color.r }, { "g", material.Color.g }, { "b", material.Color.b }, { "a", material.Color.a } };
        root["Texture"] = material.Texture.Guid;

        std::ofstream file(path);
        if (!file)
            return false;
        file << root.dump(4);

        s_Cache[path] = material; // refresh the cache immediately so the Editor sees the edit this frame
        return true;
    }

}
