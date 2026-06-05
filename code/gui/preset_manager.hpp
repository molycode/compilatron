#pragma once

#include "build/build_settings.hpp"
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Ctrn
{
class CPresetManager final : private Tge::SNoCopyNoMove
{
public:

	CPresetManager() = default;

	[[nodiscard]] bool SavePreset(std::string_view name, std::string_view description, SBuildSettings& settings);

	// The single autosave path: serializes g_buildSettings to the currently active preset.
	// g_buildSettings is the authoritative state — callers keep it current before invoking this.
	[[nodiscard]] bool SaveActivePreset();

	[[nodiscard]] bool LoadPreset(std::string_view name, SBuildSettings& settings);
	[[nodiscard]] bool DeletePreset(std::string_view name);
	[[nodiscard]] bool RenamePreset(std::string_view oldName, std::string_view newName);
	std::vector<std::string> GetPresetNames() const;
	std::string GetDescription(std::string_view name) const;

private:

	std::string GetPresetsDirectory() const;
	std::string GetPresetFilePath(std::string_view name) const;
	std::string GetCurrentDateString() const;
};

} // namespace Ctrn
