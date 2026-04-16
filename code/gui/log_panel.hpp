#pragma once

#include "common/log_entry.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace Ctrn
{

// Renders colored log entries inline (Warning=yellow, Error=red, Info=default text color).
// Must be called inside an active ImGui window or child.
void RenderLogEntries(std::vector<SLogEntry> const& entries);

// Joins all entry.text fields with '\n' separators into a single string.
std::string BuildLogText(std::vector<SLogEntry> const& entries);

// Copies log to clipboard. Prepends header; truncates content at 50 MiB.
void CopyLogToClipboard(std::string_view header, std::vector<SLogEntry> const& entries);

// Renders a bordered, horizontally-scrollable child panel with auto-scroll.
// height=0 fills all available vertical space (ImVec2(0,0) semantics).
// Shows emptyMessage in grey when entries is empty.
void RenderLogPanel(std::string_view childId,
                    std::vector<SLogEntry> const& entries,
                    std::string_view emptyMessage,
                    float height = 0.0f);

} // namespace Ctrn
