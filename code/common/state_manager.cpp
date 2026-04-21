#include "common/state_manager.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>

namespace Ctrn
{
CStateManager g_stateManager;

namespace fs = std::filesystem;

//////////////////////////////////////////////////////////////////////////
static std::string GetStateFilePath()
{
	return g_dataDir + "/config/state.json";
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::Initialize()
{
	std::string const stateFile{ GetStateFilePath() };

	if (fs::exists(stateFile))
	{
		std::ifstream file(stateFile);

		if (file.is_open())
		{
			nlohmann::json const j = nlohmann::json::parse(file, nullptr, false);

			if (!j.is_discarded())
			{
				m_activePreset   = j.value("activePreset",  m_activePreset);
				m_lastBrowseDir  = j.value("lastBrowseDir", m_lastBrowseDir);

				if (j.contains("mainWindow"))
				{
					auto const& w = j["mainWindow"];
					m_mainWindowX      = w.value("x",      m_mainWindowX);
					m_mainWindowY      = w.value("y",      m_mainWindowY);
					m_mainWindowWidth  = w.value("width",  m_mainWindowWidth);
					m_mainWindowHeight = w.value("height", m_mainWindowHeight);
				}

				if (j.contains("depWindow"))
				{
					auto const& w = j["depWindow"];
					m_depWindowX      = w.value("x",      m_depWindowX);
					m_depWindowY      = w.value("y",      m_depWindowY);
					m_depWindowWidth  = w.value("width",  m_depWindowWidth);
					m_depWindowHeight = w.value("height", m_depWindowHeight);
				}

				bool const validMainWindow{ m_mainWindowWidth  >= 400  && m_mainWindowHeight >= 300
				                         && m_mainWindowWidth  <= 3840 && m_mainWindowHeight <= 2160 };

				if (!validMainWindow)
				{
					gLog.Warning(Tge::Logging::ETarget::File, "StateManager: Invalid main window size ({}x{}) — using defaults",
						m_mainWindowWidth, m_mainWindowHeight);
					m_mainWindowX      = 100;
					m_mainWindowY      = 100;
					m_mainWindowWidth  = 1600;
					m_mainWindowHeight = 1050;
				}

				bool const validDepWindow{ m_depWindowWidth  >= 800  && m_depWindowHeight >= 600
				                        && m_depWindowWidth  <= 3840 && m_depWindowHeight <= 2160 };

				if (!validDepWindow)
				{
					m_depWindowX      = -1.0f;
					m_depWindowY      = -1.0f;
					m_depWindowWidth  = 1400.0f;
					m_depWindowHeight = 1000.0f;
				}

				gLog.Info(Tge::Logging::ETarget::File, "StateManager: Loaded — activePreset='{}' mainWindow=({},{} {}x{})",
					m_activePreset, m_mainWindowX, m_mainWindowY, m_mainWindowWidth, m_mainWindowHeight);
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::File, "StateManager: Failed to parse state.json — using defaults");
			}
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::File, "StateManager: Failed to open state.json — using defaults");
		}
	}
	else
	{
		gLog.Info(Tge::Logging::ETarget::File, "StateManager: state.json not found — using defaults");
	}

	g_mainWindowPosX    = m_mainWindowX;
	g_mainWindowPosY    = m_mainWindowY;
	g_mainWindowWidth   = m_mainWindowWidth;
	g_mainWindowHeight  = m_mainWindowHeight;
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::Terminate()
{
	std::error_code ec;
	fs::create_directories(g_dataDir + "/config", ec);

	std::string const stateFile{ GetStateFilePath() };
	std::ofstream file(stateFile);

	if (file.is_open())
	{
		nlohmann::json j;
		j["activePreset"]  = m_activePreset;
		j["lastBrowseDir"] = m_lastBrowseDir;
		j["mainWindow"]    = { {"x", m_mainWindowX}, {"y", m_mainWindowY},
		                       {"width", m_mainWindowWidth}, {"height", m_mainWindowHeight} };
		j["depWindow"]     = { {"x", m_depWindowX}, {"y", m_depWindowY},
		                       {"width", m_depWindowWidth}, {"height", m_depWindowHeight} };

		file << j.dump(2) << "\n";
		gLog.Info(Tge::Logging::ETarget::File, "StateManager: Saved state.json");
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "StateManager: Failed to write state.json");
	}
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::SetActivePreset(std::string_view name)
{
	m_activePreset = name;
}

//////////////////////////////////////////////////////////////////////////
std::string const& CStateManager::GetActivePreset() const
{
	return m_activePreset;
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::SetMainWindow(int x, int y, int width, int height)
{
	m_mainWindowX      = x;
	m_mainWindowY      = y;
	m_mainWindowWidth  = width;
	m_mainWindowHeight = height;
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::GetMainWindow(int& x, int& y, int& width, int& height) const
{
	x      = m_mainWindowX;
	y      = m_mainWindowY;
	width  = m_mainWindowWidth;
	height = m_mainWindowHeight;
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::SetDepWindow(float x, float y, float width, float height)
{
	m_depWindowX      = x;
	m_depWindowY      = y;
	m_depWindowWidth  = width;
	m_depWindowHeight = height;
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::GetDepWindow(float& x, float& y, float& width, float& height) const
{
	x      = m_depWindowX;
	y      = m_depWindowY;
	width  = m_depWindowWidth;
	height = m_depWindowHeight;
}

//////////////////////////////////////////////////////////////////////////
void CStateManager::SetLastBrowseDir(std::string_view filePath)
{
	fs::path const dir{ fs::path(filePath).parent_path() };
	std::string const dirStr{ dir.string() };

	if (!dirStr.empty() && fs::exists(dirStr) && fs::is_directory(dirStr))
	{
		m_lastBrowseDir = dirStr;
		gLog.Info(Tge::Logging::ETarget::File, "StateManager: Last browse dir saved: {}", m_lastBrowseDir);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "StateManager: SetLastBrowseDir: invalid directory '{}'", dirStr);
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CStateManager::GetLastBrowseDir() const
{
	std::string result{ m_lastBrowseDir };

	if (result.empty() || !fs::exists(result) || !fs::is_directory(result))
	{
		if (!g_dataDir.empty() && fs::exists(g_dataDir) && fs::is_directory(g_dataDir))
		{
			result = g_dataDir;
		}
		else
		{
			result = ".";
		}
	}

	return result;
}
} // namespace Ctrn
