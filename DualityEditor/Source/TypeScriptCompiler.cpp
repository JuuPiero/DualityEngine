#include "DualityEditor/TypeScriptCompiler.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Scripting/JS/JsScripting.h"

namespace Duality {

    namespace fs = std::filesystem;

    namespace {
        struct BuildResult {
            bool Ok = false;
            std::string Bundle;
            int Files = 0;
            int Bytes = 0;
            int Milliseconds = 0;
            int TypeErrors = 0;
        };

        std::atomic<TsCompileStatus> s_Status{ TsCompileStatus::Idle };
        std::atomic<bool> s_PendingResult{ false };
        std::mutex s_ResultMutex;
        BuildResult s_Result;
        std::string s_ReadyBundle;   // built, waiting for Play to stop before loading
        bool s_RebuildQueued = false;
        float s_WatchTimer = 0.0f;
        std::map<std::string, fs::file_time_type> s_Snapshot;
        std::string s_SnapshotProject;
        bool s_HasScripts = false;

        std::map<std::string, fs::file_time_type> ScanScripts(const std::string& projectDirectory) {
            std::map<std::string, fs::file_time_type> files;
            std::error_code ec;
            const fs::path assets = fs::path(projectDirectory) / "Assets";
            if (!fs::exists(assets, ec))
                return files;
            for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
                if (ec)
                    break;
                const fs::path& path = it->path();
                if (it->is_directory(ec) && path.filename() == "node_modules") {
                    it.disable_recursion_pending();
                    continue;
                }
                const std::string name = path.filename().string();
                if (name.size() > 3 && name.compare(name.size() - 3, 3, ".ts") == 0 &&
                    !(name.size() > 5 && name.compare(name.size() - 5, 5, ".d.ts") == 0))
                    files[path.generic_string()] = fs::last_write_time(path, ec);
            }
            return files;
        }

        // Runs build-scripts.mjs, forwarding its diagnostics to the Console.
        BuildResult RunBuild(const std::string& repoRoot, const std::string& projectDirectory, bool typecheck) {
            BuildResult result;
            const std::string script = (fs::path(repoRoot) / "Tools" / "TypeScript" / "build-scripts.mjs").string();
            if (!fs::exists(script)) {
                Log::Error("[TS] toolchain not found: " + script);
                return result;
            }
            const std::string project = fs::absolute(projectDirectory).string();
            // cmd.exe strips the outermost quote pair, hence the extra quotes around everything.
            std::string command = "\"node \"" + script + "\" \"" + project + "\"" + (typecheck ? " --typecheck" : "") + " 2>&1\"";
            FILE* pipe = _popen(command.c_str(), "r");
            if (!pipe) {
                Log::Error("[TS] could not start node -- install Node.js (https://nodejs.org) and make sure `node` is on PATH");
                return result;
            }
            std::vector<std::string> lines;
            char buffer[4096];
            std::string current;
            while (fgets(buffer, sizeof(buffer), pipe)) {
                current += buffer;
                if (!current.empty() && current.back() == '\n') {
                    while (!current.empty() && (current.back() == '\n' || current.back() == '\r'))
                        current.pop_back();
                    lines.push_back(current);
                    current.clear();
                }
            }
            if (!current.empty())
                lines.push_back(current);
            const int exitCode = _pclose(pipe);

            bool sawResult = false;
            for (const std::string& line : lines) {
                if (line.rfind("DUALITY_TS_RESULT ", 0) == 0) {
                    nlohmann::json json = nlohmann::json::parse(line.substr(18), nullptr, false);
                    if (json.is_object()) {
                        sawResult = true;
                        result.Ok = json.value("ok", false);
                        result.Bundle = json.value("out", std::string());
                        result.Files = json.value("files", 0);
                        result.Bytes = json.value("bytes", 0);
                        result.Milliseconds = json.value("ms", 0);
                        result.TypeErrors = json.value("typeErrors", 0);
                    }
                } else if (line.find(": error: ") != std::string::npos) {
                    Log::Error("[TS] " + line);
                } else if (line.find(": warning: ") != std::string::npos) {
                    Log::Warn("[TS] " + line);
                } else if (!line.empty()) {
                    Log::Info("[TS] " + line);
                }
            }
            if (!sawResult) {
                Log::Error("[TS] TypeScript build did not run (exit code " + std::to_string(exitCode) +
                    ") -- is Node.js installed and `node` on PATH?");
                result.Ok = false;
            }
            return result;
        }

