#include "DualityEngine/Asset/PhysicsMaterialLoader.h"

#include <fstream>

#include <nlohmann/json.hpp>

#include "DualityEngine/Reflection/FieldSerialization.h"

namespace Duality {

    PhysicsMaterial PhysicsMaterialLoader::Load(const std::string& path) {
        PhysicsMaterial material;
        std::ifstream file(path);
        if (!file.is_open())
            return material;
        try {
            nlohmann::json j;
            file >> j;
            for (auto& field : PhysicsMaterial::Fields()) {
                if (j.contains(field.Name))
                    field.Set(&material, JsonToFieldValue(j.at(field.Name), field.Get(&material)));
            }
        } catch (...) {
        }
        return material;
    }

    void PhysicsMaterialLoader::Save(const std::string& path, const PhysicsMaterial& material) {
        nlohmann::json j = nlohmann::json::object();
        for (auto& field : PhysicsMaterial::Fields())
            j[field.Name] = FieldValueToJson(field.Get(const_cast<PhysicsMaterial*>(&material)));
        std::ofstream file(path);
        if (file.is_open())
            file << j.dump(2);
    }

}
