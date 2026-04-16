#include "gui/preset_diff.hpp"
#include "build/build_settings.hpp"
#include "build/property_io.hpp"
#include <format>

namespace Ctrn
{
std::vector<std::string> GeneratePresetDiff(SBuildSettings const& current, SBuildSettings const& saved)
{
	std::vector<std::string> lines;

	// Top-level fields
	if (saved.installDirectory != current.installDirectory)
	{
		lines.emplace_back(std::format("Install directory: {} \u2192 {}",
		    saved.installDirectory, current.installDirectory));
	}

	if (saved.globalHostCompiler != current.globalHostCompiler)
	{
		lines.emplace_back(std::format("Global host compiler: {} \u2192 {}",
		    saved.globalHostCompiler.empty() ? std::string{ "(none)" } : saved.globalHostCompiler,
		    current.globalHostCompiler.empty() ? std::string{ "(none)" } : current.globalHostCompiler));
	}

	// Dependency tool paths — one line per changed entry
	for (auto const& [key, newPath] : current.dependencyLocationSelections)
	{
		auto const it{ saved.dependencyLocationSelections.find(key) };

		if (it == saved.dependencyLocationSelections.end())
		{
			lines.emplace_back(std::format("Dep {}: (none) \u2192 {}", key, newPath));
		}
		else if (it->second != newPath)
		{
			lines.emplace_back(std::format("Dep {}: {} \u2192 {}", key, it->second, newPath));
		}
	}

	for (auto const& [key, oldPath] : saved.dependencyLocationSelections)
	{
		if (!current.dependencyLocationSelections.count(key))
		{
			lines.emplace_back(std::format("Dep {}: {} \u2192 (none)", key, oldPath));
		}
	}

	// Removed compiler entries (in saved but not in current)
	for (auto const& savedEntry : saved.compilerEntries)
	{
		bool found{ false };

		for (auto const& curEntry : current.compilerEntries)
		{
			if (!found && curEntry.compilerType == savedEntry.compilerType && curEntry.name == savedEntry.name)
			{
				found = true;
			}
		}

		if (!found)
		{
			lines.emplace_back(std::format("[Removed] {} {}", savedEntry.compilerType.value, savedEntry.name.value));
		}
	}

	// New compiler entries (in current but not in saved)
	for (auto const& curEntry : current.compilerEntries)
	{
		bool found{ false };

		for (auto const& savedEntry : saved.compilerEntries)
		{
			if (!found && curEntry.compilerType == savedEntry.compilerType && curEntry.name == savedEntry.name)
			{
				found = true;
			}
		}

		if (!found)
		{
			lines.emplace_back(std::format("[New] {} {}", curEntry.compilerType.value, curEntry.name.value));
		}
	}

	// Diff matching entries
	for (auto const& curEntry : current.compilerEntries)
	{
		for (auto const& savedEntry : saved.compilerEntries)
		{
			if (curEntry.compilerType == savedEntry.compilerType && curEntry.name == savedEntry.name)
			{
				std::string const prefix{ std::format("{} {}: ", curEntry.compilerType.value, curEntry.name.value) };
				DiffSettings(lines, savedEntry, curEntry, prefix);

				if (curEntry.compilerType.value == "gcc")
				{
					DiffSettings(lines, savedEntry.gccSettings, curEntry.gccSettings, prefix);
				}
				else
				{
					DiffSettings(lines, savedEntry.clangSettings, curEntry.clangSettings, prefix);
				}
			}
		}
	}

	return lines;
}
} // namespace Ctrn
