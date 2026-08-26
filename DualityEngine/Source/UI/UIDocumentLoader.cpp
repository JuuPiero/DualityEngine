#include "DualityEngine/UI/UIDocumentLoader.h"

#include <unordered_map>

namespace Duality {

    namespace {
        std::unordered_map<std::string, UIDocument>& Cache() {
            static std::unordered_map<std::string, UIDocument> cache;
            return cache;
        }
    }

    const UIDocument& UIDocumentLoader::Load(const std::string& path) {
        auto it = Cache().find(path);
        if (it != Cache().end())
            return it->second;
        return Cache().emplace(path, UIDocument::Load(path)).first->second;
    }

    const UIDocument& UIDocumentLoader::Reload(const std::string& path) {
        Cache()[path] = UIDocument::Load(path);
        return Cache()[path];
    }

}
