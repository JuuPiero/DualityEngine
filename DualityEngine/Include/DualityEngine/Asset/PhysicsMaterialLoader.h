#pragma once

#include <string>

#include "DualityEngine/Asset/PhysicsMaterial.h"

namespace Duality {

    class PhysicsMaterialLoader {
    public:
        static PhysicsMaterial Load(const std::string& path);
        static void Save(const std::string& path, const PhysicsMaterial& material);
    };

}
