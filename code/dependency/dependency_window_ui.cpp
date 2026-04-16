#include "dependency/dependency_window.hpp"
#include "dependency/dependency_checker.hpp"
#include "gui/log_panel.hpp"
#include "gui/status_icons.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include <imgui.h>
#include <algorithm>
#include <filesystem>
#include <format>
#include <sstream>
#include <ctime>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RenderTabContent()
{
	if (m_columnWidthsDirty)
	{
		RecalculateColumnWidths();
	}

	CheckFileDialogResults();
	HandlePathProcessingResults();

	auto allDeps = g_dependencyManager.GetAllDependencies();

	std::vector<SAdvancedDependencyInfo*> missingGcc;
	std::vector<SAdvancedDependencyInfo*> missingClang;
	std::vector<SAdvancedDependencyInfo*> deps;

	for (auto* dep : allDeps)
	{
		bool const missing{ dep->status != EDependencyStatus::Available && !dep->systemPackage.empty() };

		if (missing && dep->requiredForGcc)
		{
			missingGcc.push_back(dep);
		}

		if (missing && dep->requiredForClang)
		{
			missingClang.push_back(dep);
		}

		deps.push_back(dep);
	}

	RenderReadinessSection(missingGcc, missingClang);
	RenderDepsTable(deps);

	ImGui::Spacing();

	if (!missingGcc.empty())
	{
		std::string advice{ GetInstallAdvice(missingGcc) };

		if (!advice.empty())
		{
			std::string distro{ CDependencyChecker::GetDistroId() };

			if (!distro.empty())
			{
				distro[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(distro[0])));
			}

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "For GCC (%s):", distro.c_str());
			ImGui::SameLine();
			ImGui::Text("%s", advice.c_str());
			ImGui::SameLine();

			if (ImGui::SmallButton("Copy##gcc_advice"))
			{
				ImGui::SetClipboardText(advice.c_str());
			}
		}
	}

	if (!missingClang.empty())
	{
		std::string advice{ GetInstallAdvice(missingClang) };

		if (!advice.empty())
		{
			std::string distro{ CDependencyChecker::GetDistroId() };

			if (!distro.empty())
			{
				distro[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(distro[0])));
			}

			ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.6f, 1.0f), "For Clang (%s):", distro.c_str());
			ImGui::SameLine();
			ImGui::Text("%s", advice.c_str());
			ImGui::SameLine();

			if (ImGui::SmallButton("Copy##clang_advice"))
			{
				ImGui::SetClipboardText(advice.c_str());
			}
		}
	}

	for (auto const& [identifier, state] : m_executableSelectionStates)
	{
		RenderExecutableSelectionPopup(identifier);
	}

	RenderLocateDialog();

	ImGui::Separator();
	RenderLogArea();
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RenderReadinessSection(
	std::vector<SAdvancedDependencyInfo*> const& missingGcc,
	std::vector<SAdvancedDependencyInfo*> const& missingClang)
{
	ImGui::SeparatorText("Build Requirements");
	ImGui::SameLine();

	if (ImGui::SmallButton("Refresh"))
	{
		g_compilerRegistry.Scan();
		g_dependencyManager.ScanAllDependencies();
		LoadLocationSelectionsFromPresets();
		RecalculateColumnWidths();
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Re-scan system for installed dependencies");
	}

	// GCC readiness row
	{
		auto const [glyph, color] = missingGcc.empty()
			? GetDependencyIcon(EDependencyStatus::Available)
			: GetDependencyIcon(EDependencyStatus::Missing);

		ImGui::TextColored(color, "%s", glyph);
		ImGui::SameLine();
		ImGui::Text("GCC:  ");
		ImGui::SameLine();

		if (missingGcc.empty())
		{
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "All requirements met");
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Missing %zu", missingGcc.size());
		}
	}

	// Clang readiness row
	{
		auto const [glyph, color] = missingClang.empty()
			? GetDependencyIcon(EDependencyStatus::Available)
			: GetDependencyIcon(EDependencyStatus::Missing);

		ImGui::TextColored(color, "%s", glyph);
		ImGui::SameLine();
		ImGui::Text("Clang:");
		ImGui::SameLine();

		if (missingClang.empty())
		{
			ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "All requirements met");
		}
		else
		{
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Missing %zu", missingClang.size());
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RenderDepsTable(std::vector<SAdvancedDependencyInfo*> const& deps)
{
	ImGui::Spacing();
	ImGui::SeparatorText("Dependencies");

	constexpr ImGuiTableFlags tableFlags =
		ImGuiTableFlags_RowBg |
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_SizingFixedFit;

	if (ImGui::BeginTable("AllDeps", 6, tableFlags))
	{
		ImGui::TableSetupColumn("##status",    ImGuiTableColumnFlags_WidthFixed,    22.0f);
		ImGui::TableSetupColumn("Dependency",  ImGuiTableColumnFlags_WidthFixed,    m_nameColumnWidth);
		ImGui::TableSetupColumn("Location",    ImGuiTableColumnFlags_WidthFixed,    m_locationColumnWidth);
		ImGui::TableSetupColumn("Version",     ImGuiTableColumnFlags_WidthFixed,    m_versionColumnWidth);
		ImGui::TableSetupColumn("##action",    ImGuiTableColumnFlags_WidthFixed,    m_actionColumnWidth);
		ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableHeadersRow();

		for (auto* dep : deps)
		{
			bool const isAvailable{ dep->status == EDependencyStatus::Available };
			bool const inProgress{ m_activeInstallations[dep->identifier] };

			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);

			{
				auto const [glyph, color] = GetDependencyIcon(dep->status);
				ImGui::TextColored(color, "%s", glyph);
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(dep->identifier.c_str());

			ImGui::TableSetColumnIndex(2);

			if (inProgress)
			{
				std::string const displayPath{ GetDisplayPath(m_perDependencyPaths[dep->identifier]) };
				ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", displayPath.c_str());
			}
			else if (isAvailable)
			{
				if (dep->foundLocations.size() > 1)
				{
					int currentIdx{ 0 };

					for (int i{ 0 }; i < static_cast<int>(dep->foundLocations.size()); ++i)
					{
						if (dep->selectedLocation == &dep->foundLocations[i])
						{
							currentIdx = i;
						}
					}

					std::vector<std::string> items;

					for (auto const& loc : dep->foundLocations)
					{
						if (!loc.version.empty() && loc.version != "unknown")
						{
							items.push_back(std::format("{} ({})", loc.path, loc.version));
						}
						else
						{
							items.push_back(loc.path);
						}
					}

					ImGui::SetNextItemWidth(-FLT_MIN);

					std::string const& preview{ dep->foundLocations[currentIdx].path };

					if (ImGui::BeginCombo(std::format("##loc{}", dep->identifier).c_str(), preview.c_str()))
					{
						for (int i{ 0 }; i < static_cast<int>(items.size()); ++i)
						{
							bool const selected{ i == currentIdx };

							if (ImGui::Selectable(items[i].c_str(), selected))
							{
								dep->selectedLocation = &dep->foundLocations[i];
								SaveLocationSelectionsToPresets();
								SaveDialogState();
							}

							if (selected)
							{
								ImGui::SetItemDefaultFocus();
							}
						}

						ImGui::EndCombo();
					}
				}
				else if (dep->selectedLocation != nullptr)
				{
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", dep->selectedLocation->path.c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(auto-detected)");
				}
			}

			ImGui::TableSetColumnIndex(3);

			if (isAvailable && dep->selectedLocation != nullptr && !inProgress)
			{
				ImGui::TextColored(ImVec4(0.5f, 0.7f, 0.5f, 1.0f), "%s", dep->selectedLocation->version.c_str());
			}

			ImGui::TableSetColumnIndex(4);

			bool const hasLocatableLibrary{ dep->category == EDependencyCategory::Library
				&& dep->installFunc != nullptr };

			if (dep->category != EDependencyCategory::SystemUtility &&
			    (dep->category != EDependencyCategory::Library || hasLocatableLibrary))
			{
				if (inProgress)
					{
						auto const it{ m_installationStatus.find(dep->identifier) };

						if (it != m_installationStatus.end())
						{
							ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", it->second.c_str());
							ImGui::SameLine();
						}

						if (ImGui::SmallButton(std::format("Cancel##install_{}", dep->identifier).c_str()))
						{
							CancelInstallation(dep->identifier);
						}
					}
					else if (isAvailable)
					{
						if (ImGui::SmallButton(std::format("Override##{}", dep->identifier).c_str()))
						{
							m_locateDialog.isOpen = true;
							m_locateDialog.identifier = dep->identifier;
							m_locateDialog.depName = dep->identifier;
							m_locateDialog.pathBuffer = {};

							auto const it = m_perDependencyPaths.find(dep->identifier);

							if (it != m_perDependencyPaths.end() && !it->second.empty())
							{
								std::string const displayPath{ GetDisplayPath(it->second) };
								size_t const len{ std::min(displayPath.length(), size_t(1023)) };
								displayPath.copy(m_locateDialog.pathBuffer.data(), len);
								m_locateDialog.pathBuffer[len] = '\0';
							}
						}
					}
					else
					{
						if (ImGui::SmallButton(std::format("Locate##{}", dep->identifier).c_str()))
						{
							m_locateDialog.isOpen = true;
							m_locateDialog.identifier = dep->identifier;
							m_locateDialog.depName = dep->identifier;
							m_locateDialog.pathBuffer = {};

							auto const it = m_perDependencyPaths.find(dep->identifier);

							if (it != m_perDependencyPaths.end() && !it->second.empty())
							{
								std::string const displayPath{ GetDisplayPath(it->second) };
								size_t const len{ std::min(displayPath.length(), size_t(1023)) };
								displayPath.copy(m_locateDialog.pathBuffer.data(), len);
								m_locateDialog.pathBuffer[len] = '\0';
							}
						}
					}
			}

			ImGui::TableSetColumnIndex(5);
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", dep->description.c_str());
		}

		ImGui::EndTable();
	}
}


