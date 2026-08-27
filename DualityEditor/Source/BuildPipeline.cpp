#include "DualityEditor/BuildPipeline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Asset/AudioImportSettings.h"
#include "DualityEngine/Asset/TextureImportSettings.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"

using json = nlohmann::json;

namespace Duality {

    std::atomic<BuildStatus> BuildPipeline::s_Status{ BuildStatus::Idle };

    namespace {
        // Same 3-step resolution order as devkitpro-path.bat's own DKP_WIN lookup (proven
        // reliable on this project already), reimplemented in C++ since this runs from inside
        // the Editor process rather than a batch script:
        //   1. DEVKITPRO, if it resolves to a real Windows path -- NOT always true: devkitPro's
        //      own bundled MSYS2 exports it as a POSIX path like "/opt/devkitpro", which only
        //      resolves inside that shell's own mount table and is meaningless to a plain
        //      Windows process like this Editor (confirmed empirically: a normal
        //      GetEnvironmentVariableA call from DualityEditor.exe never even sees DEVKITPRO at
        //      all unless it was launched from inside an MSYS2 shell -- Windows-level processes
        //      don't inherit MSYS2's own shell-profile-only env vars).
        //   2. The registry entry devkitProUpdater writes on install (HKLM\...\Uninstall\
        //      devkitProUpdater, value InstallLocation) -- works regardless of DEVKITPRO's
        //      format or the drive/folder chosen, checking both the WOW6432Node and native
        //      registry views since a 32-bit installer can write to either depending on how it
        //      was built.
        //   3. The documented default install path, C:\devkitPro.
        // Returns an empty string (not a hardcoded fallback) if none of these pan out -- callers
        // must handle that explicitly rather than silently trying a path that doesn't exist on
        // this machine.
        std::string FindDevkitProInstallDir() {
            char envValue[MAX_PATH]{};
            DWORD envLen = GetEnvironmentVariableA("DEVKITPRO", envValue, sizeof(envValue));
            if (envLen > 0 && envLen < sizeof(envValue) && std::filesystem::exists(std::string(envValue) + "\\devkitARM"))
                return envValue;

            for (HKEY root : { HKEY_LOCAL_MACHINE }) {
                for (const char* subKey : {
                    "SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\devkitProUpdater",
                    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\devkitProUpdater" }) {
                    HKEY key;
                    if (RegOpenKeyExA(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
                        continue;
                    char value[MAX_PATH]{};
                    DWORD size = sizeof(value);
                    DWORD type = 0;
                    LONG result = RegQueryValueExA(key, "InstallLocation", nullptr, &type, reinterpret_cast<BYTE*>(value), &size);
                    RegCloseKey(key);
                    if (result == ERROR_SUCCESS && type == REG_SZ)
                        return value;
                }
            }

            if (std::filesystem::exists("C:\\devkitPro\\devkitARM"))
                return "C:\\devkitPro";

            return "";
        }

        // Resolved once per CookAssets call (not cached across calls) -- cheap (one env lookup
        // + a couple registry reads in the common case), and re-checking every time means a
        // devkitPro reinstall/move is picked up without restarting the Editor.
        std::string FindTex3dsExe() {
            std::string devkitProDir = FindDevkitProInstallDir();
            if (devkitProDir.empty())
                return "";
            std::string exePath = devkitProDir + "\\tools\\bin\\tex3ds.exe";
            return std::filesystem::exists(exePath) ? exePath : "";
        }
    }

    // std::system() on Windows runs `cmd.exe /c "<command>"` -- and cmd.exe's own documented
    // `/c` parsing (see `cmd /?`) only PRESERVES that outer quote pair when the command has
    // *exactly two* quote characters total and the quoted text is a bare executable path (this
    // is exactly BuildFor3DS's own "\"" + repoRoot + "\\build-3ds.bat\"" call below -- one
    // quoted path, nothing else). Any other shape -- e.g. a command with several quoted
    // arguments, like tex3ds's `"exe" -o "dest" "src"` -- falls through to cmd's *old*
    // behavior instead: strip only the leading and trailing quote character and run whatever
    // is left verbatim, which corrupts a multi-argument command (confirmed empirically: it
    // silently mangled the executable name here, making tex3ds "fail" with no useful error).
    // The standard fix is to wrap the whole already-correct command in one MORE outer quote
    // pair -- cmd's stripping then removes exactly that synthetic layer, leaving the real
    // command intact underneath.
    int BuildPipeline::RunCommand(const std::string& command) {
        return std::system(("\"" + command + "\"").c_str());
    }

    bool BuildPipeline::CookAssets(const std::string& repoRoot, const std::string& assetsDirectory) {
        namespace fs = std::filesystem;

        fs::path assetsDir(assetsDirectory);
        if (!fs::exists(assetsDir)) {
            Log::Warn("BuildPipeline: assets directory '" + assetsDirectory + "' does not exist, skipping asset cook");
            return true; // a project with no assets yet is valid, not a failure
        }

        std::string tex3dsExe = FindTex3dsExe();
        if (tex3dsExe.empty()) {
            Log::Warn("BuildPipeline: could not locate tex3ds.exe under a devkitPro install (checked "
                "DEVKITPRO, the devkitProUpdater registry entry, and C:\\devkitPro) -- textures will be "
                "skipped in this cook, everything else still copies normally");
        }

        fs::path romfsAssetsDir = fs::path(repoRoot) / "DualityPlayer" / "romfs" / "Assets";
        std::error_code ec;
        fs::create_directories(romfsAssetsDir, ec);
        if (ec) {
            Log::Error("BuildPipeline: could not create '" + romfsAssetsDir.string() + "': " + ec.message());
            return false;
        }

        json manifest = json::object();

        for (auto& entry : fs::recursive_directory_iterator(assetsDir)) {
            if (entry.is_directory())
                continue;
            const fs::path& srcPath = entry.path();
            if (srcPath.extension() == ".meta")
                continue;

            fs::path relPath = fs::relative(srcPath, assetsDir);

            // Build Settings filtering: once a project has configured ScenesInBuild (Build
            // Settings window), a .scene file NOT in that list is excluded from the device build
            // entirely -- matches Unity, where only scenes explicitly added to Build Settings
            // ship. An empty ScenesInBuild means "not configured yet", which preserves this
            // function's original "cook every asset unfiltered" behavior exactly -- no back-compat
            // break for a project that hasn't touched Build Settings.
            if (srcPath.extension() == ".scene") {
                auto activeProject = Project::GetActive();
                if (activeProject && !activeProject->GetConfig().ScenesInBuild.empty()) {
                    const auto& scenesInBuild = activeProject->GetConfig().ScenesInBuild;
                    std::string relPathStr = relPath.generic_string();
                    if (std::find(scenesInBuild.begin(), scenesInBuild.end(), relPathStr) == scenesInBuild.end())
                        continue;
                }
            }

            // Reused as-is -- creates the .meta if missing, exactly like ContentBrowserPanel
            // does when browsing into a folder, so a texture never used in the Editor yet
            // still gets a stable guid to cook against.
            std::string guid = AssetMeta::EnsureMetaFile(srcPath);
            if (guid.empty())
                continue;

            std::string romfsPath;

            if (srcPath.extension() == ".png") {
                if (tex3dsExe.empty())
                    continue; // already warned once, above, before the loop started

                fs::path relT3x = relPath;
                relT3x.replace_extension(".t3x");
                fs::path destPath = romfsAssetsDir / relT3x;
                fs::create_directories(destPath.parent_path(), ec);

                // GenerateMipmaps is baked into the .t3x itself at cook time (tex3ds's -m flag
                // is opt-in, confirmed via `tex3ds --help` -- omitted entirely by default, so
                // this only changes behavior for a texture that actually asked for mipmaps).
                TextureImportSettings settings = TextureImportSettings::Load(srcPath.string());
                std::string mipmapFlag = settings.GenerateMipmaps ? " -m box" : "";
                std::string command = "\"" + tex3dsExe + "\"" + mipmapFlag + " -o \"" + destPath.string() + "\" \"" + srcPath.string() + "\"";
                if (RunCommand(command) != 0) {
                    Log::Warn("BuildPipeline: tex3ds failed converting '" + srcPath.string() + "', skipping");
                    continue;
                }

                // FilterMode/WrapMode are GPU sampler state applied at runtime, not baked into
                // the .t3x data -- Citro2DRenderer::LoadTexture only ever sees this cooked romfs
                // path, with no way back to the original Assets/ ".meta" to look them up
                // on-device. Cooking a settings-only ".meta" (no "guid", just the "Importer"
                // block) right next to the .t3x lets LoadTexture call the exact same
                // TextureImportSettings::Load(path) desktop already uses -- it just appends
                // ".meta" to whatever path it's given, so this is a real ".meta" file as far as
                // that code is concerned, not a special-cased format.
                TextureImportSettings::Save(destPath.string(), settings);

                romfsPath = "romfs:/Assets/" + relT3x.generic_string();
            } else {
                fs::path destPath = romfsAssetsDir / relPath;
                fs::create_directories(destPath.parent_path(), ec);
                if (!CopyFileA(srcPath.string().c_str(), destPath.string().c_str(), FALSE)) {
                    Log::Warn("BuildPipeline: could not copy '" + srcPath.string() + "' into romfs, skipping");
                    continue;
                }

                // Same reasoning as the texture branch above: AudioEngine::Play only ever sees
                // this cooked romfs path, not the original Assets/ ".meta" -- cook a
                // settings-only sidecar next to it so AudioImportSettings::Load(path) (the exact
                // same call the desktop build makes) finds it on-device too.
                if (srcPath.extension() == ".wav")
                    AudioImportSettings::Save(destPath.string(), AudioImportSettings::Load(srcPath.string()));

                romfsPath = "romfs:/Assets/" + relPath.generic_string();
            }

            manifest[guid] = romfsPath;
        }

        fs::path manifestPath = fs::path(repoRoot) / "DualityPlayer" / "romfs" / "AssetManifest.json";
        std::ofstream manifestFile(manifestPath);
        if (!manifestFile.is_open()) {
            Log::Error("BuildPipeline: could not write '" + manifestPath.string() + "'");
            return false;
        }
        manifestFile << manifest.dump(2);

        Log::Info("BuildPipeline: cooked " + std::to_string(manifest.size()) + " asset(s) into romfs");
        return true;
    }

    void BuildPipeline::BuildFor3DSAsync(const std::string& repoRoot, const std::string& sceneJsonPath) {
        if (s_Status == BuildStatus::Running) {
            Log::Warn("BuildPipeline: a build is already in progress");
            return;
        }
        // Mirrors the existing BuildFor3DSAsync/BuildForPCAsync single-build-at-a-time
        // convention (they already share one s_Status even though they target different build
        // trees) -- ScriptEngine::ReloadAsync runs `cmake --build` too, so it's folded into the
        // same one-background-build-task-at-a-time gate rather than letting it overlap silently.
        if (ScriptEngine::GetStatus() == ReloadStatus::Running) {
            Log::Warn("BuildPipeline: a script reload is already in progress, try again once it finishes");
            return;
        }
        s_Status = BuildStatus::Running;
        // Detached, not joined -- MenuBarPanel polls GetStatus() instead of
        // waiting on the thread. Closing the Editor mid-build leaves the
        // build-3ds.bat child process tree to finish or get cleaned up on its
        // own (no job-object-based process tracking here) -- an accepted, rare
        // edge case, not a normal shutdown path.
        std::thread([repoRoot, sceneJsonPath]() {
            bool succeeded = BuildFor3DS(repoRoot, sceneJsonPath);
            s_Status = succeeded ? BuildStatus::Succeeded : BuildStatus::Failed;
        }).detach();
    }

    bool BuildPipeline::BuildFor3DS(const std::string& repoRoot, const std::string& sceneJsonPath) {
        // Always the active project's real Assets/ root, NOT derived from sceneJsonPath's own
        // parent folder -- that used to be the same thing only by coincidence (every scene
        // happened to sit directly in Assets/ so far). A scene in a subfolder (e.g.
        // "Assets/Scenes/Test.scene", a real, valid layout -- see Build Settings' own scene scan)
        // would have derived ".../Assets/Scenes" instead of ".../Assets", silently breaking
        // CookAssets for that project (a real latent bug, found while wiring up Build Settings
        // below, fixed here rather than left for whoever hits it next).
        auto activeProject = Project::GetActive();
        std::string assetsDirectory = activeProject
            ? activeProject->GetAssetsDirectory()
            : std::filesystem::path(sceneJsonPath).parent_path().string(); // no active project -- best-effort fallback

        // Build Settings' own Start Scene (ScenesInBuild[0], see ProjectConfig's own comment)
        // wins once configured -- read straight off disk (NOT ctx.SceneRef/sceneJsonPath's live
        // in-memory state, since the Start Scene may not be whatever's currently open in the
        // Editor). Falls back to sceneJsonPath (today's "package whatever's open" behavior) for a
        // project that hasn't configured Build Settings yet -- empty ScenesInBuild is always a
        // no-op fallback, never an error, matching CookAssets' own filter below.
        std::string mainScenePath = sceneJsonPath;
        if (activeProject && !activeProject->GetConfig().ScenesInBuild.empty())
            mainScenePath = assetsDirectory + "/" + activeProject->GetConfig().ScenesInBuild[0];

        std::string sceneDest = repoRoot + "\\DualityPlayer\\romfs\\Scene.scene";
        if (!CopyFileA(mainScenePath.c_str(), sceneDest.c_str(), FALSE)) {
            Log::Error("BuildPipeline: could not copy scene to '" + sceneDest + "'");
            return false;
        }

        if (!CookAssets(repoRoot, assetsDirectory))
            return false;

        // The .dproj itself is an Editor-side file and is never present in romfs. Bake only the
        // device-relevant build option into a tiny runtime config for DualityPlayer to consume
        // before it creates its render targets.
        json runtimeBuildSettings;
        runtimeBuildSettings["N3DSAntiAliasing"] = activeProject ? activeProject->GetConfig().N3DSAntiAliasing : 0;
        std::ofstream runtimeSettingsFile(std::filesystem::path(repoRoot) / "DualityPlayer" / "romfs" / "BuildSettings.json");
        if (!runtimeSettingsFile.is_open()) {
            Log::Error("BuildPipeline: could not write 3DS runtime build settings");
            return false;
        }
        runtimeSettingsFile << runtimeBuildSettings.dump(2);
        // Close before invoking build-3ds.bat: its ROMFS packaging step immediately
        // reads this file. Keeping the stream alive until BuildFor3DS returns can
        // leave a Windows file handle open for the entire child-build duration.
        runtimeSettingsFile.close();
        if (!runtimeSettingsFile) {
            Log::Error("BuildPipeline: could not finish writing 3DS runtime build settings");
            return false;
        }

        Log::Info("BuildPipeline: building for 3DS (clean build, can take up to a minute)...");

        // Delegates to build-3ds.bat (a real, separately-parsed script file) rather than
        // hand-building a chained cmd.exe command string here -- keeps exactly one place
        // (build-3ds.bat) that knows how to configure/build the 3DS target, so everything a
        // plain double-click of that script does is exactly what this button does. Passes the
        // active project's own Assets/Scripts directory as %1 (build-3ds.bat forwards it to its
        // own cmake configure as -DDUALITY_PROJECT_SCRIPTS_DIR=..., same as ScriptEngine::Reload
        // does for the desktop build -- see GameScripts/CMakeLists.txt) so a project's own
        // scripts compile into the STATIC 3DS-linked GameScripts too. Routed through RunCommand
        // (see its own comment above) for the same cmd.exe /c quoting reason as CookAssets's
        // tex3ds invocation -- this is now a genuine multi-quoted-argument command (script path
        // + scripts dir), exactly the shape RunCommand's wrapping exists to handle correctly.
        // Resolved to absolute + forward-slash here for the exact same reason
        // ScriptEngine::Reload's own copy of this logic is -- see its comment there.
        std::string projectScriptsDir;
        // Same absolute + forward-slash resolution as projectScriptsDir above, and for the same
        // reason -- ProjectSettingsPanel's file dialog already returns an absolute path, so this
        // is a safe no-op there; it only matters if IconPath is ever set some other way.
        std::string projectIconPath;
        if (activeProject) {
            projectScriptsDir = std::filesystem::absolute(activeProject->GetScriptsDirectory()).generic_string();
            if (!activeProject->GetConfig().IconPath.empty())
                projectIconPath = std::filesystem::absolute(activeProject->GetConfig().IconPath).generic_string();
        }
        std::string command = "\"" + repoRoot + "\\build-3ds.bat\" \"" + projectScriptsDir + "\" \"" + projectIconPath + "\"";
        if (RunCommand(command) != 0) {
            Log::Error("BuildPipeline: 3DS build failed");
            return false;
        }

        Log::Info("BuildPipeline: built " + repoRoot + "\\build-3ds\\DualityPlayer\\DualityPlayer.3dsx (+ .cia if makerom/bannertool are present under Tools/)");
        return true;
    }

    bool BuildPipeline::BuildForPC(const std::string& buildDirectory) {
        Log::Info("BuildPipeline: building for PC (DualityPlayerDesktop)...");

        // Plain std::system(), not RunCommand -- this command starts with an unquoted word
        // (`cmake`), not a quoted token, so it never hits the cmd.exe /c quoting quirk
        // RunCommand's own comment describes (that quirk only triggers when the command *itself*
        // begins with a quote character). Same call shape ScriptEngine::Reload already uses for
        // GameScripts, just a different --target.
        std::string command = "cmake --build \"" + buildDirectory + "\" --target DualityPlayerDesktop";
        if (std::system(command.c_str()) != 0) {
            Log::Error("BuildPipeline: PC build failed");
            return false;
        }

        Log::Info("BuildPipeline: built " + buildDirectory + "\\DualityPlayerDesktop\\DualityPlayerDesktop.exe -- run via run-desktop-player.bat");
        return true;
    }

    void BuildPipeline::BuildForPCAsync(const std::string& buildDirectory) {
        if (s_Status == BuildStatus::Running) {
            Log::Warn("BuildPipeline: a build is already in progress");
            return;
        }
        // Same reasoning as BuildFor3DSAsync's check above -- BuildForPC in particular shares
        // the EXACT same Ninja tree ScriptEngine::Reload builds GameScripts in.
        if (ScriptEngine::GetStatus() == ReloadStatus::Running) {
            Log::Warn("BuildPipeline: a script reload is already in progress, try again once it finishes");
            return;
        }
        s_Status = BuildStatus::Running;
        std::thread([buildDirectory]() {
            bool succeeded = BuildForPC(buildDirectory);
            s_Status = succeeded ? BuildStatus::Succeeded : BuildStatus::Failed;
        }).detach();
    }

}
