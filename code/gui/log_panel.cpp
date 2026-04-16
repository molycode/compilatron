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
#include <sstream>
#include <string>
#include <unordered_map>

namespace Ctrn
{

//////////////////////////////////////////////////////////////////////////
void RenderLogEntries(std::vector<SLogEntry> const& entries)
{
	constexpr ImVec4 WarningColor{ 1.0f, 0.8f, 0.0f, 1.0f };
	constexpr ImVec4 ErrorColor  { 1.0f, 0.35f, 0.35f, 1.0f };

	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(entries.size()));

	while (clipper.Step())
	{
		for (int i{ clipper.DisplayStart }; i < clipper.DisplayEnd; ++i)
		{
			SLogEntry const& entry{ entries[static_cast<size_t>(i)] };

			if (entry.level == Tge::Logging::ELogLevel::Warning)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, WarningColor);
				ImGui::TextUnformatted(entry.text.c_str());
				ImGui::PopStyleColor();
			}
			else if (entry.level == Tge::Logging::ELogLevel::Error)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ErrorColor);
				ImGui::TextUnformatted(entry.text.c_str());
				ImGui::PopStyleColor();
			}
			else
			{
				ImGui::TextUnformatted(entry.text.c_str());
			}
		}
	}

	clipper.End();
}

//////////////////////////////////////////////////////////////////////////
std::string BuildLogText(std::vector<SLogEntry> const& entries)
{
	std::string result;

	for (SLogEntry const& entry : entries)
	{
		if (!result.empty())
		{
			result += '\n';
		}

		result += entry.text;
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CopyLogToClipboard(std::string_view header, std::vector<SLogEntry> const& entries)
{
	constexpr size_t MaxClipboardSize{ 50 * 1024 * 1024 };

	std::string logText{ BuildLogText(entries) };
	std::ostringstream oss;
	oss << header << '\n';

	if (logText.size() > MaxClipboardSize)
	{
		oss << logText.substr(0, MaxClipboardSize);
		oss << "\n[OUTPUT TRUNCATED - Too large for clipboard]\n";
	}
	else
	{
		oss << logText;
	}

	std::string clipboardText{ oss.str() };

	if (!clipboardText.empty())
	{
		ImGui::SetClipboardText(clipboardText.c_str());
	}
}

//////////////////////////////////////////////////////////////////////////
void RenderLogPanel(std::string_view childId,
                    std::vector<SLogEntry> const& entries,
                    std::string_view emptyMessage,
                    float height)
{
	// Incrementally track the widest line across all entries.
	// ImGuiListClipper only renders visible rows, so off-screen lines never
	// advance the cursor — the horizontal scrollbar range would otherwise
	// shrink to whatever lines happen to be on screen. Dummy() forces the
	// full content width every frame without re-scanning unchanged lines.
	struct SWidthCache { size_t count{ 0 }; float width{ 0.0f }; };
	static std::unordered_map<std::string, SWidthCache> s_widthCache;

	SWidthCache& cache{ s_widthCache[std::string{ childId }] };

	if (cache.count > entries.size())
	{
		cache = {};
	}

	while (cache.count < entries.size())
	{
		float const lineWidth{ ImGui::CalcTextSize(entries[cache.count].text.c_str()).x };

		if (lineWidth > cache.width)
		{
			cache.width = lineWidth;
		}

		++cache.count;
	}

	ImGui::BeginChild(childId.data(), ImVec2(0, height), true, ImGuiWindowFlags_HorizontalScrollbar);

	// Auto-scroll: stay pinned to bottom unless the user has scrolled up.
	// Uses previous-frame scroll values (ImGui immediate-mode guarantee).
	float const prevScrollY   { ImGui::GetScrollY() };
	float const prevScrollMaxY{ ImGui::GetScrollMaxY() };
	bool const wasAtBottom{ prevScrollMaxY <= 0.0f || prevScrollY >= prevScrollMaxY - 1.0f };
	bool const scrolledUp{ ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel > 0.0f };

	if (entries.empty())
	{
		ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", emptyMessage.data());
	}
	else
	{
		ImGui::Dummy(ImVec2(cache.width, 0.0f));
		RenderLogEntries(entries);

		if (wasAtBottom && !scrolledUp)
		{
			ImGui::SetScrollHereY(1.0f);
		}
	}

	ImGui::EndChild();
}

} // namespace Ctrn
