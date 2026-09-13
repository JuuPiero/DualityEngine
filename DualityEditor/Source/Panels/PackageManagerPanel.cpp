#include "DualityEditor/Panels/PackageManagerPanel.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "DualityEditor/EditorContext.h"
#include "DualityEditor/ScriptEngine.h"
#include "DualityEngine/Core/Log.h"
#include "DualityEngine/Project/Project.h"

namespace Duality {
    namespace {
        using json = nlohmann::json;

        struct PackageInfo {
            std::string Id;
            std::string DisplayName;
            std::string Version;
            std::string Description;
            std::filesystem::path Path;
            std::string Error;
            bool Enabled = false;
        };

        bool IsSafePackageId(const std::string& value) {
            if (value.empty())
                return false;
            for (unsigned char c : value) {
                if (!std::isalnum(c) && c != '.' && c != '_' && c != '-')
                    return false;
            }
            return true;
        }

        bool IsEnabled(const ProjectConfig& config, const std::string& id) {
            return std::find(config.EnabledPackages.begin(), config.EnabledPackages.end(), id) != config.EnabledPackages.end();
        }

        void SetEnabled(ProjectConfig& config, const std::string& id, bool enabled) {
            auto found = std::find(config.EnabledPackages.begin(), config.EnabledPackages.end(), id);
            if (enabled && found == config.EnabledPackages.end())
                config.EnabledPackages.push_back(id);
            else if (!enabled && found != config.EnabledPackages.end())
                config.EnabledPackages.erase(found);
        }

        std::vector<PackageInfo> ScanPackages(const Project& project) {
            std::vector<PackageInfo> packages;
            const std::filesystem::path root(project.GetPackagesDirectory());
            std::error_code error;
            if (!std::filesystem::is_directory(root, error))
                return packages;

            for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
                if (error || !entry.is_directory())
                    continue;
                PackageInfo info;
                info.Path = entry.path();
                const std::filesystem::path manifest = entry.path() / "package.json";
                std::ifstream file(manifest);
                if (!file.is_open()) {
                    info.Id = entry.path().filename().string();
                    info.Error = "package.json is missing";
                    packages.push_back(std::move(info));
                    continue;
                }
                try {
                    json rootJson;
                    file >> rootJson;
                    info.Id = rootJson.value("name", entry.path().filename().string());
                    info.DisplayName = rootJson.value("displayName", info.Id);
                    info.Version = rootJson.value("version", "0.0.0");
                    info.Description = rootJson.value("description", "");
                    if (!IsSafePackageId(info.Id))
                        info.Error = "Package id may only use letters, digits, '.', '_' and '-'";
                    else if (info.Id != entry.path().filename().string())
                        info.Error = "Manifest name must match its folder name";
                    info.Enabled = info.Error.empty() && IsEnabled(project.GetConfig(), info.Id);
                } catch (const std::exception& e) {
                    info.Id = entry.path().filename().string();
                    info.Error = std::string("Invalid package.json: ") + e.what();
                }
                packages.push_back(std::move(info));
            }
            std::sort(packages.begin(), packages.end(), [](const PackageInfo& a, const PackageInfo& b) { return a.Id < b.Id; });
            return packages;
        }

