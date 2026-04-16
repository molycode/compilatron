#include "gui/compiler_gui.hpp"
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
void CCompilerGUI::RenderCompilerNotification(CCompilerUnit& unit, ImVec2 const& buttonPos)
{
	if (unit.HasNotification())
	{
		unit.UpdateNotificationTimer(ImGui::GetIO().DeltaTime);

		if (!unit.HasNotification())
		{
			return;
		}

		ImVec2 notificationPos = ImVec2(buttonPos.x + 60, buttonPos.y - 5);
		std::string windowName{ std::format("CompilerNotification##{}", unit.GetName()) };

		ImGui::SetNextWindowPos(notificationPos);
		ImGui::SetNextWindowBgAlpha(0.9f);

		ImGuiWindowFlags notificationFlags{ ImGuiWindowFlags_NoDecoration |
		                                     ImGuiWindowFlags_AlwaysAutoResize |
		                                     ImGuiWindowFlags_NoSavedSettings |
		                                     ImGuiWindowFlags_NoNav |
		                                     ImGuiWindowFlags_NoMove };

		if (ImGui::Begin(windowName.c_str(), nullptr, notificationFlags))
		{
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "%s", ICON_FA_TRIANGLE_EXCLAMATION);
			ImGui::SameLine();
			ImGui::Text("%s", std::string{ unit.GetNotificationMessage() }.c_str());

			float progress{ unit.GetNotificationTimer() / 5.0f };
			ImGui::ProgressBar(progress, ImVec2(200, 0), "");
		}

		ImGui::End();

		ImGui::SetWindowFocus(windowName.c_str());
	}
}
} // namespace Ctrn
