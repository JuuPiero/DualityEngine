#pragma once

#include <deque>
#include <string>

#include "DualityEngine/Scripting/ScriptContext.h"

namespace Duality {

    // The loading behavior matches Unity's LoadSceneMode naming. Single replaces every
    // currently loaded scene; Additive keeps them alive and renders/updates the newly-loaded
    // scene after the already-loaded scenes. It intentionally lives above the concrete Scene
    // object: scripts request work here, while the host performs it after every scene has
    // returned from OnRuntimeUpdate. Destroying a Scene in the middle of one of its script
    // callbacks would invalidate the current Behaviour and is therefore unsafe.
    enum class LoadSceneMode {
        Single,
        Additive,
    };

    enum class SceneRequestType {
        Load,
        Unload,
    };

    struct SceneRequest {
        SceneRequestType Type = SceneRequestType::Load;
        std::string Path;
        LoadSceneMode Mode = LoadSceneMode::Single;
    };

    // Deferred request queue shared by scripts and the owning runtime loop. A queue (rather
    // than the former one-slot flag) is important for additive loading: several systems may
    // request different overlay scenes during the same frame without silently losing one.
    class SceneManager {
    public:
        // `assetsRelativePath` is resolved by each caller against that platform's own
        // Assets root (project->GetAssetsDirectory() on desktop, "romfs:/Assets/" on 3DS) --
        // SceneManager itself has no filesystem/Scene knowledge, purely a request mailbox.
        static void RequestLoadScene(const std::string& assetsRelativePath, LoadSceneMode mode = LoadSceneMode::Single) {
            if (assetsRelativePath.empty())
                return;
            // GameScripts is a separate DLL on desktop. Forward through the host-owned ABI
            // service while inside a Behaviour callback so the request does not land in a
            // DLL-local copy of this header's static queue.
            if (const EngineServices* services = ScriptContext::Services(); services && services->RequestLoadScene) {
                services->RequestLoadScene(assetsRelativePath.c_str(), static_cast<int>(mode));
                return;
            }
            EnqueueLoadRequest(assetsRelativePath, mode);
        }

        // Readable convenience equivalent to RequestLoadScene(path, LoadSceneMode::Additive).
        static void RequestLoadSceneAdditive(const std::string& assetsRelativePath) {
            RequestLoadScene(assetsRelativePath, LoadSceneMode::Additive);
        }

        // An additive scene is identified by its Assets-relative path. Hosts keep their base
        // scene loaded and ignore an unload request for that base scene, so gameplay cannot
        // accidentally end up with no active Scene.
        static void RequestUnloadScene(const std::string& assetsRelativePath) {
            if (assetsRelativePath.empty())
                return;
            if (const EngineServices* services = ScriptContext::Services(); services && services->RequestUnloadScene) {
                services->RequestUnloadScene(assetsRelativePath.c_str());
                return;
            }
            EnqueueUnloadRequest(assetsRelativePath);
        }

        // Host-side ABI adapters call these to bypass ScriptContext and append directly to the
        // one queue the real runtime loop consumes. They are public only because EngineServices
        // is an ABI bridge; gameplay code should use RequestLoadScene/RequestUnloadScene.
        static void EnqueueLoadRequest(const std::string& assetsRelativePath, LoadSceneMode mode = LoadSceneMode::Single) {
            if (!assetsRelativePath.empty())
                s_PendingRequests.push_back({ SceneRequestType::Load, assetsRelativePath, mode });
        }
        static void EnqueueUnloadRequest(const std::string& assetsRelativePath) {
            if (!assetsRelativePath.empty())
                s_PendingRequests.push_back({ SceneRequestType::Unload, assetsRelativePath, LoadSceneMode::Additive });
        }

        static bool HasPendingRequests() { return !s_PendingRequests.empty(); }

        // Consumes requests in issuance order. Hosts must call this only after all active
        // scenes finished their update for the frame.
        static std::deque<SceneRequest> ConsumePendingRequests() {
            std::deque<SceneRequest> requests;
            requests.swap(s_PendingRequests);
            return requests;
        }

        // Compatibility aliases for existing games that used the original one-slot API.
        // ConsumePendingLoad returns the next request path (irrespective of mode); new hosts
        // should consume the typed queue above so Additive/Unload semantics are retained.
        static bool HasPendingLoad() { return HasPendingRequests(); }

        // Clears the pending flag and returns the requested path -- call exactly once per
        // frame, after checking HasPendingLoad(), right before actually performing the
        // Stop/swap/Deserialize/Start dance.
        static std::string ConsumePendingLoad() {
            if (s_PendingRequests.empty())
                return {};
            std::string path = s_PendingRequests.front().Path;
            s_PendingRequests.pop_front();
            return path;
        }

        // Useful to a host when it leaves Play mode and to deterministic tests. This cancels
        // requests only; it never unloads or mutates a live Scene itself.
        static void ClearPendingRequests() { s_PendingRequests.clear(); }

    private:
        static inline std::deque<SceneRequest> s_PendingRequests;
    };

}
