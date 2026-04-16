#include "dependency/dependency_window.hpp"
#include "build/build_settings.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include <imgui.h>
#include <charconv>
#include <filesystem>
#include <format>
#include <fstream>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::Initialize()
{
	m_installationQueue.Start();
	m_pathQueue.Start();

	gDepLog.RegisterListener(this,
		[this](Tge::Logging::SLogMessage const& msg)
		{
			m_logDisplay.emplace_back(msg.level, std::format("[{}] {}", msg.formattedTimestamp, msg.message));

			if (m_logDisplay.size() > 100)
			{
				m_logDisplay.erase(
					m_logDisplay.begin(),
					m_logDisplay.begin() + static_cast<ptrdiff_t>(m_logDisplay.size() - 100));
			}
		}, Tge::Logging::EMessageFormat::Raw);

	LoadDialogState();
	LoadLocationSelectionsFromPresets();
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::Terminate()
{
	gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Terminate() called - saving state");
	SaveDialogState();

	gDepLog.UnregisterListener(this);

	m_installationQueue.Stop();
	m_pathQueue.Stop();
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyWindow::Render()
{
	// Update installation progress
	UpdateInstallationProgress();

	// If no saved position (-1), calculate centered position
	if (m_dialogPosX < 0 || m_dialogPosY < 0)
	{
		// Use ImGui viewport-relative centering for first-time positioning
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImVec2 workPos = viewport->WorkPos;
		ImVec2 workSize = viewport->WorkSize;

		m_dialogPosX = workPos.x + (workSize.x - m_dialogWidth) / 2.0f;
		m_dialogPosY = workPos.y + (workSize.y - m_dialogHeight) / 2.0f;
	}

	// Set window position and size from internal state
	ImGui::SetNextWindowPos(ImVec2(m_dialogPosX, m_dialogPosY), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(m_dialogWidth, m_dialogHeight), ImGuiCond_FirstUseEver);

	bool isOpen{ true };
	if (ImGui::Begin("Dependency Manager", &isOpen, ImGuiWindowFlags_NoCollapse))
	{
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		{
			ImGui::SetWindowFocus();
		}

		// Update internal state with current window position/size
		ImVec2 windowPos = ImGui::GetWindowPos();
		ImVec2 windowSize = ImGui::GetWindowSize();
		m_dialogPosX = windowPos.x;
		m_dialogPosY = windowPos.y;
		m_dialogWidth = windowSize.x;
		m_dialogHeight = windowSize.y;

		// Render the dependency management content
		RenderTabContent();

		ImGui::End();
	}

	// If user closed the window, save the final state
	if (!isOpen)
	{
		SaveDialogState();
	}

	return isOpen;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyWindow::GetDialogStateFilePath() const
{
	return g_dataDir + "/config/.dependency_tab_state";
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::SaveDialogState()
{
	// Ensure config directory exists
	std::string configDir{ g_dataDir + "/config" };
	std::error_code ec;
	std::filesystem::create_directories(configDir, ec);

	std::string stateFile{ GetDialogStateFilePath() };
	std::ofstream file(stateFile);

	if (file.is_open())
	{
		file << m_dialogPosX << "\n";
		file << m_dialogPosY << "\n";
		file << m_dialogWidth << "\n";
		file << m_dialogHeight << "\n";

		// Save dependency location selections — persists independently of preset system
		file << "[DependencyLocationSelections]\n";

		for (auto const& [identifier, path] : g_buildSettings.dependencyLocationSelections)
		{
			file << identifier << "=" << path << "\n";
		}

		gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Dialog state saved: pos({:.0f},{:.0f}) size({:.0f}x{:.0f})",
			m_dialogPosX, m_dialogPosY, m_dialogWidth, m_dialogHeight);
	}
	else
	{
		gDepLog.Warning(Tge::Logging::ETarget::File, "DependencyTab: Failed to save dialog state to: {}", stateFile);
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::LoadDialogState()
{
	std::string stateFile{ GetDialogStateFilePath() };
	std::string oldStateFile{ g_dataDir + "/config/.dependency_dialog_state" };

	// Check if we need to migrate from old dialog state file (same directory, just rename)
	if (!std::filesystem::exists(stateFile) && std::filesystem::exists(oldStateFile))
	{
		gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Migrating old state file: {} -> {}", oldStateFile, stateFile);
		std::error_code ec;
		std::filesystem::copy_file(oldStateFile, stateFile, ec);

		if (!ec)
		{
			std::filesystem::remove(oldStateFile, ec);
			gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Migration successful");
		}
		else
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "DependencyTab: Migration failed: {}", ec.message());
		}
	}

	gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Checking state file: {}", stateFile);

	if (std::filesystem::exists(stateFile))
	{
		gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: State file exists, attempting to open");
		std::ifstream file(stateFile);

		if (file.is_open())
		{
			std::string line;

			// Skip any comment lines that might exist in old state files
			bool foundPosX{ false };
			while (!foundPosX && std::getline(file, line))
			{
				if (!line.empty() && line[0] != '#')
				{
					// This is the first data line (posX)
					float v{};
					std::from_chars(line.data(), line.data() + line.size(), v);
					m_dialogPosX = v;
					gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Read posX: {:.0f}", m_dialogPosX);
					foundPosX = true;
				}
			}

			// Read remaining values
			if (std::getline(file, line)) {
				float v{};
				std::from_chars(line.data(), line.data() + line.size(), v);
				m_dialogPosY = v;
				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Read posY: {:.0f}", m_dialogPosY);
			}
			if (std::getline(file, line)) {
				float v{};
				std::from_chars(line.data(), line.data() + line.size(), v);
				m_dialogWidth = v;
				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Read width: {:.0f}", m_dialogWidth);
			}
			if (std::getline(file, line)) {
				float v{};
				std::from_chars(line.data(), line.data() + line.size(), v);
				m_dialogHeight = v;
				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Read height: {:.0f}", m_dialogHeight);
			}

			// Skip any remaining lines until the selections section header
			bool foundSection{ false };

			while (std::getline(file, line) && !foundSection)
			{
				foundSection = (line == "[DependencyLocationSelections]");
			}

			// Load dependency location selections if present
			if (foundSection)
			{
				while (std::getline(file, line))
				{
					if (!line.empty() && line[0] != '[')
					{
						size_t const sep{ line.find('=') };

						if (sep != std::string::npos)
						{
							std::string const identifier{ line.substr(0, sep) };
							std::string const path{ line.substr(sep + 1) };

							if (!identifier.empty() && !path.empty())
							{
								g_buildSettings.dependencyLocationSelections[identifier] = path;
							}
						}
					}
				}
			}

			// Log loaded values before validation
			gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Raw state loaded: pos({:.0f},{:.0f}) size({:.0f}x{:.0f})",
				m_dialogPosX, m_dialogPosY, m_dialogWidth, m_dialogHeight);

			// Basic validation
			if (m_dialogWidth >= 800 && m_dialogHeight >= 600 &&
			    m_dialogWidth <= 3840 && m_dialogHeight <= 2160)
			{
				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Dialog state validation passed: pos({:.0f},{:.0f}) size({:.0f}x{:.0f})",
					m_dialogPosX, m_dialogPosY, m_dialogWidth, m_dialogHeight);
			}
			else
			{
				// Log the invalid values before resetting
				float invalidWidth{ m_dialogWidth };
				float invalidHeight{ m_dialogHeight };

				// Reset to defaults if validation fails
				m_dialogWidth = 1400.0f;
				m_dialogHeight = 1000.0f;
				m_dialogPosX = -1.0f;
				m_dialogPosY = -1.0f;
				gDepLog.Warning(Tge::Logging::ETarget::File, "DependencyTab: Dialog state validation failed: size({:.0f}x{:.0f}) out of bounds, using defaults",
					invalidWidth, invalidHeight);
			}
		}
		else
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "DependencyTab: Failed to open dialog state file: {}", stateFile);
		}
	}
	else
	{
		gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Dialog state file not found: {}", stateFile);
	}
}
} // namespace Ctrn
