#include "dependency/dependency_window.hpp"
#include "build/build_settings.hpp"
#include "common/common.hpp"
#include <format>
#include <fstream>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::LoadLocationSelectionsFromPresets()
{
	for (auto const& [identifier, selectedPath] : g_buildSettings.dependencyLocationSelections)
	{
		auto* dep = g_dependencyManager.GetDependency(identifier);

		if (dep != nullptr)
		{
			bool found{ false };

			for (auto& location : dep->foundLocations)
			{
				if (!found && location.path == selectedPath)
				{
					dep->selectedLocation = &location;
					found = true;
				}
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::SaveLocationSelectionsToPresets()
{
	g_buildSettings.dependencyLocationSelections.clear();

	auto allDeps = g_dependencyManager.GetAllDependencies();

	for (auto* dep : allDeps)
	{
		// Only save user-chosen locations (a non-null selectedLocation with valid locations present)
		if (dep->selectedLocation != nullptr && !dep->foundLocations.empty())
		{
			g_buildSettings.dependencyLocationSelections[dep->identifier] = dep->selectedLocation->path;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyWindow::GetPresetDescription(std::string_view presetName)
{
	// Get preset file path using the same logic as PresetManager
	std::string const presetFile = std::format("{}/config/{}.preset", g_dataDir, presetName);

	std::ifstream file(presetFile);
	std::string result;

	if (file.is_open())
	{
		std::string line;
		bool found{ false };

		while (std::getline(file, line) && !found && (line.empty() || line[0] != '['))
		{
			if (!line.empty() && line[0] != '#' && line.starts_with("description="))
			{
				result = line.substr(12); // Remove "description=" prefix
				found = true;
			}
		}
	}

	return result;
}
} // namespace Ctrn
