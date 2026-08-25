#include "DualityEditor/AssetInspectorRegistry.h"

namespace Duality {

    namespace {
        std::vector<AssetInspectorEntry>& List() {
            static std::vector<AssetInspectorEntry> list;
            return list;
        }
    }

    void AssetInspectorRegistry::Register(const AssetInspectorEntry& entry) {
        List().push_back(entry);
    }

    const AssetInspectorEntry* AssetInspectorRegistry::FindByExtension(const std::string& extension) {
        for (auto& entry : List()) {
            if (entry.Extension == extension)
                return &entry;
        }
        return nullptr;
    }

}
