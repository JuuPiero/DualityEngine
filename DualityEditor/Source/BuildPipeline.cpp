#include "DualityEditor/BuildPipeline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "DualityEngine/Asset/AssetMeta.h"
#include "DualityEngine/Core/Log.h"

using json = nlohmann::json;

namespace Duality {

    std::atomic<BuildStatus> BuildPipeline::s_Status{ BuildStatus::Idle };

    // Hardcoded, matching build-3ds.bat's own established precedent (see that file's DKP_WIN
    // comment) of not trusting DEVKITPRO's env-var format for a direct Windows tool path.
    static const std::string s_Tex3dsExe = "E:\\App\\devkitPro\\tools\\bin\\tex3ds.exe";

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
    static int RunCommand(const std::string& command) {
        return std::system(("\"" + command + "\"").c_str());
    }

    bool BuildPipeline::CookAssets(const std::string& repoRoot, const std::string& assetsDirectory) {
        namespace fs = std::filesystem;

        fs::path assetsDir(assetsDirectory);
        if (!fs::exists(assetsDir)) {
            Log::Warn("BuildPipeline: assets directory '" + assetsDirectory + "' does not exist, skipping asset cook");
            return true; // a project with no assets yet is valid, not a failure
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

            // Reused as-is -- creates the .meta if missing, exactly like ContentBrowserPanel
            // does when browsing into a folder, so a texture never used in the Editor yet
            // still gets a stable guid to cook against.
            std::string guid = AssetMeta::EnsureMetaFile(srcPath);
            if (guid.empty())
                continue;

            fs::path relPath = fs::relative(srcPath, assetsDir);
            std::string romfsPath;

            if (srcPath.extension() == ".png") {
                fs::path relT3x = relPath;
                relT3x.replace_extension(".t3x");
                fs::path destPath = romfsAssetsDir / relT3x;
                fs::create_directories(destPath.parent_path(), ec);

                std::string command = "\"" + s_Tex3dsExe + "\" -o \"" + destPath.string() + "\" \"" + srcPath.string() + "\"";
                if (RunCommand(command) != 0) {
                    Log::Warn("BuildPipeline: tex3ds failed converting '" + srcPath.string() + "', skipping");
                    continue;
                }
                romfsPath = "romfs:/Assets/" + relT3x.generic_string();
            } else {
                fs::path destPath = romfsAssetsDir / relPath;
                fs::create_directories(destPath.parent_path(), ec);
                if (!CopyFileA(srcPath.string().c_str(), destPath.string().c_str(), FALSE)) {
                    Log::Warn("BuildPipeline: could not copy '" + srcPath.string() + "' into romfs, skipping");
                    continue;
                }
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
        std::string sceneDest = repoRoot + "\\DualityPlayer\\romfs\\Scene.json";
        if (!CopyFileA(sceneJsonPath.c_str(), sceneDest.c_str(), FALSE)) {
            Log::Error("BuildPipeline: could not copy scene to '" + sceneDest + "'");
            return false;
        }

        // The scene's own Assets/ directory sits right next to Scene.json -- deriving it here
        // avoids threading a new parameter through BuildFor3DSAsync/MenuBarPanel just for this.
        std::string assetsDirectory = std::filesystem::path(sceneJsonPath).parent_path().string();
        if (!CookAssets(repoRoot, assetsDirectory))
            return false;

        Log::Info("BuildPipeline: building for 3DS (clean build, can take up to a minute)...");

        // Delegates to build-3ds.bat (a real, separately-parsed script file) rather than
        // hand-building a chained cmd.exe command string here -- keeps exactly one place
        // (build-3ds.bat) that knows how to configure/build the 3DS target, so everything a
        // plain double-click of that script does is exactly what this button does. Routed
        // through RunCommand (see its own comment above) for the same cmd.exe /c quoting
        // reason as CookAssets's tex3ds invocation, even though this particular command only
        // has one quoted argument -- consistent handling beats relying on which specific
        // shape of command cmd.exe happens to preserve unwrapped.
        std::string command = "\"" + repoRoot + "\\build-3ds.bat\"";
        if (RunCommand(command) != 0) {
            Log::Error("BuildPipeline: 3DS build failed");
            return false;
        }

        Log::Info("BuildPipeline: built " + repoRoot + "\\build-3ds\\DualityPlayer\\DualityPlayer.3dsx (+ .cia if makerom/bannertool are present under Tools/)");
        return true;
    }

}
