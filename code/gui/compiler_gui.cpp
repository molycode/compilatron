#include "gui/compiler_gui.hpp"
#include "dependency/dependency_window.hpp"
#include "build/compiler_builder.hpp"
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
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <expected>
#include <format>
#ifndef CTRN_PLATFORM_WINDOWS
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif // CTRN_PLATFORM_WINDOWS

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
CCompilerGUI::CCompilerGUI() = default;

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::Initialize()
{
	m_compilerBuilder = std::make_unique<CCompilerBuilder>();
	m_compilerBuilder->Initialize();
	m_versionManager.Initialize();

	m_currentPresetName = g_stateManager.GetActivePreset();

	if (m_currentPresetName.empty())
	{
		m_currentPresetName = "Default";
		g_stateManager.SetActivePreset("Default");
	}

	{
		SBuildSettings loadedSettings;

		if (m_presetManager.LoadPreset(m_currentPresetName, loadedSettings))
		{
			g_buildSettings.installDirectory = loadedSettings.installDirectory;
			g_buildSettings.globalHostCompiler = loadedSettings.globalHostCompiler;
			g_buildSettings.dependencyLocationSelections = loadedSettings.dependencyLocationSelections;
			CreateTabsFromBuildSettings(loadedSettings);
		}
		else
		{
			gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Creating preset '{}'", m_currentPresetName);

			if (!m_presetManager.SavePreset(m_currentPresetName, "", loadedSettings))
			{
				gLog.Warning(Tge::Logging::ETarget::File, "CompilerGUI: Failed to create preset '{}'", m_currentPresetName);
			}
		}
	}

	// Initialize compiler registry — must be before dep manager since Scan() populates it
	g_compilerRegistry.Initialize();
	g_compilerRegistry.Scan();

	// Initialize dependency manager (deferred from static construction)
	g_dependencyManager.InitializeAllDependencies();
	g_dependencyManager.ScanAllDependencies();

	// Raw format: Console colors carry level signal; timestamp + message is sufficient
	gLog.RegisterListener(this,
		[this](Tge::Logging::SLogMessage const& msg)
		{
			m_globalLog.emplace_back(msg.level, std::format("[{}] {}", msg.formattedTimestamp, msg.message));
		}, Tge::Logging::EMessageFormat::Raw);

	RefreshPresetNames();

	g_dependencyWindow.Initialize();
	g_dependencyWindow.LoadLocationSelectionsFromPresets();
}

