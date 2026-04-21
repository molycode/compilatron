#include "dependency/dependency_window.hpp"
#include "build/build_settings.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"

#include <nlohmann/json.hpp>

#include <format>
#include <filesystem>
#include <fstream>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::LoadLocationSelectionsFromPresets()
{
	for (auto const& [identifier, savedPath] : g_buildSettings.dependencyLocationSelections)
	{
		auto* dep = g_dependencyManager.GetDependency(identifier);

		if (dep != nullptr)
		{
			bool found{ false };

			for (auto& location : dep->foundLocations)
			{
				if (!found && location.path == savedPath)
				{
					dep->selectedLocation = &location;
					found = true;
				}
			}

			// Saved path wasn't found by the scanner — re-register it as a custom path
			if (!found && std::filesystem::exists(savedPath))
			{
				SExecutableInfo const execInfo{ CreateExecutableInfo(savedPath, identifier) };

				if (execInfo.isWorking)
				{
					bool const success{ g_dependencyManager.RegisterAdditionalVersion(identifier, savedPath, execInfo.version) };

					if (success)
					{
						gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Restored custom {} path: {}", identifier, savedPath);
					}
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
	std::string const presetFile{ std::format("{}/config/presets/{}.json", g_dataDir, presetName) };
	std::ifstream file(presetFile);
	std::string result;

	if (file.is_open())
	{
		auto parsed = nlohmann::json::parse(file, nullptr, false);

		if (!parsed.is_discarded() && parsed.contains("description") && parsed["description"].is_string())
		{
			result = parsed["description"].get<std::string>();
		}
	}

	return result;
}
} // namespace Ctrn
