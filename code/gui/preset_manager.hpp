#pragma once

#include "build/build_settings.hpp"
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Ctrn
{
class CPresetManager final : private Tge::SNoCopyNoMove
{
public:

	CPresetManager() = default;

	[[nodiscard]] bool SavePreset(std::string_view name, std::string_view description, SBuildSettings& settings);
	[[nodiscard]] bool LoadPreset(std::string_view name, SBuildSettings& settings);
	[[nodiscard]] bool ParsePreset(std::string_view name, SBuildSettings& settings) const;
	[[nodiscard]] bool DeletePreset(std::string_view name);
	std::vector<std::string> GetPresetNames() const;

	void SetLastUsedPreset(std::string_view name);
	std::string GetLastUsedPreset() const;

	void SaveWindowState(int x, int y, int width, int height);
	[[nodiscard]] bool LoadWindowState() const;  // Loads directly to g_mainWindow* globals

	void SaveLastBrowseLocation(std::string_view path);
	std::string GetLastBrowseLocation() const;

private:

	void PopulateSettingsFromValues(std::unordered_map<std::string, std::string> const& values,
		SBuildSettings& settings) const;

	std::string GetPresetsDirectory() const;
	std::string GetPresetFilePath(std::string_view name) const;
	std::string GetCurrentDateString() const;
};

} // namespace Ctrn
