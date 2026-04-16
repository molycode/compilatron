#include "gui/compiler_gui.hpp"
#include "gui/log_panel.hpp"
#include "build/clang_unit.hpp"
#include "build/gcc_unit.hpp"
#include "dependency/dependency_manager.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"

#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#include <imgui.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic pop
#endif

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <thread>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::IsTabComplete(SCompilerTab const& tab)
{
	return !tab.name.empty() && !tab.folderName.empty();
}

//////////////////////////////////////////////////////////////////////////
SCompilerBuildConfig CCompilerGUI::BuildConfigFromTab(SCompilerTab const& tab) const
{
	SCompilerBuildConfig config;
	config.id = tab.id;
	config.folderName = tab.folderName.empty() ? tab.name : tab.folderName;
	config.sourcesDir = g_dataDir + "/sources/" + config.folderName;
	config.buildDir = g_dataDir + "/build_compilers/" + config.folderName;
	config.dependenciesDir = g_dataDir + "/dependencies";
	config.numJobs = tab.numJobs > 0 ? tab.numJobs : std::max(1u, std::thread::hardware_concurrency() / 2);
	config.hostCompiler = tab.hostCompiler;

	return config;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::CreateUnitForTab(SCompilerTab const& tab)
{
	SCompilerBuildConfig const config{ BuildConfigFromTab(tab) };
	auto unit = CCompilerUnit::Create(tab.kind, tab.name.empty() ? std::to_string(tab.id) : tab.name, g_buildSettings, config);

	if (unit != nullptr)
	{
		if (tab.kind == ECompilerKind::Gcc)
		{
			static_cast<CGccUnit*>(unit.get())->SetGccSettings(tab.gccSettings);
		}
		else
		{
			static_cast<CClangUnit*>(unit.get())->SetClangSettings(tab.clangSettings);
		}

		unit->Initialize();
		m_compilerUnits[tab.id] = std::move(unit);
		RegisterUnitLogListener(tab.id, *m_compilerUnits[tab.id]);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "CreateUnitForTab: Failed to create unit for tab '{}' (kind='{}')", tab.name, tab.kind == ECompilerKind::Gcc ? "gcc" : "clang");
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::AddCompilerEntry(std::string_view compilerType)
{
	SCompilerTab newTab;

	if (compilerType == "gcc")
	{
		newTab.kind = ECompilerKind::Gcc;
		newTab.folderName = "gcc_" + std::to_string(m_nextCompilerTabId);
		newTab.gccSettings = SGccSettings{};
	}
	else if (compilerType == "clang")
	{
		newTab.kind = ECompilerKind::Clang;
		newTab.folderName = "clang_" + std::to_string(m_nextCompilerTabId);
		newTab.clangSettings = SClangSettings{};
		newTab.clangSettings.numNinjaLinkJobs = g_cpuInfo.GetDefaultLinkJobs();
	}
	else
	{
		newTab.folderName = "compiler_" + std::to_string(m_nextCompilerTabId);
	}

	newTab.id = m_nextCompilerTabId;
	newTab.isOpen = true;
	newTab.selectOnOpen = true;
	newTab.keepDependencies = true;
	newTab.keepSources = true;
	newTab.numJobs = 0;

	std::string const idStr{ std::to_string(newTab.id) };
	newTab.tabDisplayName  = std::string{ newTab.kind == ECompilerKind::Gcc ? "gcc" : "clang" } + " (no version)";
	newTab.tabLabel        = newTab.tabDisplayName + "###" + idStr;
	newTab.idTabCompiler   = "##TabCompiler" + idStr;
	newTab.idDeleteSources = "Delete Sources##" + idStr;
	newTab.idSourcesPopup  = "Confirm Delete Sources##" + idStr;
	newTab.idDeleteBuild   = "Delete Build##" + idStr;
	newTab.idBuildPopup    = "Confirm Delete Build##" + idStr;
	newTab.idShowCommand   = "Show Command##" + idStr;
	newTab.idAdvanced      = "Advanced Configuration##" + idStr;
	newTab.idCopyLog       = "Copy Log##" + idStr;
	newTab.idSaveLog       = "Save Log##" + idStr;
	newTab.idCompilerLog   = "CompilerLog##" + idStr;

	m_compilerTabs.push_back(std::move(newTab));
	CreateUnitForTab(m_compilerTabs.back());
	m_nextCompilerTabId++;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderCompilerTab(SCompilerTab& tab)
{
	ImGui::Separator();

	std::string baseUrl{ tab.kind == ECompilerKind::Gcc ? std::string{ GccSourceUrl } : std::string{ ClangSourceUrl } };

	ImGui::Text("Repository:");
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
	ImGui::SetNextItemWidth(400.0f);
	ImGui::InputText("##BaseURL", baseUrl.data(), baseUrl.size() + 1, ImGuiInputTextFlags_ReadOnly);
	ImGui::PopStyleColor(2);

	ImGui::SameLine();
	ImGui::Text("Version:");
	ImGui::SameLine();

	std::vector<std::string> versions = m_versionManager.GetVersions(tab.kind);

	if (versions.empty())
	{
		ImGui::Text("(No versions cached)");
		ImGui::SameLine();

		if (ImGui::Button("Fetch Versions"))
		{
			StartActualRefresh(tab.kind);
		}

		if (m_versionManager.IsRefreshing(tab.kind))
		{
			ImGui::SameLine();
			ImGui::Text("Fetching...");
		}
	}
	else
	{
		std::string comboId{ "##Version" + std::to_string(tab.id) };
		std::string displayText{ tab.name.empty() ? "Select version..." : tab.name };
		float textWidth{ ImGui::CalcTextSize(displayText.c_str()).x };
		float placeholderWidth{ ImGui::CalcTextSize("Select version...").x };
		float minWidth{ 120.0f };
		float optimalWidth{ std::max({textWidth, placeholderWidth, minWidth}) + 40.0f };
		ImGui::SetNextItemWidth(optimalWidth);

		if (ImGui::BeginCombo(comboId.c_str(), displayText.c_str()))
		{
			ImGui::EndCombo();

			std::vector<std::string> branches = m_versionManager.GetBranches(tab.kind);
			std::vector<std::string> tags = m_versionManager.GetTags(tab.kind);
			m_versionSelectorDialog.Open(branches, tags, tab.name, tab.kind);
			ImGui::OpenPopup("Select Version##VersionSelector");
		}

		std::string selectedVersionFromDialog{ m_versionSelectorDialog.Render() };

		if (!selectedVersionFromDialog.empty())
		{
			tab.name = selectedVersionFromDialog;
			tab.tabDisplayName = selectedVersionFromDialog;
			tab.tabLabel = tab.tabDisplayName + "###" + std::to_string(tab.id);
			MarkPresetDirty();

			auto const unitIt = m_compilerUnits.find(tab.id);

			if (unitIt != m_compilerUnits.end() && unitIt->second != nullptr)
			{
				unitIt->second->SetName(selectedVersionFromDialog);
			}
		}

		ImGui::SameLine();
		std::string refreshId{ "Refresh##" + std::to_string(tab.id) };

		if (ImGui::Button(refreshId.c_str()))
		{
			StartActualRefresh(tab.kind);
		}

		if (m_versionManager.IsRefreshing(tab.kind))
		{
			ImGui::SameLine();
			ImGui::Text("Refreshing...");
		}
		else
		{
			time_t now{ std::time(nullptr) };

			if ((now - m_lastDisplayStringUpdate) >= 60)
			{
				for (auto const& t : m_compilerTabs)
				{
					m_cachedDisplayStrings[t.kind] = m_versionManager.GetLastSyncDisplay(t.kind);
				}

				m_lastDisplayStringUpdate = now;
			}

			std::string lastSync{ m_cachedDisplayStrings[tab.kind] };

			if (!lastSync.empty() && lastSync != "Never")
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Last sync: %s", lastSync.c_str());

				if (ImGui::IsItemHovered())
				{
					bool hasToken{ m_versionManager.HasGitHubToken() };
					auto const& tracker = m_versionManager.GetRateLimitTracker();
					int used{ tracker.GetRequestsUsedThisHour() };
					int limit{ tracker.GetTotalLimit(hasToken) };
					std::string resetTime{ tracker.GetResetTimeString() };

					ImGui::BeginTooltip();
					ImGui::Text("GitHub API Rate Limit Status");
					ImGui::Separator();
					ImGui::Text("Requests used this hour: %d/%d", used, limit);

					if (!resetTime.empty())
					{
						ImGui::Text("Resets: %s", resetTime.c_str());
					}

					if (hasToken)
					{
						ImGui::Text("Authentication: GitHub Token");
					}
					else
					{
						ImGui::Text("Authentication: No Token (Anonymous)");
					}

					ImGui::EndTooltip();
				}
			}
		}
	}

	static SCompilerEntry const s_entry{};
	ImGui::Text("%s:", s_entry.folderName.uiName.data());
	ImGui::SameLine();

	if (tab.folderName != tab.folderNameBuffer)
	{
		size_t copyLen{ std::min(tab.folderName.length(), sizeof(tab.folderNameBuffer) - 1) };
		tab.folderName.copy(tab.folderNameBuffer, copyLen);
		tab.folderNameBuffer[copyLen] = '\0';
	}

	if (RenderTextFieldWithContextMenu("##FolderName", tab.folderNameBuffer, sizeof(tab.folderNameBuffer)))
	{
		tab.folderName = tab.folderNameBuffer;
		MarkPresetDirty();
	}

	auto validationResult = ValidateCompilerForBuild(tab);
	bool const isSystemInstall{ WouldInstallToSystemDirectory(tab) };

	if (validationResult == ECompilerValidationResult::CompilerSelfOverwrite)
	{
		std::string actualCompiler{ GetActualCompilerForTab(tab) };
		std::string targetPath{ GetResolvedInstallPath() + "/" + tab.folderName };
		std::string errorMsg{ GetValidationErrorMessage(validationResult, actualCompiler, targetPath) };
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
		ImGui::TextWrapped("WARNING: %s", errorMsg.c_str());
		ImGui::TextWrapped("Change the compiler override or folder name to resolve this.");
		ImGui::PopStyleColor();
	}

	if (isSystemInstall)
	{
		std::string folderName{ tab.folderName.empty() ?
			GetFolderNameFromCompilerName(tab.name) : tab.folderName };
		std::string plannedPath{ GetResolvedInstallPath() + "/" + folderName };
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
		ImGui::TextWrapped("WARNING: This would install to a system directory: %s", plannedPath.c_str());
		ImGui::TextWrapped("Installing to system directories (/usr, /bin, etc) is not allowed. Change install directory.");
		ImGui::PopStyleColor();
	}

	ImGui::NewLine();

	bool isThisCompilerBuilding{ false };

	{
		auto const unitIt = m_compilerUnits.find(tab.id);

		if (unitIt != m_compilerUnits.end() && unitIt->second != nullptr)
		{
			ECompilerStatus const s{ unitIt->second->GetStatus() };
			isThisCompilerBuilding = (s == ECompilerStatus::Cloning ||
			                         s == ECompilerStatus::Building ||
			                         s == ECompilerStatus::Waiting);
		}
	}

	RenderDeleteButtons(tab, isThisCompilerBuilding);

	ImGui::NewLine();

	int maxJobs{ g_cpuInfo.logicalCores };
	int currentJobs{ (tab.numJobs == 0) ? g_cpuInfo.GetDefaultNumJobs() : tab.numJobs };

	ImGui::Text("%s:", s_entry.numJobs.uiName.data());
	ImGui::SetNextItemWidth(200);

	if (ImGui::SliderInt("##JobCount", &currentJobs, 1, maxJobs, "%d jobs"))
	{
		tab.numJobs = currentJobs;
		MarkPresetDirty();
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f));
		ImGui::BeginTooltip();
		ImGui::Text("Number of parallel compilation jobs for this compiler");
		ImGui::Text("CPU: %d physical cores, %d total threads", g_cpuInfo.physicalCores, g_cpuInfo.logicalCores);
		ImGui::Text("Default: %d jobs (physical cores)", g_cpuInfo.GetDefaultNumJobs());
		ImGui::Text("Maximum: %d jobs (physical + hyperthreading)", maxJobs);
		ImGui::Text("Recommended: Use physical core count for best performance/memory balance");
		ImGui::EndTooltip();
	}

	ImGui::SameLine();

	if (ImGui::Button("Reset##JobCount"))
	{
		tab.numJobs = 0;
		MarkPresetDirty();
	}

	if (currentJobs > g_cpuInfo.physicalCores)
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "%s", ICON_FA_TRIANGLE_EXCLAMATION);

		if (ImGui::IsItemHovered())
		{
			ImGui::SetNextWindowSize(ImVec2(350.0f, 0.0f));
			ImGui::BeginTooltip();
			ImGui::Text("Using more jobs than physical cores (%d)", g_cpuInfo.physicalCores);
			ImGui::Text("May cause memory pressure and slower builds");
			ImGui::EndTooltip();
		}
	}

	ImGui::NewLine();

	ImGui::Text("%s:", s_entry.hostCompiler.uiName.data());
	ImGui::SameLine();

	bool hasCompleteConfig{ IsTabComplete(tab) };
	bool hasDependencies{ AreRequiredDependenciesAvailable(tab.kind) };
	bool hasValidCompiler{ IsTabCompilerValid(tab) };

	RenderUnifiedCompilerSelector(tab.idTabCompiler, tab.hostCompiler,
		[this, &tab](std::string const& newCompiler) {
			tab.hostCompiler = newCompiler;
			MarkPresetDirty();
		},
		true
	);

	if (ImGui::IsItemHovered() && hasValidCompiler)
	{
		ImGui::SetTooltip("Set the host compiler for this specific build.\nLeave as default to use the globally selected host compiler.");
	}

	if (!hasValidCompiler && !tab.hostCompiler.empty())
	{
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "%s", ICON_FA_TRIANGLE_EXCLAMATION);

		if (ImGui::IsItemHovered())
		{
			ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f));
			ImGui::BeginTooltip();
			ImGui::TextWrapped("%s", GetInvalidCompilerMessage(tab).c_str());
			ImGui::EndTooltip();
		}
	}

	bool hasNoSelfCompilation{ (validationResult != ECompilerValidationResult::CompilerSelfOverwrite) };
	bool hasNoSystemInstall{ !isSystemInstall };

	bool thisCompilerBuilding{ false };

	{
		auto const unitIt = m_compilerUnits.find(tab.id);

		if (unitIt != m_compilerUnits.end() && unitIt->second != nullptr)
		{
			ECompilerStatus const s{ unitIt->second->GetStatus() };
			thisCompilerBuilding = (s == ECompilerStatus::Cloning ||
			                       s == ECompilerStatus::Building ||
			                       s == ECompilerStatus::Waiting);
		}
	}

	bool otherCompilerBuilding{ m_isBuilding && !thisCompilerBuilding };
	bool const hasLldAvailable{ IsLldAvailableForTab(tab) };
	bool canBuild{ hasCompleteConfig && hasDependencies && hasValidCompiler && hasNoSelfCompilation && hasNoSystemInstall && hasLldAvailable && !otherCompilerBuilding };

	ImGui::NewLine();

	std::string buttonText;
	std::string buttonId;

	if (thisCompilerBuilding)
	{
		buttonText = "Stop";
		buttonId = buttonText + "##" + std::to_string(tab.id);
	}
	else
	{
		buttonText = "Build";
		buttonId = buttonText + "##" + std::to_string(tab.id);
	}

	if (!canBuild)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
	}
	else if (thisCompilerBuilding)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.1f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.2f, 0.0f, 1.0f));
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.38f, 0.12f, 1.0f));
	}

	if (!canBuild && !thisCompilerBuilding)
	{
		ImGui::BeginDisabled();
	}

	ImGui::SetNextItemWidth(120.0f);
	bool buttonClicked{ ImGui::Button(buttonId.c_str(), ImVec2(120.0f, 30.0f)) };
	ImGui::PopStyleColor(3);

	if (!canBuild && !thisCompilerBuilding)
	{
		ImGui::EndDisabled();
	}

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		if (!canBuild)
		{
			if (otherCompilerBuilding)
			{
				ImGui::SetTooltip("Another compiler is currently building. Wait for it to complete or stop the build.");
			}
			else if (!hasCompleteConfig)
			{
				ImGui::SetTooltip("Select a version and complete Folder Name to enable building");
			}
			else if (!hasDependencies)
			{
				ImGui::SetTooltip("%s", GetMissingDependenciesMessage().c_str());
			}
			else if (!hasValidCompiler)
			{
				ImGui::SetTooltip("%s", GetInvalidCompilerMessage(tab).c_str());
			}
			else if (!hasNoSelfCompilation)
			{
				std::string actualCompiler{ GetActualCompilerForTab(tab) };
				ImGui::SetTooltip("Cannot build: Would cause self-compilation. Compiler %s cannot compile itself.", actualCompiler.c_str());
			}
			else if (!hasNoSystemInstall)
			{
				ImGui::SetTooltip("Cannot build: Would install to system directory. Change install directory to avoid system conflicts.");
			}
			else if (!hasLldAvailable)
			{
				ImGui::SetTooltip("Cannot build: lld selected as linker but ld.lld not found. Install lld (e.g. sudo apt install lld) or select a different linker.");
			}
		}
		else if (thisCompilerBuilding)
		{
			ImGui::SetTooltip("Stop this compiler's build process");
		}
		else
		{
			ImGui::SetTooltip("Build this compiler only");
		}
	}

	if (buttonClicked)
	{
		if (thisCompilerBuilding)
		{
			StopBuild();
		}
		else if (canBuild)
		{
			StartSingleCompilerBuild(tab);
		}
	}

	ImGui::SameLine();

	if (ImGui::Button(tab.idShowCommand.c_str(), ImVec2(120.0f, 30.0f)))
	{
		m_showCommandDialog = true;
		m_commandDialogTabId = tab.id;
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Display the complete command line that will be executed");
	}

	if (tab.kind == ECompilerKind::Gcc)
	{
		ImGui::SameLine();

		if (ImGui::Button(tab.idAdvanced.c_str(), ImVec2(170.0f, 30.0f)))
		{
			m_showGccAdvancedDialog = true;
			m_gccAdvancedTabId = tab.id;
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Configure GCC build flags and settings");
		}
	}
	else
	{
		ImGui::SameLine();

		if (ImGui::Button(tab.idAdvanced.c_str(), ImVec2(170.0f, 30.0f)))
		{
			m_showClangAdvancedDialog = true;
			m_clangAdvancedTabId = tab.id;
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Configure Clang/LLVM build targets, projects, and settings");
		}
	}

	ImGui::NewLine();
	ImGui::Separator();

	RenderCompilerProgressSection(tab);
	RenderCompilerLogSection(tab);
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderDeleteButtons(SCompilerTab const& tab, bool isBuilding)
{
	RenderDeleteBuildButton(tab, isBuilding);
	ImGui::SameLine();
	RenderDeleteSourcesButton(tab, isBuilding);
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderDeleteSourcesButton(SCompilerTab const& tab, bool isBuilding)
{

	std::string sourcesPath{ "./sources/" + tab.folderName };
	bool sourcesExist{ std::filesystem::exists(sourcesPath) };

	bool sourcesButtonEnabled{ sourcesExist && !isBuilding };

	if (!sourcesButtonEnabled)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
	}

	bool deleteSourcesClicked{ ImGui::Button(tab.idDeleteSources.c_str()) };

	if (ImGui::IsItemActive())
	{
		m_sourcesClickPos = ImGui::GetMousePos();
	}

	if (!sourcesButtonEnabled)
	{
		ImGui::PopStyleColor(3);
	}

	if (ImGui::IsItemHovered())
	{
		if (isBuilding)
		{
			ImGui::SetTooltip("Cannot delete sources while compiler is building");
		}
		else if (!sourcesExist)
		{
			ImGui::SetTooltip("No sources to delete");
		}
		else
		{
			ImGui::SetTooltip("Delete downloaded source code.\nPath: %s", sourcesPath.c_str());
		}
	}

	if (deleteSourcesClicked && sourcesButtonEnabled)
	{
		m_sourcesDialogJustOpened = true;
		m_showSourcesConfirm = true;
		m_confirmTabId = tab.id;
		m_sourcesSize = GetDirectorySize(sourcesPath);
	}

	if (m_showSourcesConfirm && m_confirmTabId == tab.id)
	{
		ImGui::OpenPopup(tab.idSourcesPopup.c_str());
	}

	if (ImGui::BeginPopupModal(tab.idSourcesPopup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (m_sourcesDialogJustOpened && m_showSourcesConfirm && m_confirmTabId == tab.id)
		{
			ImGui::SetWindowPos(m_sourcesClickPos);
			m_sourcesDialogJustOpened = false;
		}

		ImGui::Text("Delete source code for %s?", tab.name.c_str());
		ImGui::Separator();
		ImGui::Text("Path: %s", sourcesPath.c_str());
		ImGui::Text("Size: %s", FormatSize(m_sourcesSize).c_str());
		ImGui::NewLine();
		ImGui::TextWrapped("This will delete the downloaded source code. New builds will re-download sources.");
		ImGui::NewLine();
		ImGui::NewLine();

		float buttonWidth{ 120.0f };
		float buttonSpacing{ ImGui::GetStyle().ItemSpacing.x };
		float totalButtonWidth{ (buttonWidth * 2.0f) + buttonSpacing };
		float availableWidth{ ImGui::GetContentRegionAvail().x };
		float startPosX{ (availableWidth - totalButtonWidth) * 0.5f };

		if (startPosX > 0.0f)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startPosX);
		}

		if (ImGui::Button("Delete", ImVec2(buttonWidth, 0)))
		{
			DeleteCompilerSources(tab);
			m_showSourcesConfirm = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
		{
			m_showSourcesConfirm = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderDeleteBuildButton(SCompilerTab const& tab, bool isBuilding)
{

	std::string buildPath{ "./build_compilers/" + tab.folderName };
	bool buildExists{ std::filesystem::exists(buildPath) };

	bool buildButtonEnabled{ buildExists && !isBuilding };

	if (!buildButtonEnabled)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
	}

	bool deleteBuildClicked{ ImGui::Button(tab.idDeleteBuild.c_str()) };

	if (ImGui::IsItemActive())
	{
		m_buildClickPos = ImGui::GetMousePos();
	}

	if (!buildButtonEnabled)
	{
		ImGui::PopStyleColor(3);
	}

	if (ImGui::IsItemHovered())
	{
		if (isBuilding)
		{
			ImGui::SetTooltip("Cannot delete build files while compiler is building");
		}
		else if (!buildExists)
		{
			ImGui::SetTooltip("No build files to delete");
		}
		else
		{
			ImGui::SetTooltip("Delete build artifacts (forces clean rebuild).\nPath: %s", buildPath.c_str());
		}
	}

	if (deleteBuildClicked && buildButtonEnabled)
	{
		m_buildDialogJustOpened = true;
		m_showBuildConfirm = true;
		m_confirmTabId = tab.id;
		m_buildSize = GetDirectorySize(buildPath);
	}

	if (m_showBuildConfirm && m_confirmTabId == tab.id)
	{
		ImGui::OpenPopup(tab.idBuildPopup.c_str());
	}

	if (ImGui::BeginPopupModal(tab.idBuildPopup.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (m_buildDialogJustOpened && m_showBuildConfirm && m_confirmTabId == tab.id)
		{
			ImGui::SetWindowPos(m_buildClickPos);
			m_buildDialogJustOpened = false;
		}

		ImGui::Text("Delete build artifacts for %s?", tab.name.c_str());
		ImGui::Separator();
		ImGui::Text("Path: %s", buildPath.c_str());
		ImGui::Text("Size: %s", FormatSize(m_buildSize).c_str());
		ImGui::NewLine();
		ImGui::TextWrapped("This will delete compiled object files and build artifacts. The next build will be a clean rebuild.");
		ImGui::NewLine();
		ImGui::NewLine();

		float buttonWidth{ 120.0f };
		float buttonSpacing{ ImGui::GetStyle().ItemSpacing.x };
		float totalButtonWidth{ (buttonWidth * 2.0f) + buttonSpacing };
		float availableWidth{ ImGui::GetContentRegionAvail().x };
		float startPosX{ (availableWidth - totalButtonWidth) * 0.5f };

		if (startPosX > 0.0f)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + startPosX);
		}

		if (ImGui::Button("Delete", ImVec2(buttonWidth, 0)))
		{
			DeleteCompilerBuild(tab);
			m_showBuildConfirm = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0)))
		{
			m_showBuildConfirm = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderCompilerProgressSection(SCompilerTab const& tab)
{
	ImGui::Text("Build Progress");

	auto const unitIt = m_compilerUnits.find(tab.id);

	if (unitIt != m_compilerUnits.end() && unitIt->second != nullptr)
	{
		RenderProgressBarWithStatus(*unitIt->second);
	}
	else
	{
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Progress will appear here when build starts. Please select a version and configure Folder Name.");
		ImGui::ProgressBar(0.0f, ImVec2(g_elementWidth, 0), "Not Started");
	}

	ImGui::NewLine();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderCompilerLogSection(SCompilerTab const& tab)
{
	ImGui::Text("Build Log");

	bool copyClicked{ false };
	bool saveClicked{ false };

	{
		std::lock_guard<std::mutex> lock(m_unitLogsMutex);
		auto const logIt = m_unitLogs.find(tab.id);
		std::vector<SLogEntry> const emptyLog;
		std::vector<SLogEntry> const& entries{ logIt != m_unitLogs.end() ? logIt->second : emptyLog };
		bool const hasEntries{ !entries.empty() };

		if (ImGui::Button(tab.idCopyLog.c_str()))
		{
			copyClicked = true;
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Copy this compiler's log to clipboard");
		}

		ImGui::SameLine();
		ImGui::BeginDisabled(m_logSaver.IsActive() || !hasEntries);

		if (ImGui::Button(tab.idSaveLog.c_str()))
		{
			saveClicked = true;
		}

		ImGui::EndDisabled();

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Save this compiler's log to file");
		}

		std::string const emptyMsg{ std::format(
			"No build output yet for {}. Compilation progress will appear here.", tab.name) };
		RenderLogPanel(tab.idCompilerLog, entries, emptyMsg);
	}

	if (copyClicked)
	{
		CopyCompilerLogToClipboard(tab.id);
	}

	if (saveClicked)
	{
		SaveCompilerLogToFile(tab.id);
	}
}

//////////////////////////////////////////////////////////////////////////
std::uintmax_t CCompilerGUI::GetDirectorySize(std::string_view path)
{
	std::uintmax_t size{ 0 };
	std::error_code iterEc;

	for (auto const& entry : std::filesystem::recursive_directory_iterator(path, iterEc))
	{
		if (!iterEc && entry.is_regular_file())
		{
			size += entry.file_size();
		}
	}

	return size;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::FormatSize(std::uintmax_t bytes)
{
	if (bytes >= 1024ULL * 1024 * 1024)
	{
		return std::format("{:.1f} GiB", static_cast<double>(bytes) / (1024.0 * 1024 * 1024));
	}
	else if (bytes >= 1024ULL * 1024)
	{
		return std::format("{:.1f} MiB", static_cast<double>(bytes) / (1024.0 * 1024));
	}
	else if (bytes >= 1024ULL)
	{
		return std::format("{:.1f} KiB", static_cast<double>(bytes) / 1024.0);
	}
	else
	{
		return std::format("{} bytes", bytes);
	}
}
} // namespace Ctrn
