#pragma once

#include <string>

namespace Duality {

    // Lets a running script request a scene swap (Unity's SceneManager.LoadScene) without
    // needing to own or even see the Scene object itself -- a Behaviour can't safely tear
    // down/replace the very Scene it's executing inside of mid-OnRuntimeUpdate (its own
    // entity, and every other entity/Behaviour instance this frame, would be destroyed out
    // from under the call stack that's still running). So this is a DEFERRED request: a
    // plain static flag+path, polled once per frame by whichever real loop owns the actual
    // Scene value (DualityPlayer::Main.cpp, DualityPlayerDesktop::Main.cpp, and the Editor's
    // own Play-mode tick in Application.cpp) right after that frame's OnRuntimeUpdate
    // returns -- never mid-update. Mirrors ScriptRegistry's own function-local-static-state
    // shape, just for one pending request instead of a whole factory map.
    class SceneManager {
    public:
        // `assetsRelativePath` is resolved by each caller against that platform's own
        // Assets root (project->GetAssetsDirectory() on desktop, "romfs:/Assets/" on 3DS) --
        // SceneManager itself has no filesystem/Scene knowledge, purely a request mailbox.
        static void RequestLoadScene(const std::string& assetsRelativePath) {
            s_HasPending = true;
            s_PendingPath = assetsRelativePath;
        }

        static bool HasPendingLoad() { return s_HasPending; }

        // Clears the pending flag and returns the requested path -- call exactly once per
        // frame, after checking HasPendingLoad(), right before actually performing the
        // Stop/swap/Deserialize/Start dance.
        static std::string ConsumePendingLoad() {
            s_HasPending = false;
            return s_PendingPath;
        }

    private:
        static inline bool s_HasPending = false;
        static inline std::string s_PendingPath;
    };

}
