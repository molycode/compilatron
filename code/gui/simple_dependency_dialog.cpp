#include "gui/simple_dependency_dialog.hpp"
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#include <imgui.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic pop
#endif

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
CSimpleDependencyDialog::CSimpleDependencyDialog(CDependencyChecker& checker)
	: m_checker(checker)
{
}

//////////////////////////////////////////////////////////////////////////
bool CSimpleDependencyDialog::Render()
{
	if (m_showDialog)
	{
		ImGui::OpenPopup("Missing Dependencies");

		if (ImGui::BeginPopupModal("Missing Dependencies", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Required dependencies are missing:");
			ImGui::Separator();

			auto missingDeps = m_checker.GetMissingDependencies();

			for (auto const& dep : missingDeps)
			{
				ImGui::BulletText("%s", dep.name.c_str());
			}

			ImGui::Separator();
			ImGui::Text("Install command:");
			ImGui::TextWrapped("%s", m_checker.GetInstallCommand().c_str());
			ImGui::Separator();

			if (ImGui::Button("Install Dependencies", ImVec2(150, 0)))
			{
				m_isInstalling = true;
				bool success{ m_checker.InstallDependencies() };

				if (success)
				{
					m_installSuccess = true;
					m_showDialog = false;
				}
				else
				{
					m_installFailed = true;
				}

				m_isInstalling = false;
			}

			ImGui::SameLine();

			if (ImGui::Button("Exit", ImVec2(80, 0)))
			{
				m_shouldExit = true;
				m_showDialog = false;
			}

			if (m_isInstalling)
			{
				ImGui::Text("Installing...");
			}

			if (m_installFailed)
			{
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Installation failed. Please install manually.");
			}

			if (m_installSuccess)
			{
				ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Installation successful! Please restart the application.");
			}

			ImGui::EndPopup();
		}
	}

	return m_showDialog;
}
} // namespace Ctrn
