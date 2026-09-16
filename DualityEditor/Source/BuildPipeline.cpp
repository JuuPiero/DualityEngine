#include "DualityEditor/BuildPipeline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_set>
#include <vector>

// The cooker needs CPU pixels only when an artist supplied a texture larger
// than the 3DS can address. Give this translation unit its own static stb
// implementation so it does not export symbols that collide with the desktop
// OpenGL texture loader's stb implementation.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <nlohmann/json.hpp>

#include "DualityEditor/ScriptEngine.h"
#include "DualityEditor/ModelImporter.h"
#include "DualityEngine/Asset/AssetDatabase.h"
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

        std::string PathKey(const std::filesystem::path& path) {
            std::error_code error;
            const std::filesystem::path absolute = std::filesystem::absolute(path, error);
            return (error ? path.lexically_normal() : absolute.lexically_normal()).generic_string();
        }

        constexpr int N3DSTextureMaxDimension = 1024;

        bool WriteRgbaBmp(const std::filesystem::path& path, int width, int height,
            const std::vector<unsigned char>& rgbaPixels) {
            if (width <= 0 || height <= 0 || rgbaPixels.size() != static_cast<size_t>(width) * height * 4)
                return false;

            const uint32_t pixelBytes = static_cast<uint32_t>(rgbaPixels.size());
            const uint32_t fileBytes = 14u + 40u + pixelBytes;
            std::ofstream output(path, std::ios::binary);
            if (!output)
                return false;
            const auto write16 = [&output](uint16_t value) { output.put(static_cast<char>(value & 0xff)); output.put(static_cast<char>((value >> 8) & 0xff)); };
            const auto write32 = [&output](uint32_t value) {
                output.put(static_cast<char>(value & 0xff)); output.put(static_cast<char>((value >> 8) & 0xff));
                output.put(static_cast<char>((value >> 16) & 0xff)); output.put(static_cast<char>((value >> 24) & 0xff));
            };
            output.put('B'); output.put('M'); write32(fileBytes); write16(0); write16(0); write32(54);
            write32(40); write32(static_cast<uint32_t>(width)); write32(static_cast<uint32_t>(height));
            write16(1); write16(32); write32(0); write32(pixelBytes); write32(0); write32(0); write32(0); write32(0);
            for (int y = height - 1; y >= 0; --y) {
                for (int x = 0; x < width; ++x) {
                    const unsigned char* pixel = &rgbaPixels[(static_cast<size_t>(y) * width + x) * 4];
                    output.put(static_cast<char>(pixel[2])); output.put(static_cast<char>(pixel[1]));
                    output.put(static_cast<char>(pixel[0])); output.put(static_cast<char>(pixel[3]));
                }
            }
            return static_cast<bool>(output);
        }

        // Returns the source unchanged when it is 3DS-safe. Oversized source images are
        // resampled with bilinear filtering to fit the 1024px hardware limit, preserving
        // their aspect ratio. The temporary BMP lives beside the cooked .t3x and is removed
        // immediately after tex3ds runs; Assets/ is never modified.
        bool MakeTex3dsInput(const std::filesystem::path& sourcePath, const std::filesystem::path& cookedPath,
            std::filesystem::path& inputPath, bool& wasResized) {
            int sourceWidth = 0, sourceHeight = 0, channels = 0;
            if (!stbi_info(sourcePath.string().c_str(), &sourceWidth, &sourceHeight, &channels) || sourceWidth <= 0 || sourceHeight <= 0)
                return false;
            if (sourceWidth <= N3DSTextureMaxDimension && sourceHeight <= N3DSTextureMaxDimension) {
                inputPath = sourcePath;
                wasResized = false;
                return true;
            }

            const float scale = std::min(static_cast<float>(N3DSTextureMaxDimension) / sourceWidth,
                static_cast<float>(N3DSTextureMaxDimension) / sourceHeight);
            const int targetWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
            const int targetHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
            stbi_uc* decoded = stbi_load(sourcePath.string().c_str(), &sourceWidth, &sourceHeight, &channels, 4);
            if (!decoded)
                return false;
            std::vector<unsigned char> resized(static_cast<size_t>(targetWidth) * targetHeight * 4);
            for (int y = 0; y < targetHeight; ++y) {
                const float sampleY = (static_cast<float>(y) + 0.5f) * sourceHeight / targetHeight - 0.5f;
                const int y0 = std::clamp(static_cast<int>(std::floor(sampleY)), 0, sourceHeight - 1);
                const int y1 = std::min(y0 + 1, sourceHeight - 1);
                const float fy = std::clamp(sampleY - std::floor(sampleY), 0.0f, 1.0f);
                for (int x = 0; x < targetWidth; ++x) {
                    const float sampleX = (static_cast<float>(x) + 0.5f) * sourceWidth / targetWidth - 0.5f;
                    const int x0 = std::clamp(static_cast<int>(std::floor(sampleX)), 0, sourceWidth - 1);
                    const int x1 = std::min(x0 + 1, sourceWidth - 1);
                    const float fx = std::clamp(sampleX - std::floor(sampleX), 0.0f, 1.0f);
                    for (int channel = 0; channel < 4; ++channel) {
                        const float top = decoded[(static_cast<size_t>(y0) * sourceWidth + x0) * 4 + channel] * (1.0f - fx) + decoded[(static_cast<size_t>(y0) * sourceWidth + x1) * 4 + channel] * fx;
                        const float bottom = decoded[(static_cast<size_t>(y1) * sourceWidth + x0) * 4 + channel] * (1.0f - fx) + decoded[(static_cast<size_t>(y1) * sourceWidth + x1) * 4 + channel] * fx;
                        resized[(static_cast<size_t>(y) * targetWidth + x) * 4 + channel] = static_cast<unsigned char>(std::lround(top * (1.0f - fy) + bottom * fy));
                    }
                }
            }
            stbi_image_free(decoded);
            inputPath = cookedPath;
            inputPath += ".cook.bmp";
            wasResized = WriteRgbaBmp(inputPath, targetWidth, targetHeight, resized);
            return wasResized;
        }

        bool IsPathInside(const std::filesystem::path& path, const std::filesystem::path& root) {
            std::error_code error;
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, error);
            if (error)
                return false;
            const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, error);
            if (error)
                return false;
            const std::filesystem::path relative = canonicalPath.lexically_relative(canonicalRoot);
            return !relative.empty() && *relative.begin() != "..";
        }

        bool IsAssetGuid(const std::string& value) {
            return value.size() == 32 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
                return std::isxdigit(c) != 0;
            });
        }

        void CollectAssetGuids(const json& value, std::unordered_set<std::string>& found) {
            if (value.is_string()) {
                const std::string& candidate = value.get_ref<const std::string&>();
                if (IsAssetGuid(candidate))
                    found.insert(candidate);
            } else if (value.is_array()) {
                for (const json& item : value)
                    CollectAssetGuids(item, found);
            } else if (value.is_object()) {
                for (const auto& item : value.items())
                    CollectAssetGuids(item.value(), found);
            }
        }

        void CollectJsonAssetGuids(const std::filesystem::path& path, std::unordered_set<std::string>& found) {
            std::ifstream file(path);
            if (!file.is_open())
                return;
            try {
                json root;
                file >> root;
                CollectAssetGuids(root, found);
            } catch (const json::exception&) {
                // Binary/non-JSON assets cannot have serialized AssetRef dependencies.
            }
        }

        std::unordered_set<std::string> CollectReachableAssetPaths(const std::filesystem::path& assetsDir,
            const std::string& mainScenePath, bool& discoverySucceeded) {
            discoverySucceeded = false;
            AssetDatabase::Refresh(assetsDir.string());

            std::vector<std::filesystem::path> rootScenes;
            if (auto project = Project::GetActive(); project && !project->GetConfig().ScenesInBuild.empty()) {
                for (const std::string& scenePath : project->GetConfig().ScenesInBuild)
                    rootScenes.push_back(assetsDir / scenePath);
                // StartScene is intentionally allowed to be selected before being added to the
                // optional build list. It must still be a dependency root because it is what the
                // player boots, even in that configuration.
                const std::filesystem::path startScene = assetsDir / project->GetStartScenePath();
                if (std::find(rootScenes.begin(), rootScenes.end(), startScene) == rootScenes.end())
                    rootScenes.push_back(startScene);
            } else if (!mainScenePath.empty()) {
                rootScenes.emplace_back(mainScenePath);
            }

            std::unordered_set<std::string> requiredPaths;
            std::unordered_set<std::string> discoveredGuids;
            std::deque<std::string> pendingGuids;
            for (const std::filesystem::path& scenePath : rootScenes) {
                if (!std::filesystem::is_regular_file(scenePath) || !IsPathInside(scenePath, assetsDir)) {
                    Log::Warn("BuildPipeline: build scene is missing or outside Assets: '" + scenePath.string() + "'");
                    continue;
                }
                requiredPaths.insert(PathKey(scenePath));
                CollectJsonAssetGuids(scenePath, discoveredGuids);
                discoverySucceeded = true;
            }
            for (const std::string& guid : discoveredGuids)
                pendingGuids.push_back(guid);

            std::unordered_set<std::string> visitedGuids;
            while (!pendingGuids.empty()) {
                const std::string guid = pendingGuids.front();
                pendingGuids.pop_front();
                if (!visitedGuids.insert(guid).second)
                    continue;

                const std::string resolved = AssetDatabase::ResolvePath(guid);
                if (resolved.empty()) {
                    Log::Warn("BuildPipeline: referenced asset GUID '" + guid + "' was not found under Assets");
                    continue;
                }
                const std::filesystem::path path(resolved);
                if (!IsPathInside(path, assetsDir))
                    continue;
                requiredPaths.insert(PathKey(path));

                std::unordered_set<std::string> nestedGuids;
                CollectJsonAssetGuids(path, nestedGuids);
                for (const std::string& nestedGuid : nestedGuids)
                    if (!visitedGuids.count(nestedGuid))
                        pendingGuids.push_back(nestedGuid);
            }
            return requiredPaths;
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

    bool BuildPipeline::CookAssets(const std::string& repoRoot, const std::string& assetsDirectory,
        const std::string& mainScenePath) {
        namespace fs = std::filesystem;

        fs::path assetsDir(assetsDirectory);
        if (!fs::exists(assetsDir)) {
            Log::Warn("BuildPipeline: assets directory '" + assetsDirectory + "' does not exist, skipping asset cook");
            return true; // a project with no assets yet is valid, not a failure
        }

        // Ensure source-model children exist before resolving scene AssetRefs for the
        // dependency cook. This also covers a project built before its Content Browser has
        // ever shown the FBX/glTF file.
        for (const auto& entry : fs::recursive_directory_iterator(assetsDir, fs::directory_options::skip_permission_denied)) {
            std::error_code importError;
            if (entry.is_regular_file(importError) && !importError && ModelImporter::IsSupported(entry.path()))
                ModelImporter::Inspect(entry.path());
        }

        bool dependencyDiscoverySucceeded = false;
        const std::unordered_set<std::string> reachableAssetPaths =
            CollectReachableAssetPaths(assetsDir, mainScenePath, dependencyDiscoverySucceeded);
        if (!dependencyDiscoverySucceeded) {
            Log::Warn("BuildPipeline: could not read a build scene; falling back to cooking all assets");
        } else {
            Log::Info("BuildPipeline: dependency cook selected " + std::to_string(reachableAssetPaths.size()) +
                " reachable asset(s)");
        }
        const auto shouldCook = [&](const fs::path& path) {
            return !dependencyDiscoverySucceeded || reachableAssetPaths.count(PathKey(path)) != 0;
        };

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

        // tex3ds accepts JPEG as well as PNG. Both must be converted to .t3x:
        // copying a JPEG unchanged into romfs makes the manifest point at a file
        // Citro2D/Citro3D cannot load. Keep this extension check case-insensitive
        // because assets copied from cameras/art tools commonly use .JPG.
        const auto isTextureSource = [](const fs::path& path) {
            std::string extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
        };

        // tex3ds prints "Used lz11 for compression" once for every texture it
        // processes. A project with a sprite catalogue can therefore look frozen
        // in the console even though it is simply moving through a finite batch.
        // Count it first so the Editor can report meaningful progress instead.
        size_t totalTextures = 0;
        for (fs::recursive_directory_iterator it(assetsDir, fs::directory_options::skip_permission_denied), end;
             it != end; ++it) {
            std::error_code typeError;
            if (it->is_regular_file(typeError) && isTextureSource(it->path()) && shouldCook(it->path()))
                ++totalTextures;
        }
        size_t cookedTextures = 0;

        for (auto& entry : fs::recursive_directory_iterator(assetsDir, fs::directory_options::skip_permission_denied)) {
            std::error_code typeError;
            if (!entry.is_regular_file(typeError))
                continue;
            const fs::path& srcPath = entry.path();
            if (srcPath.extension() == ".meta")
                continue;

            if (!shouldCook(srcPath))
                continue;

            fs::path relPath = fs::relative(srcPath, assetsDir);

            // Reused as-is -- creates the .meta if missing, exactly like ContentBrowserPanel
            // does when browsing into a folder, so a texture never used in the Editor yet
            // still gets a stable guid to cook against.
            std::string guid = AssetMeta::EnsureMetaFile(srcPath);
            if (guid.empty())
                continue;

            std::string romfsPath;

            if (isTextureSource(srcPath)) {
                ++cookedTextures;
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
                Log::Info("BuildPipeline: cooking texture [" + std::to_string(cookedTextures) + "/" +
                    std::to_string(totalTextures) + "] " + relPath.generic_string());
                fs::path tex3dsInput;
                bool textureWasResized = false;
                if (!MakeTex3dsInput(srcPath, destPath, tex3dsInput, textureWasResized)) {
                    Log::Warn("BuildPipeline: could not decode texture '" + srcPath.string() + "', skipping");
                    continue;
                }
                if (textureWasResized) {
                    Log::Info("BuildPipeline: resized oversized 3DS texture '" + relPath.generic_string() +
                        "' to fit " + std::to_string(N3DSTextureMaxDimension) + "px (source asset unchanged)");
                }
                // Keep tex3ds's per-file LZ11 success chatter out of the visible
                // build console. stderr remains visible for real converter errors;
                // the explicit progress log above says exactly which source file
                // is being handled.
                std::string command = "\"" + tex3dsExe + "\"" + mipmapFlag + " -o \"" + destPath.string() + "\" \"" + tex3dsInput.string() + "\" > NUL";
                const int tex3dsResult = RunCommand(command);
                if (textureWasResized)
                    fs::remove(tex3dsInput, ec);
                if (tex3dsResult != 0) {
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

        // The explicit Start Scene setting wins over the scene currently open in the Editor. This
        // lets a developer edit Level2 while reliably building MainMenu as the boot scene.
        // GetStartScenePath also preserves old .dproj files: build-list first entry, then
        // Assets/Scene.scene are the compatibility fallbacks.
        std::string mainScenePath = sceneJsonPath;
        if (activeProject)
            mainScenePath = assetsDirectory + "/" + activeProject->GetStartScenePath();

        std::string sceneDest = repoRoot + "\\DualityPlayer\\romfs\\Scene.scene";
        if (!CopyFileA(mainScenePath.c_str(), sceneDest.c_str(), FALSE)) {
            Log::Error("BuildPipeline: could not copy scene to '" + sceneDest + "'");
            return false;
        }

        if (!CookAssets(repoRoot, assetsDirectory, mainScenePath))
            return false;

        // The .dproj itself is an Editor-side file and is never present in romfs. Bake only the
        // device-relevant build option into a tiny runtime config for DualityPlayer to consume
        // before it creates its render targets.
        json runtimeBuildSettings;
        runtimeBuildSettings["N3DSAntiAliasing"] = activeProject ? activeProject->GetConfig().N3DSAntiAliasing : 0;
        runtimeBuildSettings["ShadowTechnique"] = activeProject
            ? static_cast<int>(activeProject->GetConfig().ShadowTechnique)
            : static_cast<int>(ShadowMode::BlobShadows);
        runtimeBuildSettings["PPU"] = activeProject ? activeProject->GetConfig().PPU : 100.0f;
        runtimeBuildSettings["Gravity"] = activeProject ? activeProject->GetConfig().Gravity : 9.81f;
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
        std::string projectPackagesDir;
        std::string enabledPackages;
        std::string scriptingDefines;
        // Same absolute + forward-slash resolution as projectScriptsDir above, and for the same
        // reason -- ProjectSettingsPanel's file dialog already returns an absolute path, so this
        // is a safe no-op there; it only matters if IconPath is ever set some other way.
        std::string projectIconPath;
        // ProductName (Project Settings) if set, else this project's own Name -- unlike
        // IconPath, an unset ProductName should never fall back to the engine's own placeholder
        // name, so the resolution happens here rather than in build-3ds.bat/CMakeLists.txt.
        std::string productName;
        if (activeProject) {
            projectScriptsDir = std::filesystem::absolute(activeProject->GetScriptsDirectory()).generic_string();
            projectPackagesDir = std::filesystem::absolute(activeProject->GetPackagesDirectory()).generic_string();
            enabledPackages = activeProject->GetEnabledPackagesCsv();
            scriptingDefines = activeProject->GetScriptingDefinesCsv();
            if (!activeProject->GetConfig().IconPath.empty())
                projectIconPath = std::filesystem::absolute(activeProject->GetConfig().IconPath).generic_string();
            productName = activeProject->GetConfig().ProductName.empty()
                ? activeProject->GetConfig().Name
                : activeProject->GetConfig().ProductName;
        }
        std::string command = "\"" + repoRoot + "\\build-3ds.bat\" \"" + projectScriptsDir + "\" \"" + projectIconPath + "\" \"" + productName +
            "\" \"" + projectPackagesDir + "\" \"" + enabledPackages + "\" \"" + scriptingDefines + "\"";
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