//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RenderStatusIcon(EDependencyStatus status)
{
	auto const [glyph, color] = GetDependencyIcon(status);
	ImGui::TextColored(color, "%s", glyph);
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RenderLogArea()
{
	m_logSaver.PollCompletion();

	ImGui::Text("Installation Log (%zu messages)", m_logDisplay.size());
	ImGui::SameLine();

	if (ImGui::SmallButton("Copy Log"))
	{
		CopyLogToClipboard("=== Installation Log ===", m_logDisplay);
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(m_logSaver.IsActive() || m_logDisplay.empty());

	if (ImGui::SmallButton("Save Log"))
	{
		SaveLog();
	}

	ImGui::EndDisabled();

	ImGui::Separator();

	float const remainingHeight{ ImGui::GetContentRegionAvail().y };
	float const logHeight{ std::max(60.0f, remainingHeight - 2.0f) };
	RenderLogPanel("LogArea", m_logDisplay, "No installation messages yet.", logHeight);
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyWindow::GetInstallAdvice(std::vector<SAdvancedDependencyInfo*> const& deps) const
{
	std::string result;

	if (!deps.empty())
	{
		std::string distro{ CDependencyChecker::GetDistroId() };

		std::string prefix;
		bool knownDistro{ true };

		if (distro == "ubuntu" || distro == "debian")
		{
			prefix = "sudo apt install";
		}
		else if (distro == "fedora")
		{
			prefix = "sudo dnf install -y";
		}
		else if (distro == "arch")
		{
			prefix = "sudo pacman -S";
		}
		else if (distro == "opensuse" || distro == "sles")
		{
			prefix = "sudo zypper install";
		}
		else
		{
			knownDistro = false;
		}

		if (knownDistro)
		{
			std::string packages;

			for (auto const* dep : deps)
			{
				if (!dep->systemPackage.empty())
				{
					packages += " ";

					std::string pkg{ dep->systemPackage };

					if (distro == "arch" && pkg == "ninja-build")
					{
						pkg = "ninja";
					}
					else if (distro == "arch" && pkg == "zlib1g-dev")
					{
						pkg = "zlib";
					}
					else if ((distro == "fedora" || distro == "opensuse" || distro == "sles") && pkg == "zlib1g-dev")
					{
						pkg = "zlib-devel";
					}
					else if (distro == "arch" && pkg == "libffi-dev")
					{
						pkg = "libffi";
					}
					else if ((distro == "fedora" || distro == "opensuse" || distro == "sles") && pkg == "libffi-dev")
					{
						pkg = "libffi-devel";
					}
					else if (distro == "arch" && pkg == "libncurses-dev")
					{
						pkg = "ncurses";
					}
					else if ((distro == "fedora" || distro == "opensuse" || distro == "sles") && pkg == "libncurses-dev")
					{
						pkg = "ncurses-devel";
					}
					else if (distro == "arch" && pkg == "libxml2-dev")
					{
						pkg = "libxml2";
					}
					else if ((distro == "fedora" || distro == "opensuse" || distro == "sles") && pkg == "libxml2-dev")
					{
						pkg = "libxml2-devel";
					}
					else if (distro == "arch" && pkg == "binutils-dev")
					{
						pkg = "binutils";
					}
					else if ((distro == "fedora" || distro == "opensuse" || distro == "sles") && pkg == "binutils-dev")
					{
						pkg = "binutils-devel";
					}

					packages += pkg;
				}
			}

			if (!packages.empty())
			{
				result = prefix + packages;
			}
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyWindow::GetDisplayPath(std::string_view absolutePath) const
{
	// For URLs, always show full path
	if (IsUrl(absolutePath))
	{
		return std::string{absolutePath};
	}

	// Convert to filesystem path for proper comparison
	std::filesystem::path inputPath{absolutePath};

	// Check if the path is inside the data directory
	std::error_code ec;
	std::filesystem::path relativePath = std::filesystem::relative(inputPath, g_dataDir, ec);

	std::string result{ std::string{absolutePath} };

	if (!ec)
	{
		std::string relativeStr{ relativePath.string() };

		if (!relativeStr.starts_with(".."))
		{
			result = relativeStr;
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyWindow::GetAbsolutePath(std::string_view displayPath) const
{
	// For URLs, always return as-is
	if (IsUrl(displayPath))
	{
		return std::string{displayPath};
	}

	std::filesystem::path inputPath{displayPath};
	std::string result{ std::string{displayPath} };

	if (!inputPath.is_absolute())
	{
		result = (std::filesystem::path(g_dataDir) / inputPath).string();
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::SaveLog()
{
	auto now{ std::time(nullptr) };
	auto tm{ *std::localtime(&now) };

	std::ostringstream filenameStream;
	filenameStream << "dependency_log_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";

	std::ostringstream headerStream;
	headerStream << "=== Installation Log ===\n";
	headerStream << "Generated: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
	headerStream << "========================\n\n";

	m_logSaver.Save(headerStream.str(), filenameStream.str(), BuildLogText(m_logDisplay));
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RenderLocateDialog()
{
	if (m_locateDialog.isOpen)
	{
		ImGui::OpenPopup("LocateDialog");
		m_locateDialog.isOpen = false;
	}

	std::string const modalTitle{ std::format("Locate {}###LocateDialog", m_locateDialog.depName) };

	if (ImGui::BeginPopupModal(modalTitle.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		bool const isBrowseActive{ m_fileBrowseActive[m_locateDialog.identifier] };

		ImGui::SetNextItemWidth(400.0f);
		ImGui::InputText("##LocatePath", m_locateDialog.pathBuffer.data(), m_locateDialog.pathBuffer.size());

		if (ImGui::BeginPopupContextItem("##LocatePathContext"))
		{
			if (ImGui::MenuItem("Copy"))
			{
				ImGui::SetClipboardText(m_locateDialog.pathBuffer.data());
			}

			if (ImGui::MenuItem("Paste"))
			{
				char const* clipboardText{ ImGui::GetClipboardText() };

				if (clipboardText != nullptr)
				{
					std::string_view const clipText{ clipboardText };
					size_t const copyLen{
						std::min(clipText.size(), m_locateDialog.pathBuffer.size() - 1) };
					clipText.copy(m_locateDialog.pathBuffer.data(), copyLen);
					m_locateDialog.pathBuffer[copyLen] = '\0';
				}
			}

			if (ImGui::MenuItem("Clear"))
			{
				m_locateDialog.pathBuffer[0] = '\0';
			}

			ImGui::EndPopup();
		}

		ImGui::SameLine();

		if (isBrowseActive)
		{
			ImGui::BeginDisabled();
			ImGui::Button("Browsing...");
			ImGui::EndDisabled();
		}
		else if (ImGui::Button("Browse"))
		{
			LaunchFileBrowser(m_locateDialog.identifier, m_locateDialog.depName);
		}

		ImGui::Separator();

		if (ImGui::Button("Cancel"))
		{
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();

		bool const hasPath{ !std::string_view{ m_locateDialog.pathBuffer.data() }.empty() };

		if (!hasPath)
		{
			ImGui::BeginDisabled();
		}

		if (ImGui::Button("Accept"))
		{
			std::string const path{ GetAbsolutePath(m_locateDialog.pathBuffer.data()) };
			ProcessPath(m_locateDialog.identifier, path);
			ImGui::CloseCurrentPopup();
		}

		if (!hasPath)
		{
			ImGui::EndDisabled();
		}

		ImGui::EndPopup();
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RenderExecutableSelectionPopup(std::string_view identifier)
{
	std::string const identStr{identifier};
	auto it = m_executableSelectionStates.find(identStr);

	if (it != m_executableSelectionStates.end())
	{
		SExecutableSelectionState& state = it->second;

		if (state.showSelectionPopup)
		{
			std::string popupId{ std::format("Select {} executable##{}", identStr, identStr) };

			if (ImGui::BeginPopupModal(popupId.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("Multiple %s executables found:", identStr.c_str());
				ImGui::Text("Please select which executable to use:");
				ImGui::Separator();

				for (size_t i = 0; i < state.availableExecutables.size(); ++i)
				{
					SExecutableInfo const& exe = state.availableExecutables[i];
					std::string label{ std::format("{} ({})", exe.path, exe.version) };

					if (ImGui::RadioButton(label.c_str(), static_cast<int>(i) == state.selectedIndex))
					{
						state.selectedIndex = static_cast<int>(i);
					}
				}

				ImGui::Separator();

				if (ImGui::Button("Cancel"))
				{
					state.showSelectionPopup = false;
					ImGui::CloseCurrentPopup();
				}

				ImGui::SameLine();

				if (ImGui::Button("Select") && state.selectedIndex >= 0 &&
				    state.selectedIndex < static_cast<int>(state.availableExecutables.size()))
				{
					SExecutableInfo const& selectedExe = state.availableExecutables[state.selectedIndex];

					gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: User selected {} executable: {}", identifier, selectedExe.path);

					bool success{ g_dependencyManager.RegisterAdditionalVersion(identifier, selectedExe.path, selectedExe.version) };

					if (success)
					{
						gDepLog.Info(Tge::Logging::ETarget::Console, "Registered {} executable: {} (v{})", identStr, selectedExe.path, selectedExe.version);
					}
					else
					{
						gDepLog.Warning(Tge::Logging::ETarget::Console, "Failed to register {} executable: {} (v{})", identStr, selectedExe.path, selectedExe.version);
					}

					state.showSelectionPopup = false;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (!ImGui::IsPopupOpen(popupId.c_str()))
			{
				ImGui::OpenPopup(popupId.c_str());
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::RecalculateColumnWidths()
{
	auto const allDeps = g_dependencyManager.GetAllDependencies();

	m_nameColumnWidth = 0.0f;
	m_locationColumnWidth = 0.0f;
	m_versionColumnWidth = 0.0f;

	for (auto const* dep : allDeps)
	{
		m_nameColumnWidth = std::max(m_nameColumnWidth,
			ImGui::CalcTextSize(dep->identifier.c_str()).x);
	}

	for (auto const* dep : allDeps)
	{
		if (dep->status == EDependencyStatus::Available)
		{
			if (dep->foundLocations.empty())
			{
				std::string_view const text{ "(auto-detected)" };
				m_locationColumnWidth = std::max(m_locationColumnWidth,
					ImGui::CalcTextSize(text.data(), text.data() + text.size()).x);
			}
			else
			{
				float const comboArrow{ dep->foundLocations.size() > 1 ? ImGui::GetFrameHeight() : 0.0f };

				for (auto const& loc : dep->foundLocations)
				{
					m_locationColumnWidth = std::max(m_locationColumnWidth,
						ImGui::CalcTextSize(loc.path.c_str()).x + comboArrow);

					m_versionColumnWidth = std::max(m_versionColumnWidth,
						ImGui::CalcTextSize(loc.version.c_str()).x);
				}
			}
		}
	}

	float const fp{ ImGui::GetStyle().FramePadding.x };

	auto buttonW = [&](std::string_view label)
	{
		return ImGui::CalcTextSize(label.data(), label.data() + label.size()).x + fp * 2.0f;
	};

	m_actionColumnWidth = buttonW("Locate");
	m_actionColumnWidth = std::max(m_actionColumnWidth, buttonW("Override"));

	// Also accommodate the widest installation status string + Cancel button (shown side-by-side)
	float const itemSpacing{ ImGui::GetStyle().ItemSpacing.x };
	float const installStatusW{
		ImGui::CalcTextSize("Installing...").x + itemSpacing + buttonW("Cancel") };
	m_actionColumnWidth = std::max(m_actionColumnWidth, installStatusW);

	float const padding{ ImGui::GetStyle().CellPadding.x * 2.0f };
	m_nameColumnWidth     += padding;
	m_locationColumnWidth += padding;
	m_versionColumnWidth  += padding;
	m_actionColumnWidth   += padding;
	m_columnWidthsDirty = false;
}

} // namespace Ctrn
