#include "DualityEngine/Asset/MaterialLoader.h"

#include <fstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "DualityEngine/Reflection/FieldSerialization.h"

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
                for (auto& field : Material::Fields()) {
                    if (root.contains(field.Name)) {
                        FieldValue current = field.Get(&material);
                        field.Set(&material, JsonToFieldValue(root[field.Name], current));
                    }
                }
            } catch (const json::parse_error&) {
                // Fall through and cache the default Material below.
            }
        }

        s_Cache[path] = material;
        return material;
    }

    bool MaterialLoader::Save(const std::string& path, const Material& material) {
        json root;
        Material temp = material; // FieldHandle::Get takes a plain (non-const) void*
        for (auto& field : Material::Fields())
            root[field.Name] = FieldValueToJson(field.Get(&temp));

        std::ofstream file(path);
        if (!file)
            return false;
        file << root.dump(4);

        s_Cache[path] = material; // refresh the cache immediately so the Editor sees the edit this frame
        return true;
    }

}
