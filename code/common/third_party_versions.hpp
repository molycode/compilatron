#pragma once

#include <string_view>

namespace Ctrn
{
// Third-party library versions, read from each library's own headers at build time so they
// track the pinned submodules automatically — no manual edits when a submodule is bumped.
// The views reference static string literals, so they outlive any caller. The heavy library
// headers stay confined to the .cpp so callers (e.g. the About dialog) don't pull them in.
[[nodiscard]] std::string_view GetImGuiVersion();
[[nodiscard]] std::string_view GetGlfwVersion();
[[nodiscard]] std::string_view GetJsonVersion();
} // namespace Ctrn
