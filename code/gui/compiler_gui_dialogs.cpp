#include "gui/compiler_gui.hpp"
#include "build/compiler_builder.hpp"
#include "build/compiler_registry.hpp"
#include "dependency/dependency_manager.hpp"
#include "build/clang_unit.hpp"
#include "build/gcc_unit.hpp"
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
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <unistd.h>

namespace Ctrn
{
namespace
{
namespace fs = std::filesystem;

SCompilerScanResult ScanDirectoryForCompilers(std::string_view directory)
{
	SCompilerScanResult result;
	result.directory = std::string{ directory };

	// Collect all directories to scan: the root + each subdir's bin/ (or subdir itself)
	std::vector<std::string> dirsToScan;
	dirsToScan.push_back(result.directory);

	std::error_code ec;

	for (auto const& entry : fs::directory_iterator(result.directory, ec))
	{
		if (!ec && entry.is_directory())
		{
			std::string const binDir{ (entry.path() / "bin").string() };

			if (fs::exists(binDir) && fs::is_directory(binDir))
			{
				dirsToScan.push_back(binDir);
			}
			else
			{
				dirsToScan.push_back(entry.path().string());
			}
		}
	}

	if (ec)
	{
		gLog.Warning(Tge::Logging::ETarget::File,
			"CompilerGUI: Error scanning subdirectories of {}: {}", directory, ec.message());
	}

	for (std::string const& dir : dirsToScan)
	{
		std::error_code iterEc;

		for (auto const& entry : fs::directory_iterator(dir, iterEc))
		{
			if (!iterEc && entry.is_regular_file())
			{
				std::string const filename{ entry.path().filename().string() };

				bool const isGpp{ filename == "g++" || filename.starts_with("g++-") };
				bool const isClangpp{ filename == "clang++" || filename.starts_with("clang++-") };

				if (isGpp || isClangpp)
				{
					std::string const fullPath{ entry.path().string() };

					if (access(fullPath.c_str(), X_OK) == 0)
					{
						bool const alreadyFound{ std::ranges::any_of(result.compilers,
							[&](SCompiler const& c) { return c.path == fullPath; }) };

						if (!alreadyFound)
						{
							ECompilerKind const kind{ isGpp ? ECompilerKind::Gcc : ECompilerKind::Clang };
							std::string_view const versionCommand{ isGpp ? "-dumpfullversion" : "--version" };
							std::string_view const type{ isGpp ? "GCC" : "Clang" };
							std::string const version{
								g_dependencyManager.DetectVersion(fullPath, versionCommand) };

							SCompiler compiler;
							compiler.path               = fullPath;
							compiler.kind               = kind;
							compiler.version            = version;
							compiler.displayName        = std::format("{} {}", type, version);
							compiler.hasProblematicPath = HasProblematicPathCharacters(fullPath);
							compiler.isRemovable        = true;

							if (g_compilerRegistry.Contains(fullPath))
							{
								result.alreadyRegistered.push_back(std::move(compiler));
							}
							else
							{
								result.compilers.push_back(std::move(compiler));
							}
						}
					}
				}
			}
		}

		if (iterEc)
		{
			gLog.Warning(Tge::Logging::ETarget::File,
				"CompilerGUI: Error scanning {}: {}", dir, iterEc.message());
		}
	}

	return result;
}
} // namespace

void CCompilerGUI::RenderSettingsDialog()
{
	if (m_showSettingsDialog)
	{
		ImGui::OpenPopup("GitHub Token");
		m_showSettingsDialog = false;
	}

	if (ImGui::BeginPopupModal("GitHub Token", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("GitHub Personal Access Token (Optional)");
		ImGui::Separator();

		bool hasToken{ m_versionManager.HasGitHubToken() };
		auto& tracker = m_versionManager.GetRateLimitTracker();

		ImGui::Text("Rate Limit Status:");
		ImGui::Text("%s", tracker.GetStatusMessage(hasToken).c_str());
		ImGui::Separator();

		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), "%s", ICON_FA_CIRCLE_INFO);
		ImGui::SameLine();
		ImGui::TextWrapped("Benefits of adding a GitHub token:");
		ImGui::BulletText("Significantly higher API rate limit (unauthenticated requests are heavily throttled by GitHub)");
		ImGui::BulletText("Reliable version refresh (never fails partway)");
		ImGui::BulletText("Access to all branches and tags");
		ImGui::Separator();

		ImGui::Text("Token:");
		ImGui::SetNextItemWidth(400);
		bool tokenChanged{ ImGui::InputText("##token", m_tokenInputBuffer, sizeof(m_tokenInputBuffer), ImGuiInputTextFlags_Password) };

		if (ImGui::BeginPopupContextItem("token_context"))
		{
			if (ImGui::MenuItem("Paste"))
			{
				char const* clipboardText = ImGui::GetClipboardText();

				if (clipboardText != nullptr)
				{
					std::string_view const clipText{ clipboardText };
					size_t const copyLen = std::min(clipText.size(), sizeof(m_tokenInputBuffer) - 1);
					clipText.copy(m_tokenInputBuffer, copyLen);
					m_tokenInputBuffer[copyLen] = '\0';
					tokenChanged = true;
				}
			}

			ImGui::EndPopup();
		}

		if (tokenChanged)
		{
			m_tokenTestResult.clear();
		}

		ImGui::TextWrapped("Get token from: https://github.com/settings/tokens");
		ImGui::SameLine();

		if (ImGui::Button("Open in Browser"))
		{
			OpenUrlInBrowser("https://github.com/settings/tokens");
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Opens GitHub's Personal Access Token page in your default browser");
		}

		ImGui::TextWrapped("Required scope: 'public_repo' (read-only access)");
		ImGui::Separator();

		if (ImGui::Button("Test Token") && !m_tokenTestInProgress)
		{
			std::string testToken(m_tokenInputBuffer);

			if (testToken.empty())
			{
				m_tokenTestResult = "Token is empty";
			}
			else
			{
				m_tokenTestInProgress = true;
				m_tokenTestResult = "Testing...";

				if (testToken.length() < 20 || (!testToken.starts_with("ghp_") && !testToken.starts_with("github_pat_")))
				{
					m_tokenTestResult = "Token format looks incorrect (should start with 'ghp_' or 'github_pat_')";
					m_tokenTestInProgress = false;
				}
				else
				{
					m_tokenTestResult = "Token format looks good (save to test with GitHub)";
					m_tokenTestInProgress = false;
				}
			}
		}

		if (!m_tokenTestResult.empty())
		{
			ImGui::Text("%s", m_tokenTestResult.c_str());
		}

		ImGui::Separator();

		if (ImGui::Button("Save"))
		{
			std::string newToken(m_tokenInputBuffer);
			m_versionManager.SetGitHubToken(newToken);

			if (!m_versionManager.SaveToCache())
			{
				gLog.Warning(Tge::Logging::ETarget::Console, "Failed to persist GitHub token to cache");
			}

			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		if (ImGui::Button("Clear Token"))
		{
			m_tokenInputBuffer[0] = '\0';
			m_versionManager.ClearGitHubToken();

			if (!m_versionManager.SaveToCache())
			{
				gLog.Warning(Tge::Logging::ETarget::Console, "Failed to save version cache after clearing GitHub token");
			}

			m_tokenTestResult.clear();
		}

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Warning: Token will be permanently removed from configuration file and cannot be restored");
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::StartActualRefresh(ECompilerKind kind)
{
	std::string tabDisplayName{ kind == ECompilerKind::Gcc ? "gcc" : "clang" };
	bool foundTab{ false };

	for (auto const& tab : m_compilerTabs)
	{
		if (!foundTab && tab.kind == kind)
		{
			foundTab = true;
			tabDisplayName = tab.name.empty() ? tabDisplayName : tab.name;
		}
	}

	m_versionManager.RefreshVersionsAsync(kind, [this, tabDisplayName, kind](bool success, std::string const& error) {
		if (success)
		{
			std::vector<std::string> branches = m_versionManager.GetBranches(kind);
			std::vector<std::string> tags = m_versionManager.GetTags(kind);
			std::vector<std::string> versions = m_versionManager.GetVersions(kind);
			gLog.Info(Tge::Logging::ETarget::File, "{}: Successfully synced {} branches and {} tags ({} total versions)", tabDisplayName, branches.size(), tags.size(), versions.size());
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::File, "{}: Failed to fetch versions: {}", tabDisplayName, error);
		}
	});
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderCompilerBrowserDialog()
{
	if (m_showCompilerBrowserDialog)
	{
		ImGui::OpenPopup("Find Compilers");
		m_showCompilerBrowserDialog = false;
		m_scannedCompilers.clear();
		m_scannedAlreadyRegistered.clear();
		m_compilerSelectionStates.clear();
		m_scanCompleted = false;
	}

	if (ImGui::BeginPopupModal("Find Compilers", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Browse for a directory to scan for host compilers:");
		ImGui::Separator();

		ImGui::Text("Directory:");

		RenderTextFieldWithContextMenu("##DirectoryPath", m_customDirectoryBuffer, sizeof(m_customDirectoryBuffer));

		ImGui::SameLine();

		ImGui::BeginDisabled(m_isScanningDirectory.load());

		if (ImGui::Button("Browse"))
		{
			m_isScanningDirectory = true;
			m_scanCompleted = false;
			m_scannedCompilers.clear();
			m_compilerSelectionStates.clear();

			m_scanFuture = std::async(std::launch::async, []() -> SCompilerScanResult
			{
				std::string const cmd{
					"zenity --file-selection --directory"
					" --title=\"Select Directory to Scan for Compilers\" 2>/dev/null" };
				auto const zenityResult{ CProcessExecutor::Execute(cmd) };

				if (!zenityResult.success || zenityResult.output.empty())
				{
					gLog.Warning(Tge::Logging::ETarget::File,
						"CompilerGUI: zenity directory picker failed or was cancelled (exit code {})",
						zenityResult.exitCode);
					RequestRedraw();
					return SCompilerScanResult{};
				}

				std::string directory{ zenityResult.output };

				if (!directory.empty() && directory.back() == '\n')
				{
					directory.pop_back();
				}

				auto result{ ScanDirectoryForCompilers(directory) };
				RequestRedraw();
				return result;
			});
		}

		ImGui::EndDisabled();

		// Poll async scan result
		if (m_isScanningDirectory && m_scanFuture.valid())
		{
			if (m_scanFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
			{
				SCompilerScanResult scanResult{ m_scanFuture.get() };
				m_isScanningDirectory = false;

				if (!scanResult.directory.empty())
				{
					size_t const len{ std::min(scanResult.directory.size(),
						sizeof(m_customDirectoryBuffer) - 1) };
					scanResult.directory.copy(m_customDirectoryBuffer, len);
					m_customDirectoryBuffer[len] = '\0';
				}

				m_scannedCompilers = std::move(scanResult.compilers);
				m_scannedAlreadyRegistered = std::move(scanResult.alreadyRegistered);
				m_compilerSelectionStates.assign(m_scannedCompilers.size(), 1);
				m_scanCompleted = true;

				if (m_scannedCompilers.empty() && !std::string_view{ m_customDirectoryBuffer }.empty())
				{
					gLog.Warning(Tge::Logging::ETarget::Console,
						"CompilerGUI: No compilers found in {}", m_customDirectoryBuffer);
				}
			}
			else
			{
				ImGui::SameLine();
				ImGui::Text("Scanning...");
			}
		}

		ImGui::Separator();
		ImGui::Spacing();

		if (m_scanCompleted && (!m_scannedCompilers.empty() || !m_scannedAlreadyRegistered.empty()))
		{
			if (!m_scannedCompilers.empty())
			{
				if (ImGui::Button("Select All"))
				{
					std::ranges::fill(m_compilerSelectionStates, 1);
				}

				ImGui::SameLine();

				if (ImGui::Button("Select None"))
				{
					std::ranges::fill(m_compilerSelectionStates, 0);
				}

				ImGui::Spacing();

				for (size_t i{ 0 }; i < m_scannedCompilers.size(); ++i)
				{
					auto const& compiler{ m_scannedCompilers[i] };
					std::string const label{
						compiler.displayName + " — " + compiler.path + "##suite_" + std::to_string(i) };
					bool isSelected{ m_compilerSelectionStates[i] != 0 };

					if (ImGui::Checkbox(label.c_str(), &isSelected))
					{
						m_compilerSelectionStates[i] = isSelected ? 1 : 0;
					}
				}
			}

			if (!m_scannedAlreadyRegistered.empty())
			{
				if (!m_scannedCompilers.empty())
				{
					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();
				}

				ImGui::TextDisabled("Already in registry:");

				ImGui::BeginDisabled();

				for (auto const& compiler : m_scannedAlreadyRegistered)
				{
					std::string const label{ compiler.displayName + " — " + compiler.path };
					ImGui::TextDisabled("%s", label.c_str());
				}

				ImGui::EndDisabled();
			}

			ImGui::Spacing();
			ImGui::Separator();

			int const numSelected{ static_cast<int>(
				std::ranges::count(m_compilerSelectionStates, 1)) };

			if (numSelected > 0)
			{
				std::string const buttonLabel{
					numSelected == static_cast<int>(m_scannedCompilers.size())
						? "Add All"
						: std::format("Add Selected ({})", numSelected) };

				if (ImGui::Button(buttonLabel.c_str()))
				{
					int numAdded{ 0 };

					for (size_t i{ 0 }; i < m_scannedCompilers.size(); ++i)
					{
						if (m_compilerSelectionStates[i] != 0)
						{
							auto const& compiler{ m_scannedCompilers[i] };
							size_t const sizeBefore{ g_compilerRegistry.GetCompilers().size() };
							g_compilerRegistry.AddCompiler(compiler.path);

							if (g_compilerRegistry.GetCompilers().size() > sizeBefore)
							{
								++numAdded;
							}
						}
					}

					if (numAdded > 0)
					{
						g_compilerRegistry.SaveConfig();
					}

					gLog.Info(Tge::Logging::ETarget::Console,
						"CompilerGUI: Added {} compiler(s)", numAdded);

					m_scannedCompilers.clear();
					m_scannedAlreadyRegistered.clear();
					m_compilerSelectionStates.clear();
					m_scanCompleted = false;
					m_customDirectoryBuffer[0] = '\0';
					ImGui::CloseCurrentPopup();
				}
			}

			ImGui::SameLine();
		}
		else if (m_scanCompleted)
		{
			ImGui::TextDisabled("No compilers found in this directory.");
			ImGui::SameLine();
		}

		if (ImGui::Button("Close"))
		{
			m_scannedCompilers.clear();
			m_compilerSelectionStates.clear();
			m_scanCompleted = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderUnifiedCompilerSelector(std::string_view label, std::string_view currentCompiler,
	std::function<void(std::string const&)> onSelectionChanged, bool isPerTab,
	std::string_view /*compilerType*/)
{
	std::string const labelStr{label};
	std::string const currentCompilerStr{currentCompiler};

	std::vector<SCompiler> const& entries{ g_compilerRegistry.GetCompilers() };

	std::string const defaultPath{ GetEffectiveHostCompiler() };

	auto shortName = [&](std::string_view path) -> std::string
	{
		std::string result{ path };

		for (auto const& entry : entries)
		{
			if (entry.path == path)
			{
				result = entry.displayName;
			}
		}

		return result;
	};

	auto makeDisplayText = [&]() -> std::string
	{
		std::string result{ currentCompilerStr };

		if (currentCompiler.empty())
		{
			if (isPerTab)
			{
				result = defaultPath.empty() ? "Default (none)" : "Default (" + shortName(defaultPath) + ")";
			}
			else
			{
				result = defaultPath.empty() ? "(no compiler found)" : shortName(defaultPath);
			}
		}
		else
		{
			bool found{ false };

			for (auto const& entry : entries)
			{
				if (entry.path == currentCompiler)
				{
					result = entry.displayName;
					found = true;
				}
			}

			if (!found)
			{
				result = std::filesystem::exists(currentCompilerStr)
					? "(not in registry)"
					: "(binary missing)";
			}
		}

		return result;
	};

	std::string const displayText{ makeDisplayText() };

	bool isCurrentCompilerInvalid{ false };

	if (!currentCompiler.empty())
	{
		isCurrentCompilerInvalid = !IsCompilerValid(currentCompilerStr)
			|| !g_compilerRegistry.Contains(currentCompilerStr);
	}
	else
	{
		isCurrentCompilerInvalid = defaultPath.empty();
	}

	ImGui::SetNextItemWidth(250);

	if (isCurrentCompilerInvalid)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.4f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 0.6f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 0.8f));
	}

	// Extra overhead: separators + Browse + optional Default item
	int const numPopupItems{ static_cast<int>(entries.size()) + (isPerTab ? 4 : 2) };
	ImGuiComboFlags comboFlags{ ImGuiComboFlags_None };

	if (numPopupItems > 20)
	{
		comboFlags = ImGuiComboFlags_HeightLargest;
	}
	else if (numPopupItems > 8)
	{
		comboFlags = ImGuiComboFlags_HeightLarge;
	}

	bool const comboOpened{ ImGui::BeginCombo(labelStr.c_str(), displayText.c_str(), comboFlags) };

	if (comboOpened)
	{
		if (isCurrentCompilerInvalid)
		{
			ImGui::PopStyleColor(4);
		}

		if (isPerTab)
		{
			bool const isUsingDefault{ currentCompiler.empty() };
			std::string const defaultLabel{ (defaultPath.empty() ? "Default (none)" : "Default (" + shortName(defaultPath) + ")")
				+ "##" + labelStr + "_useDefault" };

			if (ImGui::Selectable(defaultLabel.c_str(), isUsingDefault))
			{
				onSelectionChanged("");
			}

			if (isUsingDefault)
			{
				ImGui::SetItemDefaultFocus();
			}

			ImGui::Separator();
		}

		std::string pendingRemovePath;

		for (size_t i = 0; i < entries.size(); ++i)
		{
			auto const& entry = entries[i];
			bool const isSelected{ entry.path == currentCompiler };
			bool const isAutoDefault{ currentCompiler.empty() && !isPerTab && i == 0 };
			std::string entryText{ entry.displayName };

			if (entry.hasProblematicPath)
			{
				entryText += " (invalid path)";
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
			}
			else if (!entry.isRemovable)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.80f, 1.00f, 1.0f));
			}

			std::string const uniqueId{ entryText + "##" + labelStr + "_" + entry.path };
			float const removeButtonWidth{ entry.isRemovable ? 22.0f : 0.0f };
			float const selectableWidth{ ImGui::GetContentRegionAvail().x - removeButtonWidth };

			if (ImGui::Selectable(uniqueId.c_str(), isSelected || isAutoDefault, 0, ImVec2(selectableWidth, 0)))
			{
				onSelectionChanged(entry.path);
			}

			if (entry.hasProblematicPath || !entry.isRemovable)
			{
				ImGui::PopStyleColor();
			}

			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Path: %s", entry.path.c_str());

				if (entry.hasProblematicPath)
				{
					ImGui::Separator();
					ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Invalid Path");
					ImGui::Text("This compiler path contains shell-problematic characters.");
					ImGui::Text("It cannot be used with build systems (CMake/autoconf).");
				}

				ImGui::EndTooltip();
			}

			if (entry.isRemovable)
			{
				ImGui::SameLine(0, 2);
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.6f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.6f, 0.1f, 0.1f, 0.8f));
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.4f, 0.4f, 1.0f));