        bool WriteManifest(const std::filesystem::path& folder, const std::string& id, const std::string& displayName) {
            json manifest{
                { "name", id },
                { "displayName", displayName.empty() ? id : displayName },
                { "version", "0.1.0" },
                { "description", "Project-local Duality gameplay package." },
                { "supportedPlatforms", { "Desktop", "Nintendo3DS" } }
            };
            std::ofstream file(folder / "package.json");
            if (!file.is_open())
                return false;
            file << manifest.dump(2) << '\n';
            return static_cast<bool>(file);
        }
    }

    void PackageManagerPanel::OnImGuiRender(EditorContext& ctx) {
        if (!ctx.ShowPackageManager)
            return;
        if (!ImGui::Begin("Package Manager", &ctx.ShowPackageManager)) {
            ImGui::End();
            return;
        }

        auto project = Project::GetActive();
        if (!project) {
            ImGui::TextDisabled("<no active project>");
            ImGui::End();
            return;
        }

        const std::filesystem::path packagesRoot(project->GetPackagesDirectory());
        std::error_code error;
        std::filesystem::create_directories(packagesRoot, error);
        if (error)
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Cannot create Packages folder: %s", error.message().c_str());

        ImGui::TextDisabled("Project-local packages: %s", packagesRoot.string().c_str());
        ImGui::TextWrapped("Enabled packages compile into GameScripts on both desktop and Nintendo 3DS. A package is never linked into the engine core.");
        ImGui::Separator();

        static char newId[128] = "com.company.newpackage";
        static char newDisplayName[128] = "New Package";
        static char installPath[512] = {};
        static std::string status;
        static std::string pendingRemoval;

        if (ImGui::Button("Create Package..."))
            ImGui::OpenPopup("Create Package");
        ImGui::SameLine();
        if (ImGui::Button("Install from Folder..."))
            ImGui::OpenPopup("Install Package");
        ImGui::SameLine();
        const bool canReload = !ctx.IsPlaying && ScriptEngine::GetStatus() != ReloadStatus::Running;
        if (ImGui::Button("Apply + Reload Scripts") && canReload) {
            ScriptEngine::ReloadAsync(ctx.BuildDirectory);
            status = "Rebuilding GameScripts with enabled packages...";
        }
        if (!canReload && ImGui::IsItemHovered())
            ImGui::SetTooltip("Stop Play mode and wait for any active rebuild first.");

        if (ImGui::BeginPopupModal("Create Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Package ID", newId, sizeof(newId));
            ImGui::InputText("Display Name", newDisplayName, sizeof(newDisplayName));
            ImGui::TextDisabled("Example: com.mystudio.tween. ID becomes the package folder name.");
            if (ImGui::Button("Create")) {
                const std::string id(newId);
                const std::filesystem::path destination = packagesRoot / id;
                if (!IsSafePackageId(id))
                    status = "Package ID contains unsupported characters.";
                else if (std::filesystem::exists(destination))
                    status = "A package with that ID already exists.";
                else {
                    std::error_code createError;
                    std::filesystem::create_directories(destination / "Source", createError);
                    std::filesystem::create_directories(destination / "Include", createError);
                    if (createError || !WriteManifest(destination, id, newDisplayName)) {
                        std::filesystem::remove_all(destination, createError);
                        status = "Could not create package files.";
                    } else {
                        SetEnabled(project->GetConfig(), id, true);
                        project->Save();
                        status = "Created and enabled '" + id + "'. Add C++ files under Source/ and headers under Include/.";
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Install Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Paste the folder containing package.json. Its folder name and manifest name must be the same package ID.");
            ImGui::InputText("Folder", installPath, sizeof(installPath));
            if (ImGui::Button("Install and Enable")) {
                const std::filesystem::path source(installPath);
                std::ifstream manifestFile(source / "package.json");
                json manifest;
                try {
                    if (!manifestFile.is_open())
                        throw std::runtime_error("package.json was not found");
                    manifestFile >> manifest;
                    const std::string id = manifest.value("name", "");
                    const std::filesystem::path destination = packagesRoot / id;
                    if (!IsSafePackageId(id) || source.filename().string() != id)
                        throw std::runtime_error("invalid package ID or folder name");
                    if (std::filesystem::exists(destination))
                        throw std::runtime_error("a package with that ID is already installed");
                    std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive);
                    SetEnabled(project->GetConfig(), id, true);
                    project->Save();
                    status = "Installed and enabled '" + id + "'.";
                    installPath[0] = '\0';
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    status = std::string("Install failed: ") + e.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        const std::vector<PackageInfo> packages = ScanPackages(*project);
        ImGui::Spacing();
        if (ImGui::BeginTable("##Packages", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 78.0f);
            ImGui::TableHeadersRow();
            for (const PackageInfo& info : packages) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(info.DisplayName.empty() ? info.Id.c_str() : info.DisplayName.c_str());
                ImGui::TextDisabled("%s", info.Id.c_str());
                if (!info.Description.empty())
                    ImGui::TextWrapped("%s", info.Description.c_str());
                if (!info.Error.empty())
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", info.Error.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(info.Version.c_str());
                ImGui::TableSetColumnIndex(2);
                bool enabled = info.Enabled;
                ImGui::BeginDisabled(!info.Error.empty());
                if (ImGui::Checkbox(("##enabled_" + info.Id).c_str(), &enabled)) {
                    SetEnabled(project->GetConfig(), info.Id, enabled);
                    project->Save();
                    status = std::string(enabled ? "Enabled '" : "Disabled '") + info.Id + "'. Click Apply + Reload Scripts to use the change.";
                }
                ImGui::EndDisabled();
                ImGui::TableSetColumnIndex(3);
                if (ImGui::SmallButton(("Remove##" + info.Id).c_str())) {
                    pendingRemoval = info.Id;
                    ImGui::OpenPopup("Remove Package?");
                }
            }
            ImGui::EndTable();
        }

        if (ImGui::BeginPopupModal("Remove Package?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Remove '%s' and all of its files from this project? This cannot be undone by the editor.", pendingRemoval.c_str());
            if (ImGui::Button("Remove Package")) {
                const std::filesystem::path target = packagesRoot / pendingRemoval;
                const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(packagesRoot, error);
                const std::filesystem::path canonicalTarget = std::filesystem::weakly_canonical(target, error);
                if (error || canonicalTarget.parent_path() != canonicalRoot || !IsSafePackageId(pendingRemoval)) {
                    status = "Refused to remove an invalid package path.";
                } else {
                    std::filesystem::remove_all(target, error);
                    if (error)
                        status = "Could not remove package: " + error.message();
                    else {
                        SetEnabled(project->GetConfig(), pendingRemoval, false);
                        project->Save();
                        status = "Removed '" + pendingRemoval + "'. Click Apply + Reload Scripts to rebuild without it.";
                    }
                }
                pendingRemoval.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                pendingRemoval.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (!status.empty())
            ImGui::TextWrapped("%s", status.c_str());
        ImGui::End();
    }
}
