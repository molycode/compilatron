#include "gui/compiler_gui.hpp"
#include "gui/log_panel.hpp"

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
#include <ctime>
#include <format>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RegisterUnitLogListener(uint16_t tabId, CCompilerUnit& unit)
{
	unit.RegisterLogListener(this,
		[this, tabId](Tge::Logging::SLogMessage const& msg)
		{
			std::lock_guard<std::mutex> lock(m_unitLogsMutex);
			m_unitLogs[tabId].emplace_back(msg.level, std::format("[{}] {}", msg.formattedTimestamp, msg.message));
			RequestRedraw();
		});
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::ClearUnitLog(uint16_t tabId)
{
	std::lock_guard<std::mutex> lock(m_unitLogsMutex);
	m_unitLogs[tabId].clear();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::RenderGlobalLogPanel()
{
	m_logSaver.PollCompletion();

	ImGui::Text("Application Log");

	if (ImGui::Button("Copy Log##Global"))
	{
		CopyLogToClipboard("=== Global Application Log ===", m_globalLog);
	}

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Copy application log to clipboard");
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(m_logSaver.IsActive() || m_globalLog.empty());

	if (ImGui::Button("Save Log##Global"))
	{
		SaveGlobalLogToFile();
	}

	ImGui::EndDisabled();

	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("Save application log to file");
	}

	float const remainingHeight{ ImGui::GetContentRegionAvail().y };
	float const logDisplayHeight{ std::max(60.0f, remainingHeight - 2.0f) };
	RenderLogPanel("GlobalLogPanel", m_globalLog,
		"No application messages yet. Dependency management, startup info, and general status will appear here.",
		logDisplayHeight);
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::CopyCompilerLogToClipboard(uint16_t tabId)
{
	std::lock_guard<std::mutex> lock(m_unitLogsMutex);
	auto const it = m_unitLogs.find(tabId);

	if (it != m_unitLogs.end())
	{
		std::string displayName;

		for (auto const& tab : m_compilerTabs)
		{
			if (tab.id == tabId)
			{
				displayName = tab.name;
				break;
			}
		}

		std::string const header{ std::format("=== {} Build Log ===", displayName) };
		CopyLogToClipboard(header, it->second);
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::SaveCompilerLogToFile(uint16_t tabId)
{
	auto now{ std::time(nullptr) };
	auto tm{ *std::localtime(&now) };

	std::string displayName;

	for (auto const& tab : m_compilerTabs)
	{
		if (tab.id == tabId)
		{
			displayName = tab.name;
			break;
		}
	}

	std::string safeDisplayName{ displayName };
	std::replace(safeDisplayName.begin(), safeDisplayName.end(), ' ', '_');
	std::replace(safeDisplayName.begin(), safeDisplayName.end(), '/', '_');

	std::string logContent;

	{
		std::lock_guard<std::mutex> lock(m_unitLogsMutex);
		auto const it = m_unitLogs.find(tabId);

		if (it != m_unitLogs.end())
		{
			logContent = BuildLogText(it->second);
		}
	}

	if (!m_logSaver.IsActive() && !logContent.empty())
	{
		std::ostringstream headerStream;
		headerStream << std::format("=== {} Build Log ===\n", displayName);
		headerStream << "Generated: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
		headerStream << "========================\n\n";

		std::ostringstream filenameStream;
		filenameStream << safeDisplayName << "_log_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";

		m_logSaver.Save(headerStream.str(), filenameStream.str(), std::move(logContent));
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerGUI::SaveGlobalLogToFile()
{
	auto now{ std::time(nullptr) };
	auto tm{ *std::localtime(&now) };

	std::string logContent{ BuildLogText(m_globalLog) };

	if (!m_logSaver.IsActive() && !logContent.empty())
	{
		std::ostringstream headerStream;
		headerStream << "=== Global Application Log ===\n";
		headerStream << "Generated: " << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "\n";
		headerStream << "========================\n\n";

		std::ostringstream filenameStream;
		filenameStream << "Global_log_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".txt";

		m_logSaver.Save(headerStream.str(), filenameStream.str(), std::move(logContent));
	}
}
} // namespace Ctrn
