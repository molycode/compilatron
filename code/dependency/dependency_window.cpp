#include "dependency/dependency_window.hpp"
#include "build/build_settings.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "gui/preset_manager.hpp"
#include <imgui.h>
#include <filesystem>
#include <format>

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
void CDependencyWindow::SaveDialogState()
{
	g_stateManager.SetDepWindow(m_dialogPosX, m_dialogPosY, m_dialogWidth, m_dialogHeight);
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::LoadDialogState()
{
	g_stateManager.GetDepWindow(m_dialogPosX, m_dialogPosY, m_dialogWidth, m_dialogHeight);

	if (m_dialogWidth < 800.0f || m_dialogHeight < 600.0f ||
	    m_dialogWidth > 3840.0f || m_dialogHeight > 2160.0f)
	{
		m_dialogWidth = 1400.0f;
		m_dialogHeight = 1000.0f;
		m_dialogPosX = -1.0f;
		m_dialogPosY = -1.0f;
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::SaveActivePreset()
{
	SaveLocationSelectionsToPresets();

	std::string const name{ g_stateManager.GetActivePreset() };

	if (!name.empty())
	{
		std::string const desc{ g_presetManager.GetDescription(name) };

		if (!g_presetManager.SavePreset(name, desc, g_buildSettings))
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "DependencyTab: Failed to save active preset '{}'", name);
		}
	}
}
} // namespace Ctrn
