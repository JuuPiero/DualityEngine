#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace Duality {

    // Plain JSON file save/load, identical on both platforms -- confirmed
    // by References/devkitpro-3ds-templates/sdmc that a relative-path
    // fopen()/fstream already works on real 3DS hardware with no explicit
    // "sdmc:/" prefix or archive mounting, exactly like Project/
    // SceneSerializer already assume for their own file I/O. `path` is
    // resolved against the process's current working directory on both
    // platforms -- no separate "user data directory" layer this pass.
    // Header-only (std::fstream + nlohmann::json are both already
    // standard-library/header-only), so it's directly callable from
    // Behaviour scripts with no EngineServices entry needed.
    class SaveSystem {
    public:
        static bool SaveJson(const std::string& path, const nlohmann::json& data) {
            std::filesystem::path parent = std::filesystem::path(path).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent);

            std::ofstream file(path);
            if (!file.is_open())
                return false;
            file << data.dump(2);
            return true;
        }

        // Returns an empty *object* (not a null json -- calling .value() on
        // a null json throws) if `path` doesn't exist or fails to parse --
        // callers should treat that the same as "no save yet".
        static nlohmann::json LoadJson(const std::string& path) {
            std::ifstream file(path);
            if (!file.is_open())
                return nlohmann::json::object();

            nlohmann::json data;
            try {
                file >> data;
            } catch (const nlohmann::json::parse_error&) {
                return nlohmann::json::object();
            }
            return data;
        }
    };

}