        bool LoadBundle(const std::string& bundle) {
            if (bundle.empty())
                return false;
            return JsScripting::LoadBundle(bundle);
        }
    }

    std::string TypeScriptCompiler::BundlePath(const std::string& projectDirectory) {
        return (fs::path(projectDirectory) / "Library" / "ScriptAssemblies" / "scripts.js").generic_string();
    }

    TsCompileStatus TypeScriptCompiler::GetStatus() { return s_Status; }
    bool TypeScriptCompiler::HasScripts() { return s_HasScripts; }

    bool TypeScriptCompiler::CompileAndLoadSync(const std::string& repoRoot, const std::string& projectDirectory) {
        s_Snapshot = ScanScripts(projectDirectory);
        s_SnapshotProject = projectDirectory;
        s_HasScripts = !s_Snapshot.empty();
        if (!s_HasScripts) {
            JsScripting::Unload(); // a project without TS scripts: no runtime at all
            s_Status = TsCompileStatus::Idle;
            return true;
        }
        s_Status = TsCompileStatus::Running;
        BuildResult result = RunBuild(repoRoot, projectDirectory, false);
        if (!result.Ok || !LoadBundle(result.Bundle)) {
            s_Status = TsCompileStatus::Failed;
            return false;
        }
        Log::Info("[TS] compiled " + std::to_string(result.Files) + " script file(s) in " + std::to_string(result.Milliseconds) + " ms");
        s_Status = TsCompileStatus::Succeeded;
        return true;
    }

    void TypeScriptCompiler::CompileAsync(const std::string& repoRoot, const std::string& projectDirectory) {
        if (s_Status == TsCompileStatus::Running) {
            s_RebuildQueued = true;
            return;
        }
        s_Snapshot = ScanScripts(projectDirectory);
        s_SnapshotProject = projectDirectory;
        s_HasScripts = !s_Snapshot.empty();
        if (!s_HasScripts) {
            if (JsScripting::IsLoaded())
                s_ReadyBundle = "<unload>"; // applied by Update once Play isn't running
            return;
        }
        s_Status = TsCompileStatus::Running;
        std::thread([repoRoot, projectDirectory]() {
            BuildResult result = RunBuild(repoRoot, projectDirectory, true);
            {
                std::lock_guard<std::mutex> lock(s_ResultMutex);
                s_Result = result;
            }
            s_PendingResult = true;
        }).detach();
    }

    void TypeScriptCompiler::Update(float deltaTime, bool isPlaying, const std::string& repoRoot, const std::string& projectDirectory) {
        if (projectDirectory != s_SnapshotProject) {
            // Project switched: the owner calls CompileAsync/Sync itself; just re-baseline.
            s_Snapshot = ScanScripts(projectDirectory);
            s_SnapshotProject = projectDirectory;
        }

        if (s_PendingResult) {
            s_PendingResult = false;
            BuildResult result;
            {
                std::lock_guard<std::mutex> lock(s_ResultMutex);
                result = s_Result;
            }
            if (result.Ok && result.TypeErrors > 0) {
                // Like Unity: scripts with compile (type) errors never replace working ones.
                Log::Error("[TS] " + std::to_string(result.TypeErrors) + " type error(s) -- fix them to reload; the previous scripts stay loaded");
                result.Ok = false;
            }
            if (result.Ok) {
                s_ReadyBundle = result.Bundle;
                Log::Info("[TS] compiled " + std::to_string(result.Files) + " script file(s) in " + std::to_string(result.Milliseconds) +
                    " ms" + (isPlaying ? " -- will reload when Play stops" : ""));
            }
            s_Status = result.Ok ? TsCompileStatus::Succeeded : TsCompileStatus::Failed;
            if (s_RebuildQueued) {
                s_RebuildQueued = false;
                CompileAsync(repoRoot, projectDirectory);
            }
        }

        // Swap the bundle only outside Play: live JS objects are never replaced mid-session.
        if (!s_ReadyBundle.empty() && !isPlaying && s_Status != TsCompileStatus::Running) {
            const std::string bundle = s_ReadyBundle;
            s_ReadyBundle.clear();
            if (bundle == "<unload>")
                JsScripting::Unload();
            else if (!LoadBundle(bundle))
                s_Status = TsCompileStatus::Failed;
        }

        // File watching (once a second): any added/removed/modified .ts rebuilds.
        s_WatchTimer += deltaTime;
        if (s_WatchTimer < 1.0f)
            return;
        s_WatchTimer = 0.0f;
        auto current = ScanScripts(projectDirectory);
        if (current != s_Snapshot) {
            Log::Info("[TS] script change detected, recompiling...");
            CompileAsync(repoRoot, projectDirectory);
        }
    }

}