//////////////////////////////////////////////////////////////////////////
CCompilerGUI::~CCompilerGUI()
{
	gLog.Info(Tge::Logging::ETarget::File, "CompilerGUI: Destructor called - starting shutdown sequence");

	gLog.UnregisterListener(this);

	g_dependencyWindow.Terminate();
	g_compilerRegistry.Terminate();

	if (m_isBuilding)
	{
		StopBuild();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::Render()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Settings"))
		{
			if (ImGui::MenuItem("GitHub Token..."))
			{
				m_showSettingsDialog = true;
				std::string currentToken{ m_versionManager.GetGitHubToken() };
				size_t const len = std::min(currentToken.size(), sizeof(m_tokenInputBuffer) - 1);
				currentToken.copy(m_tokenInputBuffer, len);
				m_tokenInputBuffer[len] = '\0';
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About Compilatron..."))
			{
				m_showAboutDialog = true;
			}

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	RenderMainPanel();
	UpdateBuildStatus();
	RenderSettingsDialog();
	RenderCompilerBrowserDialog();
	RenderCommandDialog();
	RenderGccAdvancedDialog();
	RenderClangAdvancedDialog();
	RenderPresetSaveDialog();
	RenderRemoveCompilerDialog();

	if (m_showDependencyWindow)
	{
		bool const libffiEnabled{ std::any_of(m_compilerTabs.begin(), m_compilerTabs.end(),
			[](SCompilerTab const& tab)
			{
				return tab.kind == ECompilerKind::Clang && tab.clangSettings.enableLibffi.value;
			}) };
		g_dependencyManager.SetDynamicRequired("libffi", false, libffiEnabled);

		bool const terminfoEnabled{ std::any_of(m_compilerTabs.begin(), m_compilerTabs.end(),
			[](SCompilerTab const& tab)
			{
				return tab.kind == ECompilerKind::Clang && tab.clangSettings.enableTerminfo.value;
			}) };
		g_dependencyManager.SetDynamicRequired("libtinfo", false, terminfoEnabled);

		bool const libxml2Enabled{ std::any_of(m_compilerTabs.begin(), m_compilerTabs.end(),
			[](SCompilerTab const& tab)
			{
				return tab.kind == ECompilerKind::Clang && tab.clangSettings.enableLibxml2.value;
			}) };
		g_dependencyManager.SetDynamicRequired("libxml2", false, libxml2Enabled);

		bool const zlibEnabled{ std::any_of(m_compilerTabs.begin(), m_compilerTabs.end(),
			[](SCompilerTab const& tab)
			{
				return tab.kind == ECompilerKind::Clang && tab.clangSettings.enableZlib.value;
			}) };
		g_dependencyManager.SetDynamicRequired("zlib", false, zlibEnabled);

		bool const goldPluginEnabled{ std::any_of(m_compilerTabs.begin(), m_compilerTabs.end(),
			[](SCompilerTab const& tab)
			{
				return tab.kind == ECompilerKind::Clang && tab.clangSettings.enableGoldPlugin.value;
			}) };
		g_dependencyManager.SetDynamicRequired("binutils-include", false, goldPluginEnabled);

		m_showDependencyWindow = g_dependencyWindow.Render();
	}

	RenderAboutDialog();

}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderMainPanel()
{
	ImGuiIO& io = ImGui::GetIO();
	float menuBarHeight{ ImGui::GetFrameHeight() };
	ImVec2 windowSize = ImVec2(io.DisplaySize.x, io.DisplaySize.y - menuBarHeight);

	ImGui::SetNextWindowPos(ImVec2(0, menuBarHeight));
	ImGui::SetNextWindowSize(windowSize);
	ImGui::Begin("Compilatron", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

	RenderPresetControls();

	float totalHeight{ ImGui::GetContentRegionAvail().y };
	float minLogHeight{ 100.0f };
	float maxLogHeight{ totalHeight * 0.7f };

	m_logPanelHeight = std::max(minLogHeight, std::min(m_logPanelHeight, maxLogHeight));

	float globalLogControlsHeight{ ImGui::GetTextLineHeightWithSpacing() * 2.5f };
	float totalGlobalLogHeight{ m_logPanelHeight + globalLogControlsHeight };
	float topContainerHeight{ totalHeight - totalGlobalLogHeight - 8.0f };

	ImGui::BeginChild("CompilerTabsContainer", ImVec2(0, topContainerHeight), false);

	if (ImGui::BeginTabBar("MainTabs"))
	{
		for (auto& tab : m_compilerTabs)
		{
			if (tab.isOpen)
			{
				bool isOpen{ true };

				ImGuiTabItemFlags tabFlags = tab.selectOnOpen ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				tab.selectOnOpen = false;

				auto unitIt = m_compilerUnits.find(tab.id);
				bool const isCompiling{ unitIt != m_compilerUnits.end() && unitIt->second != nullptr &&
					(unitIt->second->GetStatus() == ECompilerStatus::Building ||
					 unitIt->second->GetStatus() == ECompilerStatus::Cloning) };

				int numColorsPushed{ 0 };

				if (isCompiling)
				{
					ImGui::PushStyleColor(ImGuiCol_Tab,                ImVec4(0.38f, 0.10f, 0.62f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabHovered,         ImVec4(0.50f, 0.18f, 0.78f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabSelected,        ImVec4(0.50f, 0.18f, 0.78f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabDimmed,          ImVec4(0.26f, 0.07f, 0.44f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected,  ImVec4(0.34f, 0.09f, 0.56f, 1.0f));
					numColorsPushed = 5;
				}
				else if (!IsTabComplete(tab) || !AreRequiredDependenciesAvailable(tab.kind))
				{
					ImGui::PushStyleColor(ImGuiCol_Tab,                ImVec4(0.55f, 0.30f, 0.05f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabHovered,         ImVec4(0.65f, 0.40f, 0.10f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabSelected,        ImVec4(0.65f, 0.40f, 0.10f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabDimmed,          ImVec4(0.55f, 0.30f, 0.05f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected,  ImVec4(0.55f, 0.30f, 0.05f, 1.0f));
					numColorsPushed = 5;
				}

				std::string const activeLabel{ isCompiling
					? (tab.tabDisplayName + " [Building]###" + std::to_string(tab.id))
					: tab.tabLabel };

				if (ImGui::BeginTabItem(activeLabel.c_str(), &isOpen, tabFlags))
				{
					ImGui::PopStyleColor(numColorsPushed);
					numColorsPushed = 0;
					RenderCompilerTab(tab);
					ImGui::EndTabItem();
				}

				if (numColorsPushed > 0)
				{
					ImGui::PopStyleColor(numColorsPushed);
				}

				if (!isOpen)
				{
					m_compilerToRemove = tab.name;
					m_showRemoveCompilerDialog = true;
				}
			}
		}

		ImGui::EndTabBar();

		for (auto const& tab : m_compilerTabs)
		{
			if (!tab.isOpen)
			{
				m_compilerUnits.erase(tab.id);
			}
		}

		m_compilerTabs.erase(
			std::remove_if(m_compilerTabs.begin(), m_compilerTabs.end(),
				[](SCompilerTab const& tab) { return !tab.isOpen; }),
			m_compilerTabs.end()
		);
	}
	else
	{
		ImGui::NewLine();
		ImGui::NewLine();
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Add compiler tabs above to begin building compilers.");
		ImGui::NewLine();
	}

	ImGui::EndChild();

	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));

	ImGui::Button("##splitter", ImVec2(-1, 4.0f));

	if (ImGui::IsItemActive())
	{
		m_logPanelHeight -= ImGui::GetIO().MouseDelta.y;
		m_logPanelHeight = std::max(minLogHeight, std::min(m_logPanelHeight, maxLogHeight));
	}

	ImGui::SetItemTooltip("Drag to resize log panel");
	ImGui::PopStyleColor(3);

	RenderGlobalLogPanel();

	ImGui::End();
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GetCpuName() const
{
	static std::string cachedCpuName;

	if (cachedCpuName.empty())
	{
#ifdef CTRN_PLATFORM_LINUX
		std::ifstream cpuinfo("/proc/cpuinfo");
		std::string line;
		bool foundCpuName{ false };

		while (!foundCpuName && std::getline(cpuinfo, line))
		{
			if (line.find("model name") == 0)
			{
				size_t pos{ line.find(":") };

				if (pos != std::string::npos && pos + 2 < line.length())
				{
					cachedCpuName = line.substr(pos + 2);
					foundCpuName = true;
				}
			}
		}
#endif // CTRN_PLATFORM_LINUX

		if (cachedCpuName.empty())
		{
			cachedCpuName = "Unknown CPU";
		}
	}

	return cachedCpuName;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::RenderTextFieldWithContextMenu(char const* label, char* buffer, size_t bufferSize)
{
	ImGui::SetNextItemWidth(400);
	bool changed{ ImGui::InputText(label, buffer, bufferSize) };

	if (ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Copy"))
		{
			ImGui::SetClipboardText(buffer);
		}

		if (ImGui::MenuItem("Paste"))
		{
			char const* clipboardText = ImGui::GetClipboardText();

			if (clipboardText)
			{
				std::string_view const clipText{ clipboardText };
				size_t const copyLen = std::min(clipText.size(), bufferSize - 1);
				clipText.copy(buffer, copyLen);
				buffer[copyLen] = '\0';
				changed = true;
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Select All"))
		{
			ImGui::SetKeyboardFocusHere(-1);
		}

		if (ImGui::MenuItem("Clear"))
		{
			buffer[0] = '\0';
			changed = true;
		}

		ImGui::EndPopup();
	}

	return changed;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::OpenUrlInBrowser(std::string_view url)
{
#ifdef CTRN_PLATFORM_WINDOWS
	std::string const urlStr{ url };
	HINSTANCE const result{ ShellExecuteA(nullptr, "open", urlStr.c_str(), nullptr, nullptr, SW_SHOWNORMAL) };
	bool const success{ reinterpret_cast<intptr_t>(result) > 32 };

	if (!success)
	{
		gLog.Warning(Tge::Logging::ETarget::File, "GUI: Failed to open URL in browser: {}", url);
	}

	return success;
#else // CTRN_PLATFORM_WINDOWS
	// Double-fork: the grandchild runs xdg-open/open in a new session, fully
	// detached from our process group, so it is never killed by our cleanup.
	std::string const urlStr{ url };

#ifdef CTRN_PLATFORM_MACOS
	char const* const opener{ "open" };
#else
	char const* const opener{ "xdg-open" };
#endif // CTRN_PLATFORM_MACOS

	pid_t const pid{ fork() };

	if (pid < 0)
	{
		gLog.Warning(Tge::Logging::ETarget::File, "GUI: fork() failed opening URL: {}", url);
		return false;
	}

	if (pid == 0)
	{
		// First child: fork again immediately so the grandchild is orphaned
		// and reparented to init — no zombie, no process-group membership.
		if (fork() == 0)
		{
			setsid();

			// Redirect stdin/stdout/stderr to /dev/null
			int const devNull{ open("/dev/null", O_RDWR) };

			if (devNull >= 0)
			{
				dup2(devNull, STDIN_FILENO);
				dup2(devNull, STDOUT_FILENO);
				dup2(devNull, STDERR_FILENO);

				if (devNull > STDERR_FILENO)
				{
					close(devNull);
				}
			}

			execlp(opener, opener, urlStr.c_str(), nullptr);
			_exit(1); // execlp failed
		}

		_exit(0); // First child exits immediately
	}

	// Parent: wait for the short-lived first child only
	waitpid(pid, nullptr, 0);
	return true;
#endif // CTRN_PLATFORM_WINDOWS
}

//////////////////////////////////////////////////////////////////////////
std::expected<void, std::string> CCompilerGUI::OpenFolder(std::string_view path)
{
	if (!std::filesystem::exists(path))
	{
		return std::unexpected(std::format("Folder does not exist: {}", path));
	}

	if (!std::filesystem::is_directory(path))
	{
		return std::unexpected(std::format("Path is not a directory: {}", path));
	}

	std::string command;

#ifdef CTRN_PLATFORM_WINDOWS
	command = std::format("explorer.exe \"{}\"", path);
#elif defined(CTRN_PLATFORM_MACOS)
	command = std::format("open \"{}\"", path);
#else
	struct SCandidate
	{
		std::string_view tool;
		std::string_view args;
	};

	static constexpr std::array<SCandidate, 4> Candidates = {{
		{"xdg-open", ""},
		{"nautilus", " &"},
		{"dolphin",  " &"},
		{"thunar",   " &"},
	}};

	for (auto const& candidate : Candidates)
	{
		if (command.empty() && CProcessExecutor::Execute(std::format("which {} 2>/dev/null", candidate.tool)).success)
		{
			command = std::format("{} \"{}\"{}",  candidate.tool, path, candidate.args);
		}
	}

	if (command.empty())
	{
		return std::unexpected("No file manager found. Please install xdg-open, nautilus, dolphin, or thunar.");
	}
#endif // CTRN_PLATFORM_WINDOWS

	auto const openResult = CProcessExecutor::Execute(command);

	if (!openResult.success)
	{
		return std::unexpected(std::format("Failed to open folder (exit code {})", openResult.exitCode));
	}

	return {};
}
} // namespace Ctrn
