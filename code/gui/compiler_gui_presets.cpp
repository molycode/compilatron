#include "gui/compiler_gui.hpp"
#include "gui/preset_diff.hpp"
#include "dependency/dependency_window.hpp"
#include "common/loggers.hpp"
#include "common/common.hpp"
#include "common/process_executor.hpp"
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#include <imgui.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic pop
#endif
#include <chrono>
#include <cstring>
#include <filesystem>
#include <format>
#include <future>
#include <string_view>
#include <fstream>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RefreshPresetNames()
{
	m_availablePresets = m_presetManager.GetPresetNames();
	m_presetDescriptions.clear();

	for (std::string const& name : m_availablePresets)
	{
		m_presetDescriptions[name] = GetPresetDescription(name);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::IsPresetDirty() const
{
	return !m_currentPresetName.empty()
		&& !GeneratePresetDiff(CreateBuildSettingsFromTabs(), m_savedPresetSnapshot).empty();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::MarkPresetDirty()
{
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderPresetControls()
{
	ImGui::NewLine();

	bool const isDirty{ IsPresetDirty() };
	bool const hasPreset{ !m_currentPresetName.empty() };
	std::string dropdownLabel{ hasPreset ? m_currentPresetName : "— none —" };

	if (isDirty)
	{
		dropdownLabel += " *";
	}

	ImGui::TextDisabled("Preset:");
	ImGui::SameLine();

	float dropdownWidth{ std::max(160.0f, ImGui::CalcTextSize(dropdownLabel.c_str()).x + ImGui::GetFrameHeight() * 2.0f) };
	ImGui::SetNextItemWidth(dropdownWidth);

	if (ImGui::BeginCombo("##PresetDropdown", dropdownLabel.c_str()))
	{
		bool isDefaultSelected{ !hasPreset };

		if (ImGui::Selectable("— none —", isDefaultSelected))
		{
			SelectPreset("");
		}

		if (isDefaultSelected)
		{
			ImGui::SetItemDefaultFocus();
		}

		for (auto const& presetName : m_availablePresets)
		{
			bool isSelected{ (m_currentPresetName == presetName) };

			if (ImGui::Selectable(presetName.c_str(), isSelected))
			{
				SelectPreset(presetName);
			}

			if (isSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		ImGui::EndCombo();
	}

	if (ImGui::IsItemHovered() && hasPreset)
	{
		auto const descIt{ m_presetDescriptions.find(m_currentPresetName) };

		if (descIt != m_presetDescriptions.end() && !descIt->second.empty())
		{
			ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f));
			ImGui::BeginTooltip();
			ImGui::Text("Description:");
			ImGui::TextWrapped("%s", descIt->second.c_str());
			ImGui::EndTooltip();
		}
	}

	// Save — single click, only when a preset is active
	ImGui::SameLine();

	if (!hasPreset || !isDirty)
	{
		ImGui::BeginDisabled();
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.65f, 0.45f, 0.05f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.78f, 0.57f, 0.10f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.37f, 0.03f, 1.0f));
	}

	if (ImGui::Button("Save"))
	{
		ShowPresetSaveDialog(false);
	}

	if (!hasPreset || !isDirty)
	{
		ImGui::EndDisabled();
	}
	else
	{
		ImGui::PopStyleColor(3);
	}

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		if (!hasPreset)
		{
			ImGui::SetTooltip("No preset selected — use Save As... to create one");
		}
		else if (isDirty)
		{
			ImGui::SetTooltip("Save changes to \"%s\"", m_currentPresetName.c_str());
		}
		else
		{
			ImGui::SetTooltip("Preset is up to date");
		}
	}

	// Save As... — always available, opens naming dialog
	ImGui::SameLine();

	if (ImGui::Button("Save As..."))
	{
		ShowPresetSaveDialog(true);
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Save current settings as a new or existing preset");
	}

	// Delete — only when a preset is active
	ImGui::SameLine();

	if (!hasPreset)
	{
		ImGui::BeginDisabled();
	}

	ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.18f, 0.18f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.35f, 0.08f, 0.08f, 1.0f));

	if (ImGui::Button("Delete"))
	{
		ImGui::OpenPopup("ConfirmDeletePreset");
	}

	ImGui::PopStyleColor(3);

	if (!hasPreset)
	{
		ImGui::EndDisabled();
	}

	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip(hasPreset ? "Delete preset \"%s\"" : "No preset selected", m_currentPresetName.c_str());
	}

	if (ImGui::BeginPopup("ConfirmDeletePreset"))
	{
		ImGui::Text("Delete \"%s\"? This cannot be undone.", m_currentPresetName.c_str());
		ImGui::Spacing();

		if (ImGui::Button("Delete"))
		{
			if (m_presetManager.DeletePreset(m_currentPresetName))
			{
				gLog.Info(Tge::Logging::ETarget::Console, "Preset deleted: {}", m_currentPresetName);
				SelectPreset("");
				RefreshPresetNames();
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::Console, "Failed to delete preset: {}", m_currentPresetName);
			}

			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(20.0f, 0.0f));
	ImGui::SameLine();

	static std::string cpuName = GetCpuName();

	ImGui::Text("System:");
	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.7f, 0.9f, 1.0f, 1.0f), "%dC/%dT", g_cpuInfo.physicalCores, g_cpuInfo.logicalCores);

	if (ImGui::IsItemHovered())
	{
		ImGui::SetNextWindowSize(ImVec2(300.0f, 0.0f));
		ImGui::BeginTooltip();
		ImGui::Text("CPU: %s", cpuName.c_str());
		ImGui::Text("Physical Cores: %d", g_cpuInfo.physicalCores);
		ImGui::Text("Logical Threads: %d", g_cpuInfo.logicalCores);
		ImGui::EndTooltip();
	}

	ImGui::SameLine();
	ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1.0f), "%.0f GiB RAM", g_cpuInfo.totalMemoryGB);

	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::Text("Total System Memory: %.1f GiB", g_cpuInfo.totalMemoryGB);
		ImGui::Text("Currently Available: %.1f GiB", g_cpuInfo.availableMemoryGB);
		ImGui::EndTooltip();
	}

	ImGui::NewLine();

	ImGui::Text("Install Directory:");
	ImGui::SameLine();
	static char installDirBuffer[512];
	size_t copyLen{ std::min(g_buildSettings.installDirectory.length(), sizeof(installDirBuffer) - 1) };
	g_buildSettings.installDirectory.copy(installDirBuffer, copyLen);
	installDirBuffer[copyLen] = '\0';
	ImGui::SetNextItemWidth(200);

	if (RenderTextFieldWithContextMenu("##InstallDir", installDirBuffer, sizeof(installDirBuffer)))
	{
		g_buildSettings.installDirectory = installDirBuffer;
		MarkPresetDirty();
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f));
		ImGui::BeginTooltip();
		ImGui::Text("Directory where compiled compilers will be installed");
		ImGui::Text("Relative path: relative to executable location");
		ImGui::Text("Absolute path: exact location on system");
		ImGui::Text("Default: 'compilers' (creates folder next to executable)");
		ImGui::EndTooltip();
	}

	ImGui::SameLine();

	if (m_installDirBrowseActive
		&& m_installDirBrowseFuture.valid()
		&& m_installDirBrowseFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
	{
		std::string selectedPath{ m_installDirBrowseFuture.get() };

		while (!selectedPath.empty() && std::isspace(static_cast<unsigned char>(selectedPath.back())))
		{
			selectedPath.pop_back();
		}

		if (!selectedPath.empty())
		{
			g_buildSettings.installDirectory = selectedPath;
			size_t const browseLen = std::min(selectedPath.length(), sizeof(installDirBuffer) - 1);
			selectedPath.copy(installDirBuffer, browseLen);
			installDirBuffer[browseLen] = '\0';
			MarkPresetDirty();
		}

		m_installDirBrowseActive = false;
	}

	ImGui::BeginDisabled(m_installDirBrowseActive);

	if (ImGui::Button("Browse##InstallDir"))
	{
		m_installDirBrowseActive = true;
		m_installDirBrowseFuture = std::async(std::launch::async, []() -> std::string
		{
			std::string const zenityCmd = "zenity --file-selection --directory --title=\"Select Install Directory for Compilers\" 2>/dev/null";
			auto result{ CProcessExecutor::Execute(zenityCmd).output };
			RequestRedraw();
			return result;
		});
	}

	ImGui::EndDisabled();

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Browse for directory using file manager");
	}

	ImGui::SameLine();

	if (ImGui::Button("Reset##InstallDir"))
	{
		g_buildSettings.installDirectory = "compilers";
		MarkPresetDirty();
	}

	ImGui::SameLine();
	ImGui::Text("Host Compiler:");
	ImGui::SameLine();

	RenderUnifiedCompilerSelector("##GlobalCompiler", g_buildSettings.globalHostCompiler,
		[this](std::string const& newCompiler) {
			g_buildSettings.globalHostCompiler = newCompiler;
			MarkPresetDirty();
		}
	);

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Compiler used to build downloaded GCC/Clang sources\nCan be overridden per compiler tab");
	}

	ImGui::SameLine();

	if (!m_isBuilding)
	{
		bool canBuild{ true };
		bool anyTabHasMissingDeps{ false };
		bool hasValidCompilers{ false };
		bool allTabsHaveValidCompilers{ true };
		bool anyTabHasSelfCompilation{ false };
		bool anyTabHasSystemInstall{ false };
		bool anyTabHasLldMissing{ false };
		int numBuildableCompilers{ 0 };

		for (auto const& tab : m_compilerTabs)
		{
			if (tab.isOpen && !tab.name.empty() && !tab.folderName.empty())
			{
				hasValidCompilers = true;

				if (!AreRequiredDependenciesAvailable(tab.kind))
				{
					canBuild = false;
					anyTabHasMissingDeps = true;
				}

				if (!IsTabCompilerValid(tab))
				{
					allTabsHaveValidCompilers = false;
				}

				auto validationResult = ValidateCompilerForBuild(tab);

				if (validationResult == ECompilerValidationResult::CompilerSelfOverwrite)
				{
					anyTabHasSelfCompilation = true;
				}

				if (WouldInstallToSystemDirectory(tab))
				{
					anyTabHasSystemInstall = true;
				}

				if (!IsLldAvailableForTab(tab))
				{
					anyTabHasLldMissing = true;
				}
				else
				{
					++numBuildableCompilers;
				}
			}
		}

		canBuild = canBuild && hasValidCompilers && allTabsHaveValidCompilers && !anyTabHasSelfCompilation && !anyTabHasSystemInstall && numBuildableCompilers > 0;

		if (!canBuild)
		{
			ImGui::BeginDisabled();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.15f, 0.45f, 0.15f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.12f, 0.38f, 0.12f, 1.0f));
		}

		if (ImGui::Button("Build Compilers"))
		{
			StartBuild();
		}

		if (canBuild)
		{
			ImGui::PopStyleColor(3);
		}

		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			if (anyTabHasMissingDeps)
			{
				ImGui::SetTooltip("%s", GetMissingDependenciesMessage().c_str());
			}
			else if (!hasValidCompilers)
			{
				ImGui::SetTooltip("Please complete at least one compiler entry (select version + folder name)");
			}
			else if (!allTabsHaveValidCompilers)
			{
				ImGui::SetTooltip("One or more compiler tabs have invalid or missing compilers");
			}
			else if (anyTabHasSelfCompilation)
			{
				ImGui::SetTooltip("One or more compiler tabs would cause self-compilation (compiler compiling itself). Change compiler or folder name.");
			}
			else if (anyTabHasSystemInstall)
			{
				ImGui::SetTooltip("One or more compiler tabs would install to system directories (/usr, /bin, etc). Change install directory to avoid system conflicts.");
			}
			else if (anyTabHasLldMissing && numBuildableCompilers == 0)
			{
				ImGui::SetTooltip("No compilers can be built: lld selected but ld.lld not found. Install lld (e.g. sudo apt install lld) or select a different linker.");
			}
		}

		if (!canBuild)
		{
			ImGui::EndDisabled();
		}
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.8f, 0.3f, 0.1f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.4f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.7f, 0.2f, 0.0f, 1.0f));

		if (ImGui::Button("Stop All"))
		{
			StopBuild();
		}

		ImGui::PopStyleColor(3);
	}

	ImGui::SameLine();

	if (ImGui::Button("Add Compiler"))
	{
		ImGui::OpenPopup("AddCompilerPopup");
	}

	if (ImGui::BeginPopup("AddCompilerPopup"))
	{
		if (ImGui::MenuItem("GCC"))
		{
			AddCompilerEntry("gcc");
		}

		if (ImGui::MenuItem("Clang"))
		{
			AddCompilerEntry("clang");
		}

		ImGui::EndPopup();
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(m_showDependencyWindow);

	if (ImGui::Button("Dependencies"))
	{
		m_showDependencyWindow = true;
	}

	ImGui::EndDisabled();

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(m_showDependencyWindow ?
			"Dependency window is already open" :
			"Manage system dependencies required for compiler building");
	}

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(30.0f, 0.0f));
	ImGui::SameLine();

	ImGui::Separator();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::SelectPreset(std::string_view name)
{
	if (name.empty())
	{
		m_currentPresetName.clear();
		g_buildSettings = SBuildSettings{};
		m_savedPresetSnapshot = SBuildSettings{};
		m_presetManager.SetLastUsedPreset("");

		std::vector<std::string> removablePaths;

		for (auto const& compiler : g_compilerRegistry.GetCompilers())
		{
			if (compiler.isRemovable)
			{
				removablePaths.push_back(compiler.path);
			}
		}

		for (std::string const& path : removablePaths)
		{
			g_compilerRegistry.RemoveCompiler(path);
		}

		g_compilerRegistry.Scan();
		g_dependencyManager.ScanAllDependencies();
		CreateTabsFromBuildSettings(SBuildSettings{});
	}
	else
	{
		SBuildSettings loadedSettings;

		if (m_presetManager.LoadPreset(name, loadedSettings))
		{
			m_currentPresetName = name;
			m_presetManager.SetLastUsedPreset(name);
			g_buildSettings.installDirectory = loadedSettings.installDirectory;
			g_buildSettings.globalHostCompiler = loadedSettings.globalHostCompiler;
			g_buildSettings.dependencyLocationSelections = loadedSettings.dependencyLocationSelections;
			CreateTabsFromBuildSettings(loadedSettings);
			m_savedPresetSnapshot = loadedSettings;
			g_dependencyWindow.LoadLocationSelectionsFromPresets();
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::Console, "Preset '{}' could not be loaded — reverting to no preset", name);
			SelectPreset("");
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::ShowPresetSaveDialog(bool saveAs)
{
	m_presetSaveAs = saveAs;
	RefreshPresetNames();
	std::memset(m_presetNameBuffer, 0, sizeof(m_presetNameBuffer));
	std::memset(m_presetDescriptionBuffer, 0, sizeof(m_presetDescriptionBuffer));

	if (!m_currentPresetName.empty())
	{
		size_t copyLen{ std::min(m_currentPresetName.size(), sizeof(m_presetNameBuffer) - 1) };
		m_currentPresetName.copy(m_presetNameBuffer, copyLen);
		m_presetNameBuffer[copyLen] = '\0';

		auto const descIt{ m_presetDescriptions.find(m_currentPresetName) };

		if (descIt != m_presetDescriptions.end())
		{
			copyLen = std::min(descIt->second.size(), sizeof(m_presetDescriptionBuffer) - 1);
			descIt->second.copy(m_presetDescriptionBuffer, copyLen);
			m_presetDescriptionBuffer[copyLen] = '\0';
		}
	}

	m_dialogDiffPresetName.clear();
	m_showPresetSaveDialog = true;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderPresetSaveDialog()
{
	char const* const popupTitle{ m_presetSaveAs ? "Save As" : "Save Preset" };

	if (m_showPresetSaveDialog)
	{
		ImGui::OpenPopup(popupTitle);
		m_showPresetSaveDialog = false;
	}

	if (ImGui::BeginPopupModal(popupTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		// Pre-compute diff lines and derive panel width before rendering any widgets
		std::string_view const dialogName{ m_presetNameBuffer };
		bool const nameIsExisting{ std::ranges::find(m_availablePresets, dialogName) != m_availablePresets.end() };

		if (nameIsExisting && dialogName != m_dialogDiffPresetName)
		{
			bool const ok{ m_presetManager.ParsePreset(dialogName, m_dialogDiffSnapshot) };

			if (ok)
			{
				m_dialogDiffPresetName = dialogName;
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::File, "PresetSaveDialog: failed to parse preset '{}' for diff", dialogName);
				m_dialogDiffPresetName.clear();
			}
		}

		std::vector<std::string> diffLines;

		if (nameIsExisting && !m_dialogDiffPresetName.empty())
		{
			diffLines = GeneratePresetDiff(CreateBuildSettingsFromTabs(), m_dialogDiffSnapshot);
		}

		float panelWidth{ 400.0f };
		float const scrollbarW{ ImGui::GetStyle().ScrollbarSize };
		float const innerPad{ ImGui::GetStyle().WindowPadding.x * 2.0f };

		for (auto const& line : diffLines)
		{
			panelWidth = std::max(panelWidth, ImGui::CalcTextSize(line.c_str()).x + innerPad + scrollbarW);
		}

		panelWidth = std::min(panelWidth, ImGui::GetIO().DisplaySize.x * 0.8f);

		ImGui::Text("Preset name:");
		ImGui::SetNextItemWidth(panelWidth);

		if (!m_presetSaveAs)
		{
			ImGui::InputText("##PresetName", m_presetNameBuffer, sizeof(m_presetNameBuffer),
				ImGuiInputTextFlags_ReadOnly);
		}
		else
		{
			ImGui::InputText("##PresetName", m_presetNameBuffer, sizeof(m_presetNameBuffer));
		}

		if (m_presetSaveAs && !m_availablePresets.empty())
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Existing presets:");
			ImGui::BeginChild("ExistingPresets", ImVec2(panelWidth, 80), true);

			for (auto const& presetName : m_availablePresets)
			{
				if (ImGui::Selectable(presetName.c_str()))
				{
					size_t copyLen{ std::min(presetName.length(), sizeof(m_presetNameBuffer) - 1) };
					presetName.copy(m_presetNameBuffer, copyLen);
					m_presetNameBuffer[copyLen] = '\0';

					auto const descIt{ m_presetDescriptions.find(presetName) };
					std::string_view const existingDescription{ (descIt != m_presetDescriptions.end()) ? descIt->second : std::string_view{} };
					copyLen = std::min(existingDescription.length(), sizeof(m_presetDescriptionBuffer) - 1);
					existingDescription.copy(m_presetDescriptionBuffer, copyLen);
					m_presetDescriptionBuffer[copyLen] = '\0';
				}
			}

			ImGui::EndChild();
		}

		if (nameIsExisting && !m_dialogDiffPresetName.empty())
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Changes vs saved:");

			int const numLines{ diffLines.empty() ? 1 : static_cast<int>(diffLines.size()) };
			float const lineH{ ImGui::GetTextLineHeightWithSpacing() };
			float const padding{ ImGui::GetStyle().WindowPadding.y * 2.0f };
			float const maxH{ ImGui::GetIO().DisplaySize.y * 0.4f };
			float const panelH{ std::min(static_cast<float>(numLines) * lineH + padding, maxH) };

			ImGui::BeginChild("DiffPanel", ImVec2(panelWidth, panelH), true);

			if (diffLines.empty())
			{
				ImGui::TextDisabled("No changes — identical to saved preset");
			}
			else
			{
				for (auto const& line : diffLines)
				{
					ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "%s", line.c_str());
				}
			}

			ImGui::EndChild();
		}
		else if (!dialogName.empty() && !nameIsExisting)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Will create a new preset.");
		}

		ImGui::Spacing();
		ImGui::TextDisabled("Description (optional):");
		ImGui::InputTextMultiline("##PresetDescription", m_presetDescriptionBuffer,
			sizeof(m_presetDescriptionBuffer), ImVec2(panelWidth, 52));

		ImGui::Separator();

		bool canSave{ !std::string_view(m_presetNameBuffer).empty() };

		if (!canSave)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::Button(m_presetSaveAs ? "Save" : "Apply"))
		{
			SaveToPreset(std::string(m_presetNameBuffer), std::string(m_presetDescriptionBuffer));
			ImGui::CloseCurrentPopup();
		}

		if (!canSave)
		{
			ImGui::EndDisabled();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::SaveToPreset(std::string_view presetName, std::string_view description)
{
	g_dependencyWindow.SaveLocationSelectionsToPresets();

	for (auto const& tab : m_compilerTabs)
	{
		if (tab.isOpen && (tab.name.empty() || tab.folderName.empty()))
		{
			gLog.Warning(Tge::Logging::ETarget::Console,
				"Preset '{}': excluded {} tab '{}' — {} not set",
				presetName,
				tab.kind == ECompilerKind::Gcc ? "gcc" : "clang",
				tab.tabDisplayName,
				(tab.name.empty() && tab.folderName.empty()) ? "version and folder" :
				tab.name.empty() ? "version" : "folder name");
		}
	}

	SBuildSettings tabBasedSettings = CreateBuildSettingsFromTabs();
	bool success{ m_presetManager.SavePreset(presetName, description, tabBasedSettings) };

	if (success)
	{
		m_currentPresetName = presetName;
		m_presetManager.SetLastUsedPreset(presetName);
		RefreshPresetNames();
		m_savedPresetSnapshot = tabBasedSettings;
		gLog.Info(Tge::Logging::ETarget::Console, "Preset saved: {}", presetName);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::Console, "Failed to save preset: {}", presetName);
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GetPresetDescription(std::string_view presetName)
{
	std::string presetFile{ std::format("{}/config/{}.preset", g_dataDir, presetName) };
	std::ifstream file(presetFile);

	if (file.is_open())
	{
		std::string line;

		while (std::getline(file, line) && (line.empty() || line[0] != '['))
		{
			if (!line.empty() && line[0] != '#' && line.starts_with("description="))
			{
				return line.substr(12);
			}
		}
	}

	return "";
}

//////////////////////////////////////////////////////////////////////////
SBuildSettings CCompilerGUI::CreateBuildSettingsFromTabs() const
{
	SBuildSettings settings = g_buildSettings;
	settings.compilerEntries.clear();

	for (auto const& tab : m_compilerTabs)
	{
		if (tab.isOpen && !tab.name.empty() && !tab.folderName.empty())
		{
			SCompilerEntry entry;
			entry.name = tab.name;
			entry.folderName = tab.folderName;
			entry.numJobs = tab.numJobs;
			entry.keepDependencies = tab.keepDependencies;
			entry.keepSources = tab.keepSources;
			entry.hostCompiler = tab.hostCompiler;
			entry.compilerType = tab.kind == ECompilerKind::Gcc ? "gcc" : "clang";
			entry.clangSettings = tab.clangSettings;
			entry.gccSettings = tab.gccSettings;
			settings.compilerEntries.push_back(std::move(entry));

		}
	}

	return settings;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::CreateTabsFromBuildSettings(SBuildSettings const& settings)
{
	gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Creating tabs from build settings with {} compiler entries", settings.compilerEntries.size());

	m_compilerTabs.clear();
	m_compilerUnits.clear();

	for (auto const& entry : settings.compilerEntries)
	{
		if (!entry.name.value.empty() && !entry.folderName.value.empty())
		{
			SCompilerTab tab;
			tab.name = entry.name.value;
			tab.folderName = entry.folderName.value;
			tab.numJobs = entry.numJobs.value;
			tab.keepDependencies = entry.keepDependencies.value;
			tab.keepSources = entry.keepSources.value;
			tab.isOpen = true;

			if (!entry.compilerType.value.empty())
			{
				tab.kind = entry.compilerType.value == "gcc" ? ECompilerKind::Gcc : ECompilerKind::Clang;
				tab.clangSettings = entry.clangSettings;
				tab.gccSettings = entry.gccSettings;

				if (tab.clangSettings.numNinjaLinkJobs == 0)
				{
					bool const isRelease{ tab.clangSettings.buildType == EBuildType::Release
						|| tab.clangSettings.buildType == EBuildType::MinSizeRel };
					tab.clangSettings.numNinjaLinkJobs = isRelease
						? g_cpuInfo.GetDefaultLinkJobs()
						: g_cpuInfo.GetDefaultLinkJobsConservative();
				}
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::File,
					"CreateTabsFromBuildSettings: entry '{}' has no compilerType, defaulting to gcc", entry.name);
				tab.kind = ECompilerKind::Gcc;
			}

			tab.id = m_nextCompilerTabId++;

			std::string const idStr{ std::to_string(tab.id) };
			tab.tabDisplayName  = tab.name;
			tab.tabLabel        = tab.name + "###" + idStr;
			tab.idTabCompiler   = "##TabCompiler" + idStr;
			tab.idDeleteSources = "Delete Sources##" + idStr;
			tab.idSourcesPopup  = "Confirm Delete Sources##" + idStr;
			tab.idDeleteBuild   = "Delete Build##" + idStr;
			tab.idBuildPopup    = "Confirm Delete Build##" + idStr;
			tab.idShowCommand   = "Show Command##" + idStr;
			tab.idAdvanced      = "Advanced Configuration##" + idStr;
			tab.idCopyLog       = "Copy Log##" + idStr;
			tab.idSaveLog       = "Save Log##" + idStr;
			tab.idCompilerLog   = "CompilerLog##" + idStr;

			if (!entry.hostCompiler.value.empty())
			{
				std::filesystem::path const hostPath{ entry.hostCompiler.value };
				std::string const filename = hostPath.filename().string();

				if (filename.find("++") != std::string::npos)
				{
					tab.hostCompiler = entry.hostCompiler.value;
				}
				else
				{
					// Old preset format: hostCompiler was a bin directory — migrate to full executable path
					std::string const exe = (tab.kind == ECompilerKind::Clang) ? "clang++" : "g++";
					tab.hostCompiler = entry.hostCompiler.value + "/" + exe;
					gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Migrated old-format compiler override: {} -> {}", entry.hostCompiler, tab.hostCompiler);
				}
			}

			m_compilerTabs.push_back(std::move(tab));
			CreateUnitForTab(m_compilerTabs.back());

			gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Created tab from preset entry (index={})", m_compilerTabs.size() - 1);
		}
	}

	gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Created {} tabs from preset", m_compilerTabs.size());
}
} // namespace Ctrn