				std::string const removeId{ "×##remove_" + entry.path };

				if (ImGui::SmallButton(removeId.c_str()))
				{
					pendingRemovePath = entry.path;
				}

				ImGui::PopStyleColor(4);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Remove from list");
				}
			}

			if (isSelected || isAutoDefault)
			{
				ImGui::SetItemDefaultFocus();
			}
		}

		if (!pendingRemovePath.empty())
		{
			if (currentCompiler == pendingRemovePath)
			{
				onSelectionChanged("");
			}

			g_compilerRegistry.RemoveCompiler(pendingRemovePath);
			g_compilerRegistry.SaveConfig();
			ImGui::CloseCurrentPopup();
		}

		ImGui::Separator();

		std::string const browseId{ "Browse...##" + labelStr + "_browse" };

		if (ImGui::Selectable(browseId.c_str()))
		{
			m_showCompilerBrowserDialog = true;
		}

		ImGui::EndCombo();
	}

	if (isCurrentCompilerInvalid && !comboOpened)
	{
		ImGui::PopStyleColor(4);

		if (ImGui::IsItemHovered() && !currentCompiler.empty())
		{
			ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f));
			ImGui::BeginTooltip();
			ImGui::TextWrapped("%s", currentCompilerStr.c_str());
			ImGui::EndTooltip();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderRemoveCompilerDialog()
{
	if (m_showRemoveCompilerDialog)
	{
		ImGui::OpenPopup("Remove Compiler?");
		m_showRemoveCompilerDialog = false;
	}

	if (ImGui::BeginPopupModal("Remove Compiler?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Are you sure you want to remove this compiler tab?");
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Compiler: %s", m_compilerToRemove.c_str());
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Warning: This will permanently remove the tab and all its settings:");
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "- Custom URL, display name, folder name");
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "- Job count and other build configurations");
		ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "- This cannot be undone once you save a preset");
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Your compiled files on disk will remain untouched.");
		ImGui::Separator();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));

		if (ImGui::Button("Remove"))
		{
			bool foundTabToRemove{ false };

			for (auto& tab : m_compilerTabs)
			{
				if (!foundTabToRemove && tab.name == m_compilerToRemove)
				{
					foundTabToRemove = true;
					tab.isOpen = false;
				}
			}

			gLog.Info(Tge::Logging::ETarget::Console, "Removed compiler tab: {}", m_compilerToRemove);
			ImGui::CloseCurrentPopup();
		}

		ImGui::PopStyleColor(3);

		ImGui::SameLine();

		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderCommandDialog()
{
	if (m_showCommandDialog)
	{
		m_commandDialogText.clear();
		m_commandDialogTabName.clear();

		for (auto const& tab : m_compilerTabs)
		{
			if (tab.id == m_commandDialogTabId)
			{
				m_commandDialogTabName = tab.name;

				auto unitIt = m_compilerUnits.find(tab.id);

				if (unitIt != m_compilerUnits.end() && unitIt->second != nullptr)
				{
					unitIt->second->UpdateBuildConfig(BuildConfigFromTab(tab));

					if (tab.kind == ECompilerKind::Gcc)
					{
						static_cast<CGccUnit*>(unitIt->second.get())->SetGccSettings(tab.gccSettings);
					}
					else
					{
						static_cast<CClangUnit*>(unitIt->second.get())->SetClangSettings(tab.clangSettings);
					}

					unitIt->second->GenerateCommands();
				}

				m_commandDialogText = GenerateCompilerCommands(tab);
			}
		}

		ImGui::OpenPopup("Command Preview");
		m_showCommandDialog = false;
	}

	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

	bool isOpen{ true };

	if (ImGui::BeginPopupModal("Command Preview", &isOpen, ImGuiWindowFlags_None))
	{
		if (!m_commandDialogText.empty())
		{
			ImGui::Text("Command Preview for: %s", m_commandDialogTabName.c_str());
			ImGui::Separator();

			ImGui::BeginChild("CommandText", ImVec2(0, -35), true, ImGuiWindowFlags_HorizontalScrollbar);
			ImGui::TextWrapped("%s", m_commandDialogText.c_str());
			ImGui::EndChild();

			ImGui::Separator();

			if (ImGui::Button("Copy to Clipboard"))
			{
				ImGui::SetClipboardText(m_commandDialogText.c_str());
			}

			ImGui::SameLine();
		}
		else
		{
			ImGui::Text("Error: Tab not found");
		}

		if (ImGui::Button("Close"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	if (!isOpen)
	{
		m_commandDialogTabId = 0;
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GenerateCompilerCommands(SCompilerTab const& tab) const
{
	auto unitIt = m_compilerUnits.find(tab.id);

	if (unitIt == m_compilerUnits.end() || unitIt->second == nullptr)
	{
		gLog.Warning(Tge::Logging::ETarget::File, "CompilerGUI: GenerateCompilerCommands: No unit found for tab '{}'", tab.id);
		return std::format("=== BUILD COMMANDS FOR {} ===\n\nError: Compiler unit not found.\n", tab.name);
	}

	CCompilerUnit const& unit = *unitIt->second;

	return std::format(
		"=== BUILD COMMANDS FOR {} ===\n\n"
		"1. CONFIGURE:\n{}\n\n"
		"2. BUILD:\n{}\n\n"
		"3. INSTALL:\n{}",
		tab.name,
		unit.GetConfigureCommand(),
		unit.GetBuildCommand(),
		unit.GetInstallCommand());
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderGccAdvancedDialog()
{
	if (m_showGccAdvancedDialog)
	{
		ImGui::OpenPopup("GCC Advanced Configuration");
		m_showGccAdvancedDialog = false;
	}

	ImGui::SetNextWindowSize(ImVec2(700, 600), ImGuiCond_Once);

	if (ImGui::BeginPopupModal("GCC Advanced Configuration", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		SCompilerTab* targetTab = nullptr;

		for (auto& tab : m_compilerTabs)
		{
			if (targetTab == nullptr && tab.id == m_gccAdvancedTabId && tab.kind == ECompilerKind::Gcc)
			{
				targetTab = &tab;
			}
		}

		if (targetTab != nullptr)
		{
			auto& gcc = targetTab->gccSettings;
			bool anyChanged{ false };

			ImGui::Text("Advanced Configuration for: %s", targetTab->folderName.c_str());
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Basic Configuration", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Enabled Languages:");
				ImGui::SetNextItemWidth(400);
				char languagesBuffer[256];
				size_t const lenLang = std::min(gcc.enabledLanguages.value.size(), sizeof(languagesBuffer) - 1);
				gcc.enabledLanguages.value.copy(languagesBuffer, lenLang);
				languagesBuffer[lenLang] = '\0';

				if (ImGui::InputText("##Languages", languagesBuffer, sizeof(languagesBuffer)))
				{
					gcc.enabledLanguages = languagesBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-languages (e.g., c,c++,fortran)");
				}

				anyChanged |= ImGui::Checkbox(gcc.enableLto.uiName.data(), &gcc.enableLto.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-lto: Enable Link-Time Optimization support");
				}

				ImGui::Text("Optimization Level:");
				ImGui::SetNextItemWidth(300);
				int optLevel{ static_cast<int>(gcc.optimizationLevel.value) };
				static constexpr std::array<char const*, 6> optLevels = {"O0 (No optimization)", "O1 (Basic)", "O2 (Standard)", "O3 (Aggressive)", "Os (Size)", "Ofast (Unsafe)"};

				if (ImGui::Combo("##Optimization", &optLevel, optLevels.data(), static_cast<int>(optLevels.size())))
				{
					gcc.optimizationLevel = static_cast<EOptimizationLevel>(optLevel);
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Default optimization level for the built GCC compiler");
				}

				anyChanged |= ImGui::Checkbox(gcc.generateDebugSymbols.uiName.data(), &gcc.generateDebugSymbols.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-debug: Generate debugging information for GDB and other debuggers");
				}
			}

			if (ImGui::CollapsingHeader("Core Functionality", ImGuiTreeNodeFlags_DefaultOpen))
			{
				anyChanged |= ImGui::Checkbox(gcc.enableBootstrap.uiName.data(), &gcc.enableBootstrap.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-bootstrap: Multi-stage build for consistency");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(gcc.enableShared.uiName.data(), &gcc.enableShared.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-shared: Build shared libraries");
				}

				anyChanged |= ImGui::Checkbox(gcc.useSystemZlib.uiName.data(), &gcc.useSystemZlib.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--with-system-zlib: Use proven system zlib instead of bundled");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(gcc.disableWerror.uiName.data(), &gcc.disableWerror.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--disable-werror: Don't treat warnings as errors during build");
				}

				anyChanged |= ImGui::Checkbox(gcc.modernCppAbi.uiName.data(), &gcc.modernCppAbi.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--with-default-libstdcxx-abi=new: Use modern C++ ABI");
				}
			}

			if (ImGui::CollapsingHeader("Architecture & Threading", ImGuiTreeNodeFlags_DefaultOpen))
			{
				anyChanged |= ImGui::Checkbox(gcc.disableMultilib.uiName.data(), &gcc.disableMultilib.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--disable-multilib: Build only for the target architecture");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(gcc.posixThreads.uiName.data(), &gcc.posixThreads.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-threads=posix: POSIX threading support");
				}

				anyChanged |= ImGui::Checkbox(gcc.enablePlugin.uiName.data(), &gcc.enablePlugin.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-plugin: Enable plugin architecture");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(gcc.enablePie.uiName.data(), &gcc.enablePie.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-default-pie: Security enhancement");
				}

				anyChanged |= ImGui::Checkbox(gcc.enableBuildId.uiName.data(), &gcc.enableBuildId.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--enable-linker-build-id: Build ID support for debugging");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(gcc.enableGold.uiName.data(), &gcc.enableGold.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("--enable-gold: set gold as the default linker in the built GCC.");
					ImGui::Text("Requires gold to be installed on the build system (binutils < 2.44).");
					ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "gold was removed from binutils 2.44+. Consider using lld instead.");
					ImGui::EndTooltip();
				}

				ImGui::Text("Target arch:");
				ImGui::SetNextItemWidth(200);
				char withArchBuffer[128];
				size_t copyLen{ std::min(gcc.withArch.value.size(), sizeof(withArchBuffer) - 1) };
				gcc.withArch.value.copy(withArchBuffer, copyLen);
				withArchBuffer[copyLen] = '\0';

				if (ImGui::InputText("##WithArch", withArchBuffer, sizeof(withArchBuffer)))
				{
					gcc.withArch = withArchBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--with-arch: Target CPU ISA (e.g. native, x86-64, haswell, cortex-a72)\nLeave empty to use the system default");
				}

				ImGui::SameLine();
				ImGui::Text("Target tune:");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(200);
				char withTuneBuffer[128];
				copyLen = std::min(gcc.withTune.value.size(), sizeof(withTuneBuffer) - 1);
				gcc.withTune.value.copy(withTuneBuffer, copyLen);
				withTuneBuffer[copyLen] = '\0';

				if (ImGui::InputText("##WithTune", withTuneBuffer, sizeof(withTuneBuffer)))
				{
					gcc.withTune = withTuneBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--with-tune: CPU scheduling target (e.g. generic, native, intel)\nLeave empty to use the system default");
				}

				ImGui::Text("Sysroot:");
				ImGui::SetNextItemWidth(400);
				char withSysrootBuffer[512];
				copyLen = std::min(gcc.withSysroot.value.size(), sizeof(withSysrootBuffer) - 1);
				gcc.withSysroot.value.copy(withSysrootBuffer, copyLen);
				withSysrootBuffer[copyLen] = '\0';

				if (ImGui::InputText("##WithSysroot", withSysrootBuffer, sizeof(withSysrootBuffer)))
				{
					gcc.withSysroot = withSysrootBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("--with-sysroot: Alternate sysroot for cross-compilation\nLeave empty for native builds");
				}
			}

			if (ImGui::CollapsingHeader("Internal Checking", ImGuiTreeNodeFlags_DefaultOpen))
			{
				anyChanged |= ImGui::Checkbox(gcc.enableChecking.uiName.data(), &gcc.enableChecking.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Enable internal consistency checks during compilation (slower but safer)");
				}

				if (gcc.enableChecking)
				{
					ImGui::Text("Checking Level:");
					int checkLevel{ 0 };

					if (gcc.checkingLevel.value == "yes")
					{
						checkLevel = 1;
					}
					else if (gcc.checkingLevel.value == "release")
					{
						checkLevel = 0;
					}
					else
					{
						checkLevel = 2;
					}

					ImGui::SetNextItemWidth(200);
					static constexpr std::array<char const*, 3> checkLevels = {"Release", "Yes", "No"};

					if (ImGui::Combo("##CheckLevel", &checkLevel, checkLevels.data(), static_cast<int>(checkLevels.size())))
					{
						switch (checkLevel)
						{
							case 0: gcc.checkingLevel = "release"; break;
							case 1: gcc.checkingLevel = "yes"; break;
							case 2: gcc.checkingLevel = "no"; break;
						}

						anyChanged = true;
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("--enable-checking=LEVEL");
					}
				}
			}

			if (ImGui::CollapsingHeader("Custom Flags", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("Custom C Flags:");
				ImGui::SetNextItemWidth(500);
				char customCFlagsBuffer[512];
				size_t const lenCF = std::min(gcc.customCFlags.value.size(), sizeof(customCFlagsBuffer) - 1);
				gcc.customCFlags.value.copy(customCFlagsBuffer, lenCF);
				customCFlagsBuffer[lenCF] = '\0';

				if (ImGui::InputText("##CustomCFlags", customCFlagsBuffer, sizeof(customCFlagsBuffer)))
				{
					gcc.customCFlags = customCFlagsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Custom CFLAGS for GCC build");
				}

				ImGui::Text("Custom C++ Flags:");
				ImGui::SetNextItemWidth(500);
				char customCxxFlagsBuffer[512];
				size_t const lenCXX = std::min(gcc.customCxxFlags.value.size(), sizeof(customCxxFlagsBuffer) - 1);
				gcc.customCxxFlags.value.copy(customCxxFlagsBuffer, lenCXX);
				customCxxFlagsBuffer[lenCXX] = '\0';

				if (ImGui::InputText("##CustomCxxFlags", customCxxFlagsBuffer, sizeof(customCxxFlagsBuffer)))
				{
					gcc.customCxxFlags = customCxxFlagsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Custom CXXFLAGS for GCC build");
				}

				ImGui::Text("Additional Configure Flags:");
				ImGui::SetNextItemWidth(500);
				char additionalFlagsBuffer[512];
				size_t const lenAF = std::min(gcc.additionalConfigureFlags.value.size(), sizeof(additionalFlagsBuffer) - 1);
				gcc.additionalConfigureFlags.value.copy(additionalFlagsBuffer, lenAF);
				additionalFlagsBuffer[lenAF] = '\0';

				if (ImGui::InputText("##AdditionalFlags", additionalFlagsBuffer, sizeof(additionalFlagsBuffer)))
				{
					gcc.additionalConfigureFlags = additionalFlagsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Additional configure script options");
				}
			}

			ImGui::Separator();

			if (ImGui::Button("Reset to defaults", ImVec2(160, 30)))
			{
				gcc.ResetToDefaults();
				anyChanged = true;
			}

			if (anyChanged)
			{
				SaveActivePreset();
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 120);

			if (ImGui::Button("Close", ImVec2(120, 30)))
			{
				ImGui::CloseCurrentPopup();
			}
		}
		else
		{
			ImGui::Text("Error: GCC tab not found!");

			if (ImGui::Button("Close", ImVec2(120, 30)))
			{
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderClangAdvancedDialog()
{
	if (m_showClangAdvancedDialog)
	{
		ImGui::OpenPopup("Clang Advanced Configuration");
		m_showClangAdvancedDialog = false;
	}

	ImGui::SetNextWindowSize(ImVec2(800, 700), ImGuiCond_Once);

	if (ImGui::BeginPopupModal("Clang Advanced Configuration", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		SCompilerTab* targetTab = nullptr;

		for (auto& tab : m_compilerTabs)
		{
			if (targetTab == nullptr && tab.id == m_clangAdvancedTabId && tab.kind == ECompilerKind::Clang)
			{
				targetTab = &tab;
			}
		}

		if (targetTab != nullptr)
		{
			auto& clang = targetTab->clangSettings;
			bool anyChanged{ false };

			if (ImGui::CollapsingHeader("Build Configuration", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Text("%s:", clang.buildType.uiName.data());
				ImGui::SetNextItemWidth(200.0f);
				int buildType{ static_cast<int>(clang.buildType.value) };
				static constexpr std::array<char const*, 4> buildTypes = {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"};

				if (ImGui::Combo("##BuildType", &buildType, buildTypes.data(), static_cast<int>(buildTypes.size())))
				{
					auto const IsRelease = [](EBuildType t)
					{
						return t == EBuildType::Release || t == EBuildType::MinSizeRel;
					};

					EBuildType const newType{ static_cast<EBuildType>(buildType) };

					if (IsRelease(clang.buildType) != IsRelease(newType))
					{
						clang.numNinjaLinkJobs = IsRelease(newType)
							? g_cpuInfo.GetDefaultLinkJobs()
							: g_cpuInfo.GetDefaultLinkJobsConservative();
					}

					clang.buildType = newType;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("CMAKE_BUILD_TYPE: Release (default), Debug, RelWithDebInfo, MinSizeRel");
				}

				ImGui::SameLine();
				ImGui::Text("%s:", clang.cppStandard.uiName.data());
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f);
				int cppStd{ static_cast<int>(clang.cppStandard.value) };
				static constexpr std::array<char const*, 7> cppStandards = {"C++11", "C++14", "C++17", "C++20", "C++23", "C++26", "C++29"};

				if (ImGui::Combo("##CppStandard", &cppStd, cppStandards.data(), static_cast<int>(cppStandards.size())))
				{
					clang.cppStandard = static_cast<ECppStandard>(cppStd);
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("CMAKE_CXX_STANDARD: C++23 default");
				}

				anyChanged |= ImGui::Checkbox(clang.cxxStandardRequired.uiName.data(), &clang.cxxStandardRequired.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("CMAKE_CXX_STANDARD_REQUIRED: Require exact C++ standard");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.cxxExtensions.uiName.data(), &clang.cxxExtensions.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("CMAKE_CXX_EXTENSIONS: Enable compiler-specific extensions");
				}

				ImGui::Text("%s:", clang.generator.uiName.data());
				ImGui::SetNextItemWidth(200.0f);
				int generator{ static_cast<int>(clang.generator.value) };
				static constexpr std::array<char const*, 2> generators = {"Unix Makefiles", "Ninja"};

				if (ImGui::Combo("##Generator", &generator, generators.data(), static_cast<int>(generators.size())))
				{
					clang.generator = static_cast<ECMakeGenerator>(generator);
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("CMake build system generator (Ninja recommended for performance)");
				}
			}

			if (ImGui::CollapsingHeader("Target Architectures", ImGuiTreeNodeFlags_DefaultOpen))
			{
				anyChanged |= ImGui::Checkbox(clang.targetX86.uiName.data(), &clang.targetX86.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("X86: Build support for Intel/AMD x86-64 architecture");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.targetAArch64.uiName.data(), &clang.targetAArch64.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("AArch64: Build support for ARM 64-bit architecture (Apple Silicon, modern ARM servers)");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.targetARM.uiName.data(), &clang.targetARM.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("ARM: Build support for ARM 32-bit architecture (embedded systems, older ARM devices)");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.targetRISCV.uiName.data(), &clang.targetRISCV.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("RISC-V: Build support for RISC-V open-source architecture");
				}

				ImGui::Text("%s:", clang.customTargets.uiName.data());
				ImGui::SetNextItemWidth(400.0f);
				char customTargetsBuffer[512];
				size_t copyLen{ std::min(clang.customTargets.value.length(), sizeof(customTargetsBuffer) - 1) };
				clang.customTargets.value.copy(customTargetsBuffer, copyLen);
				customTargetsBuffer[copyLen] = '\0';

				if (ImGui::InputText("##CustomTargets", customTargetsBuffer, sizeof(customTargetsBuffer)))
				{
					clang.customTargets = customTargetsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Additional LLVM_TARGETS_TO_BUILD (semicolon-separated)\nExample: WebAssembly;NVPTX;AMDGPU");
				}
			}

			if (ImGui::CollapsingHeader("LLVM Projects", ImGuiTreeNodeFlags_DefaultOpen))
			{
				anyChanged |= ImGui::Checkbox(clang.projectClang.uiName.data(), &clang.projectClang.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Clang: C/C++ compiler frontend (essential)");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectClangToolsExtra.uiName.data(), &clang.projectClangToolsExtra.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Clang Tools Extra: Additional tools like clang-tidy, clang-format, clang-include-fixer");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectLld.uiName.data(), &clang.projectLld.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLD: LLVM's fast, cross-platform linker (recommended)");
				}

				anyChanged |= ImGui::Checkbox(clang.projectCompilerRt.uiName.data(), &clang.projectCompilerRt.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Compiler-RT: Runtime libraries for sanitizers (AddressSanitizer, etc.)");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectLibcxx.uiName.data(), &clang.projectLibcxx.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("libc++: LLVM's C++ standard library implementation");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectLibcxxabi.uiName.data(), &clang.projectLibcxxabi.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("libc++abi: C++ ABI library for exception handling and RTTI");
				}

				anyChanged |= ImGui::Checkbox(clang.projectLibunwind.uiName.data(), &clang.projectLibunwind.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("libunwind: Stack unwinding library for exception handling");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectLldb.uiName.data(), &clang.projectLldb.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLDB: LLVM debugger");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectOpenmp.uiName.data(), &clang.projectOpenmp.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("OpenMP: Parallel programming API and runtime");
				}

				anyChanged |= ImGui::Checkbox(clang.projectMlir.uiName.data(), &clang.projectMlir.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("MLIR: Multi-Level Intermediate Representation framework for compiler infrastructure");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectFlang.uiName.data(), &clang.projectFlang.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Flang: Fortran frontend for LLVM");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectPolly.uiName.data(), &clang.projectPolly.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Polly: High-level loop and data-locality optimizer and optimization infrastructure");
				}

				anyChanged |= ImGui::Checkbox(clang.projectPstl.uiName.data(), &clang.projectPstl.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("PSTL: Parallel STL implementation");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.projectBolt.uiName.data(), &clang.projectBolt.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("BOLT: Binary Optimization and Layout Tool");
				}

				ImGui::Text("%s:", clang.customProjects.uiName.data());
				ImGui::SetNextItemWidth(400.0f);
				char customProjectsBuffer[512];
				size_t copyLen{ std::min(clang.customProjects.value.length(), sizeof(customProjectsBuffer) - 1) };
				clang.customProjects.value.copy(customProjectsBuffer, copyLen);
				customProjectsBuffer[copyLen] = '\0';

				if (ImGui::InputText("##CustomProjects", customProjectsBuffer, sizeof(customProjectsBuffer)))
				{
					clang.customProjects = customProjectsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Additional LLVM_ENABLE_PROJECTS (semicolon-separated)\nExample: libclc;cross-project-tests");
				}
			}

			if (ImGui::CollapsingHeader("Advanced Options", ImGuiTreeNodeFlags_DefaultOpen))
			{
				anyChanged |= ImGui::Checkbox(clang.enableRtti.uiName.data(), &clang.enableRtti.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("RTTI: Runtime Type Information for dynamic_cast and typeid operations");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.enableEh.uiName.data(), &clang.enableEh.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Exception Handling: C++ exception support (try/catch/throw)");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.enableZlib.uiName.data(), &clang.enableZlib.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("zlib compression: Enables compressed debug info and object files.\nRequires zlib1g-dev (Ubuntu/Debian), zlib-devel (Fedora), or zlib (Arch).");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.enableLibffi.uiName.data(), &clang.enableLibffi.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("libffi: Enables the LLVM JIT interpreter (lli).\nRequires libffi-dev (Ubuntu/Debian), libffi-devel (Fedora), or libffi (Arch).");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.optimizedTablegen.uiName.data(), &clang.optimizedTablegen.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Optimized TableGen: Use optimized LLVM TableGen for faster build times");
				}

				anyChanged |= ImGui::Checkbox(clang.enableAssertions.uiName.data(), &clang.enableAssertions.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLVM_ENABLE_ASSERTIONS: Enable internal LLVM assertions.\nCatches bugs but slows compilation. Recommended for Debug builds.");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.buildLlvmDylib.uiName.data(), &clang.buildLlvmDylib.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLVM_BUILD_LLVM_DYLIB: Build LLVM as a single shared library.\nReduces binary size significantly; required for Link LLVM dylib.");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.linkLlvmDylib.uiName.data(), &clang.linkLlvmDylib.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLVM_LINK_LLVM_DYLIB: Link tools against the LLVM shared library.\nRequires Build LLVM dylib to be enabled.");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.installUtils.uiName.data(), &clang.installUtils.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLVM_INSTALL_UTILS: Install utility tools (llvm-config, FileCheck, etc.).\nUseful for downstream projects that depend on LLVM.");
				}

				anyChanged |= ImGui::Checkbox(clang.enableTerminfo.uiName.data(), &clang.enableTerminfo.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLVM_ENABLE_TERMINFO: Enable colorized terminal output via ncurses.\nRequires libncurses-dev (Ubuntu/Debian), ncurses-devel (Fedora), or ncurses (Arch).");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.enableLibxml2.uiName.data(), &clang.enableLibxml2.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("LLVM_ENABLE_LIBXML2: Enable libxml2 support (used by some LLVM tools).\nRequires libxml2-dev (Ubuntu/Debian), libxml2-devel (Fedora), or libxml2 (Arch).");
				}

				ImGui::SameLine();
				anyChanged |= ImGui::Checkbox(clang.enableGoldPlugin.uiName.data(), &clang.enableGoldPlugin.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("Build gold linker plugin (LLVMgold.so)");
					ImGui::Text("Enables LTO with the GNU gold linker in the compiled Clang.");
					ImGui::Text("Requires binutils — install via the Dependencies window.");
					ImGui::EndTooltip();
				}

				if (clang.enableGoldPlugin.value &&
				    g_dependencyManager.GetSelectedPath("binutils").empty())
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "binutils missing");
				}

				ImGui::Text("%s:", clang.ltoMode.uiName.data());
				ImGui::SetNextItemWidth(150.0f);
				static constexpr std::array<char const*, 4> ltoModes = {"Off", "On", "Thin", "Full"};
				int currentLto{ 0 };
				bool foundLto{ false };

				for (int i = 0; i < static_cast<int>(ltoModes.size()); i++)
				{
					if (!foundLto && clang.ltoMode.value == ltoModes[i])
					{
						foundLto = true;
						currentLto = i;
					}
				}

				if (ImGui::Combo("##LtoMode", &currentLto, ltoModes.data(), static_cast<int>(ltoModes.size())))
				{
					clang.ltoMode = ltoModes[currentLto];
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("LLVM_ENABLE_LTO: Link-Time Optimization for the LLVM build itself");
					ImGui::Text("Off: no LTO (default, fastest build)");
					ImGui::Text("On: basic LTO — works with GCC or Clang host");
					ImGui::Text("Thin: parallel ThinLTO — Clang host required; moderate build time increase");
					ImGui::Text("Full: maximum optimization — Clang host required; slowest link, smallest binary");
					ImGui::EndTooltip();
				}

				if (clang.ltoMode.value == "Thin" || clang.ltoMode.value == "Full")
				{
					std::string const actualCompiler{ GetActualCompilerForTab(*targetTab) };
					bool const isClangHost{ actualCompiler.find("clang") != std::string::npos };

					if (!isClangHost)
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.1f, 1.0f), "Requires Clang host");
					}
				}

				ImGui::Text("%s:", clang.linker.uiName.data());
				ImGui::SetNextItemWidth(150.0f);
				static constexpr std::array<char const*, 4> linkers = {"default", "lld", "gold", "bfd"};
				int currentLinker{ 0 };
				bool foundLinker{ false };

				for (int i = 0; i < static_cast<int>(linkers.size()); i++)
				{
					if (!foundLinker && clang.linker.value == linkers[i])
					{
						foundLinker = true;
						currentLinker = i;
					}
				}

				if (ImGui::Combo("##Linker", &currentLinker, linkers.data(), static_cast<int>(linkers.size())))
				{
					clang.linker = linkers[currentLinker];
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Linker selection: default (system), lld (LLVM), gold (GNU), bfd (GNU)");
				}

				if (clang.linker.value == "lld" && !IsLldAvailableForTab(*targetTab))
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "ld.lld not found — install lld or use a different linker");
				}

				if (clang.linker.value == "lld")
				{
					// Poll async browse result
					if (m_lldBrowseActive
						&& m_lldBrowseFuture.valid()
						&& m_lldBrowseFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
					{
						std::string selectedPath{ m_lldBrowseFuture.get() };

						while (!selectedPath.empty() && std::isspace(static_cast<unsigned char>(selectedPath.back())))
						{
							selectedPath.pop_back();
						}

						if (!selectedPath.empty())
						{
							clang.lldOverridePath = selectedPath;
							anyChanged = true;
						}

						m_lldBrowseActive = false;
					}

					// Resolved path display
					std::string const resolvedLld{ FindLldForTab(*targetTab) };

					if (clang.lldOverridePath.value.empty())
					{
						if (!resolvedLld.empty())
						{
							ImGui::TextDisabled("Auto: %s", resolvedLld.c_str());
						}
						else
						{
							ImGui::TextDisabled("Auto: not found");
						}
					}

					// Override path input
					ImGui::Text("%s:", clang.lldOverridePath.uiName.data());
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);

					static char lldOverrideBuffer[1024]{};

					if (!m_lldBrowseActive)
					{
						size_t const copyLen{ std::min(clang.lldOverridePath.value.length(), sizeof(lldOverrideBuffer) - 1) };
						clang.lldOverridePath.value.copy(lldOverrideBuffer, copyLen);
						lldOverrideBuffer[copyLen] = '\0';
					}

					if (ImGui::InputText("##LldOverride", lldOverrideBuffer, sizeof(lldOverrideBuffer)))
					{
						clang.lldOverridePath = lldOverrideBuffer;
						anyChanged = true;
					}

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Explicit path to ld.lld used when building this compiler.");
						ImGui::Text("Leave empty to use auto-resolution (co-located with host compiler, then PATH).");
						ImGui::Text("Example: /usr/bin/ld.lld");
						ImGui::EndTooltip();
					}

					ImGui::SameLine();
					ImGui::BeginDisabled(m_lldBrowseActive);

					if (ImGui::Button("Browse##LldOverride"))
					{
						m_lldBrowseActive = true;
						m_lldBrowseFuture = std::async(std::launch::async, []() -> std::string
						{
							auto result{ CProcessExecutor::Execute(
								"zenity --file-selection --title=\"Select ld.lld\" 2>/dev/null").output };
							RequestRedraw();
							return result;
						});
					}

					ImGui::EndDisabled();

					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("Browse for ld.lld using file manager");
					}

					if (!clang.lldOverridePath.value.empty())
					{
						ImGui::SameLine();

						if (ImGui::Button("Clear##LldOverride"))
						{
							clang.lldOverridePath = std::string{};
							lldOverrideBuffer[0] = '\0';
							anyChanged = true;
						}

						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("Clear override — restore auto-resolution");
						}
					}
				}

				if (clang.linker.value == "gold" && clang.ltoMode.value != "Off")
				{
					std::string const actualCompiler{ GetActualCompilerForTab(*targetTab) };
					std::error_code ec;
					std::filesystem::path const resolved{ std::filesystem::canonical(actualCompiler, ec) };
					std::filesystem::path const compilerDir{ ec
						? std::filesystem::path{ actualCompiler }.parent_path()
						: resolved.parent_path() };

					if (!std::filesystem::exists(compilerDir / ".." / "lib" / "LLVMgold.so"))
					{
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "LLVMgold.so missing — use lld");
					}
				}

				if (clang.generator == ECMakeGenerator::Ninja)
				{
					int const calculatedDefault{ (clang.buildType == EBuildType::Release || clang.buildType == EBuildType::MinSizeRel)
						? g_cpuInfo.GetDefaultLinkJobs()
						: g_cpuInfo.GetDefaultLinkJobsConservative() };

					ImGui::SetNextItemWidth(200.0f);
					std::string const linkJobsLabel{ std::format("{} jobs", clang.numNinjaLinkJobs.value) };

					anyChanged |= ImGui::SliderInt(clang.numNinjaLinkJobs.uiName.data(), &clang.numNinjaLinkJobs.value, 1, 20, linkJobsLabel.c_str());

					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Parallel link jobs passed to Ninja via -DLLVM_PARALLEL_LINK_JOBS");
						ImGui::Text("Default: %d jobs for this build type (RAM-based)", calculatedDefault);
						ImGui::Text("Release builds: ~4 GiB per job, Debug builds: ~9 GiB per job");
						ImGui::EndTooltip();
					}
				}

				anyChanged |= ImGui::Checkbox(clang.buildWithInstallRpath.uiName.data(), &clang.buildWithInstallRpath.value);

				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("CMAKE_BUILD_WITH_INSTALL_RPATH - Use install RPATH during build");
					ImGui::Text("Workaround for problematic compiler paths (spaces, special chars)");
					ImGui::Text("Usually not needed - symlinks handle problematic paths automatically");
					ImGui::Text("Enable only if builds fail with RPATH relinking errors");
					ImGui::EndTooltip();
				}

				ImGui::Text("%s:", clang.customCFlags.uiName.data());
				ImGui::SetNextItemWidth(400.0f);
				char customCFlagsBuffer[512];
				size_t copyLen{ std::min(clang.customCFlags.value.length(), sizeof(customCFlagsBuffer) - 1) };
				clang.customCFlags.value.copy(customCFlagsBuffer, copyLen);
				customCFlagsBuffer[copyLen] = '\0';

				if (ImGui::InputText("##CustomCFlags", customCFlagsBuffer, sizeof(customCFlagsBuffer)))
				{
					clang.customCFlags = customCFlagsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Additional CMAKE_C_FLAGS");
				}

				ImGui::Text("%s:", clang.customCxxFlags.uiName.data());
				ImGui::SetNextItemWidth(400.0f);
				char customCxxFlagsBuffer[512];
				copyLen = std::min(clang.customCxxFlags.value.length(), sizeof(customCxxFlagsBuffer) - 1);
				clang.customCxxFlags.value.copy(customCxxFlagsBuffer, copyLen);
				customCxxFlagsBuffer[copyLen] = '\0';

				if (ImGui::InputText("##CustomCxxFlags", customCxxFlagsBuffer, sizeof(customCxxFlagsBuffer)))
				{
					clang.customCxxFlags = customCxxFlagsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Additional CMAKE_CXX_FLAGS");
				}

				ImGui::Text("%s:", clang.additionalConfigureFlags.uiName.data());
				ImGui::SetNextItemWidth(400.0f);
				char additionalFlagsBuffer[1024];
				copyLen = std::min(clang.additionalConfigureFlags.value.length(), sizeof(additionalFlagsBuffer) - 1);
				clang.additionalConfigureFlags.value.copy(additionalFlagsBuffer, copyLen);
				additionalFlagsBuffer[copyLen] = '\0';

				if (ImGui::InputText("##AdditionalFlags", additionalFlagsBuffer, sizeof(additionalFlagsBuffer)))
				{
					clang.additionalConfigureFlags = additionalFlagsBuffer;
					anyChanged = true;
				}

				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("Additional CMake configure flags (space-separated)");
				}
			}

			ImGui::Separator();

			if (ImGui::Button("Reset to defaults", ImVec2(160, 30)))
			{
				clang.ResetToDefaults();
				anyChanged = true;
			}

			if (anyChanged)
			{
				SaveActivePreset();
			}

			ImGui::SameLine();
			ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 120);

			if (ImGui::Button("Close", ImVec2(120, 30)))
			{
				ImGui::CloseCurrentPopup();
			}
		}
		else
		{
			ImGui::Text("Error: Could not find Clang tab with ID: %u", m_clangAdvancedTabId);

			if (ImGui::Button("Close", ImVec2(120, 30)))
			{
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderAboutDialog()
{
	if (m_showAboutDialog)
	{
		ImGui::OpenPopup("About Compilatron");
		m_showAboutDialog = false;
	}

	ImVec2 const center{ ImGui::GetMainViewport()->GetCenter() };
	ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f));

	if (ImGui::BeginPopupModal("About Compilatron", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
	{
		// Title and version
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "Compilatron");
		ImGui::SameLine();
		ImGui::TextDisabled(CTRN_VERSION);
		ImGui::Spacing();
		ImGui::TextWrapped("A GUI application for building GCC and Clang compilers from source.");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Source
		ImGui::TextDisabled("Source");
		constexpr char const* repoUrl{ "https://github.com/molycode/compilatron" };
		ImGui::InputText("##repo_url", const_cast<char*>(repoUrl), std::strlen(repoUrl) + 1,
			ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();

		if (ImGui::Button("Open"))
		{
			m_aboutUrlOpenFailed = !OpenUrlInBrowser(repoUrl);
		}

		if (m_aboutUrlOpenFailed)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Could not open browser");
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// License
		ImGui::TextDisabled("License");
		ImGui::Text("MIT \xC2\xA9 2026 Thomas Wollenzin");

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Third-party libraries
		ImGui::TextDisabled("Third-Party Libraries");
		ImGui::Spacing();

		constexpr std::array<std::pair<char const*, char const*>, 3> thirdParty{{
			{ "Dear ImGui 1.92.6", "MIT" },
			{ "GLFW",             "zlib/libpng" },
			{ "tge-core",         "MIT" },
		}};

		for (auto const& [name, license] : thirdParty)
		{
			ImGui::BulletText("%s", name);
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", license);
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// OK button — centered
		float const buttonWidth{ 120.0f };
		ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5f + ImGui::GetCursorPosX());

		if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		{
			m_aboutUrlOpenFailed = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
} // namespace Ctrn
