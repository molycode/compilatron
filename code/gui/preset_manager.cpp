#include "gui/preset_manager.hpp"
#include "gui/compiler_gui.hpp"
#include "build/property_io.hpp"
#include "common/loggers.hpp"
#include <charconv>
#include <format>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace
{
int ParseInt(std::string const& str, int defaultVal = 0)
{
	int value{ defaultVal };
	std::from_chars(str.data(), str.data() + str.size(), value);
	return value;
}
} // namespace

namespace Ctrn
{
bool CPresetManager::SavePreset(std::string_view name, std::string_view description, SBuildSettings& settings)
{
	gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Saving preset: {}", name);

	bool result{ false };

	if (name.empty())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Save failed: empty preset name");
	}
	else
	{
		// Don't clean up invalid tab overrides - preserve them so user can see what was configured

		std::error_code mkdirEc;
		std::filesystem::create_directories(GetPresetsDirectory(), mkdirEc);

		if (mkdirEc)
		{
			gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to create presets directory: {}", mkdirEc.message());
		}
		else
		{
			std::ofstream file(GetPresetFilePath(name));

			if (!file.is_open())
			{
				gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to open preset file for writing: {}", GetPresetFilePath(name));
			}
			else
			{
				file << "# Compilatron Preset File\n";
				file << "name=" << name << "\n";
				file << "description=" << description << "\n";
				file << "createdDate=" << GetCurrentDateString() << "\n";
				file << "\n";

				file << "[Settings]\n";
				file << "installDirectory=" << settings.installDirectory << "\n";
				file << "globalCompiler=" << settings.globalHostCompiler << "\n";

				file << "\n[DependencyLocationSelections]\n";
				for (auto const& [identifier, path] : settings.dependencyLocationSelections)
				{
					file << identifier << "=" << path << "\n";
				}

				file << "\n[CompilerEntries]\n";
				file << "count=" << settings.compilerEntries.size() << "\n";
				for (size_t i{ 0 }; i < settings.compilerEntries.size(); ++i)
				{
					auto const& entry{ settings.compilerEntries[i] };
					std::string const prefix{ std::format("entry{}_", i) };
					SerializeSettings(file, entry, prefix);

					if (entry.compilerType.value == "clang")
					{
						SerializeSettings(file, entry.clangSettings, prefix);
					}

					if (entry.compilerType.value == "gcc")
					{
						SerializeSettings(file, entry.gccSettings, prefix);
					}
				}


				gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Successfully saved preset: {} with {} compiler entries", name, settings.compilerEntries.size());
				result = true;
			}
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CPresetManager::PopulateSettingsFromValues(
	std::unordered_map<std::string, std::string> const& values,
	SBuildSettings& settings) const
{
	settings.installDirectory = values.count("Settings.installDirectory") ? values.at("Settings.installDirectory") : "compilers";

	if (values.count("Settings.globalCompiler"))
	{
		settings.globalHostCompiler = values.at("Settings.globalCompiler");
		gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Loaded global compiler from Settings: {}", settings.globalHostCompiler);
	}
	else if (values.count("CompilerSelection.globalCompiler"))
	{
		settings.globalHostCompiler = values.at("CompilerSelection.globalCompiler");
		gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Loaded global compiler from CompilerSelection (old format): {}", settings.globalHostCompiler);
	}

	settings.dependencyLocationSelections.clear();

	for (auto const& [key, value] : values)
	{
		if (key.starts_with("DependencyLocationSelections."))
		{
			std::string identifier{ key.substr(29) };
			settings.dependencyLocationSelections[identifier] = value;
		}
	}

	settings.compilerEntries.clear();

	if (values.count("CompilerEntries.count"))
	{
		size_t numEntries{ static_cast<size_t>(ParseInt(values.at("CompilerEntries.count"))) };

		for (size_t i{ 0 }; i < numEntries; ++i)
		{
			std::string const prefix{ std::format("CompilerEntries.entry{}_", i) };

			if (values.count(prefix + "name") && values.count(prefix + "folderName"))
			{
				SCompilerEntry entry;
				entry.id = static_cast<uint16_t>(i + 1);
				gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Generated entry ID: {}", entry.id);

				DeserializeSettings(values, entry, prefix);

				if (entry.numJobs.value == 0)
				{
					entry.numJobs = g_cpuInfo.GetDefaultNumJobs();
				}

				if (entry.compilerType.value == "clang")
				{
					DeserializeSettings(values, entry.clangSettings, prefix);
				}

				if (entry.compilerType.value == "gcc")
				{
					DeserializeSettings(values, entry.gccSettings, prefix);
				}

				settings.compilerEntries.push_back(entry);
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
bool CPresetManager::ParsePreset(std::string_view name, SBuildSettings& settings) const
{
	bool result{ false };

	if (!std::filesystem::exists(GetPresetFilePath(name)))
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: ParsePreset failed: preset '{}' not found", name);
	}
	else
	{
		std::ifstream file(GetPresetFilePath(name));

		if (!file.is_open())
		{
			gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: ParsePreset: failed to open '{}'", GetPresetFilePath(name));
		}
		else
		{
			std::unordered_map<std::string, std::string> values;
			std::string line;
			std::string currentSection;

			while (std::getline(file, line))
			{
				if (!line.empty() && line[0] != '#')
				{
					if (line[0] == '[' && line.back() == ']')
					{
						currentSection = line.substr(1, line.length() - 2);
					}
					else
					{
						auto const pos{ line.find('=') };

						if (pos != std::string::npos)
						{
							std::string key{ line.substr(0, pos) };
							std::string value{ line.substr(pos + 1) };

							if (!currentSection.empty())
							{
								key = currentSection + "." + key;
							}

							values[key] = value;
						}
					}
				}
			}

			PopulateSettingsFromValues(values, settings);
			result = true;
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool CPresetManager::LoadPreset(std::string_view name, SBuildSettings& settings)
{
	gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Loading preset: {}", name);

	bool result{ false };

	if (!std::filesystem::exists(GetPresetFilePath(name)))
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Load failed: preset file not found");
	}
	else
	{
		std::ifstream file(GetPresetFilePath(name));

		if (!file.is_open())
		{
			gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to open preset file: {}", GetPresetFilePath(name));
		}
		else
		{
			std::unordered_map<std::string, std::string> values;
			std::string line;
			std::string currentSection;

			while (std::getline(file, line))
			{
				if (!line.empty() && line[0] != '#')
				{
					if (line[0] == '[' && line.back() == ']')
					{
						currentSection = line.substr(1, line.length() - 2);
					}
					else
					{
						auto const pos{ line.find('=') };

						if (pos != std::string::npos)
						{
							std::string key{ line.substr(0, pos) };
							std::string value{ line.substr(pos + 1) };

							if (!currentSection.empty())
							{
								key = currentSection + "." + key;
							}

							values[key] = value;
						}
					}
				}
			}

			PopulateSettingsFromValues(values, settings);

			size_t numCustom{ 0 };

			for (auto const& c : g_compilerRegistry.GetCompilers())
			{
				if (c.isRemovable)
				{
					++numCustom;
				}
			}

			gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Restored {} custom compiler(s) from preset", numCustom);

			g_compilerRegistry.Scan();
			g_dependencyManager.ScanAllDependencies();

			// Verify global compiler is available in registry
			if (!settings.globalHostCompiler.empty())
			{
				bool found{ false };

				for (auto const& compiler : g_compilerRegistry.GetCompilers())
				{
					if (!found && compiler.path == settings.globalHostCompiler)
					{
						found = true;
					}
				}

				if (found)
				{
					gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Global compiler found in registry: {}",
						settings.globalHostCompiler);
				}
				else
				{
					gLog.Warning(Tge::Logging::ETarget::File,
						"PresetManager: Global compiler not found after scan: {} (will use auto-detect)",
						settings.globalHostCompiler);
				}
			}

			gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Successfully loaded preset: {} with {} compiler entries",
				name, settings.compilerEntries.size());
			result = true;
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool CPresetManager::DeletePreset(std::string_view name)
{
	bool result{ false };
	std::string const path{ GetPresetFilePath(name) };

	if (!std::filesystem::exists(path))
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Delete failed: preset '{}' not found", name);
	}
	else
	{
		std::error_code ec;
		std::filesystem::remove(path, ec);

		if (ec)
		{
			gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to delete preset '{}': {}", name, ec.message());
		}
		else
		{
			gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Deleted preset: {}", name);
			result = true;
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CPresetManager::GetPresetNames() const
{
	std::vector<std::string> names;
	std::string presetsDir{ GetPresetsDirectory() };

	if (std::filesystem::exists(presetsDir))
	{
		std::error_code ec;
		for (auto const& entry : std::filesystem::directory_iterator(presetsDir, ec))
		{
			if (!ec && entry.is_regular_file() && entry.path().extension() == ".preset")
			{
				std::string filename{ entry.path().stem().string() };
				names.push_back(filename);
			}
		}
	}

	return names;
}

//////////////////////////////////////////////////////////////////////////
void CPresetManager::SetLastUsedPreset(std::string_view name)
{
	std::string lastUsedFile{ GetPresetsDirectory() + "/.lastused" };
	std::ofstream file(lastUsedFile);

	if (file.is_open())
	{
		file << name;
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetLastUsedPreset() const
{
	std::string result;
	std::string lastUsedFile{ GetPresetsDirectory() + "/.lastused" };

	if (std::filesystem::exists(lastUsedFile))
	{
		std::ifstream file(lastUsedFile);

		if (file.is_open())
		{
			std::string presetName;
			std::getline(file, presetName);

			if (!presetName.empty() && std::filesystem::exists(GetPresetFilePath(presetName)))
			{
				result = presetName;
			}
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CPresetManager::SaveWindowState(int x, int y, int width, int height)
{
	std::error_code mkdirEc;
	std::filesystem::create_directories(GetPresetsDirectory(), mkdirEc);

	std::string windowStateFile{ GetPresetsDirectory() + "/.windowstate" };
	std::ofstream file(windowStateFile);

	if (file.is_open())
	{
		file << x << "\n" << y << "\n" << width << "\n" << height;
		gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Window state saved to: {}", windowStateFile);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to open window state file: {}", windowStateFile);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CPresetManager::LoadWindowState() const
{
	auto ParseInt = [](std::string const& str, int defaultVal = 0) -> int
	{
		int value{ defaultVal };
		std::from_chars(str.data(), str.data() + str.size(), value);
		return value;
	};

	bool result{ false };
	std::string windowStateFile{ GetPresetsDirectory() + "/.windowstate" };

	if (std::filesystem::exists(windowStateFile))
	{
		std::ifstream file(windowStateFile);

		if (file.is_open())
		{
			std::string line;
			if (std::getline(file, line))
			{
				g_mainWindowPosX = ParseInt(line);
			}
			if (std::getline(file, line))
			{
				g_mainWindowPosY = ParseInt(line);
			}
			if (std::getline(file, line))
			{
				g_mainWindowWidth = ParseInt(line);
			}
			if (std::getline(file, line))
			{
				g_mainWindowHeight = ParseInt(line);
			}

			// Basic validation - ensure reasonable window size
			int x{ g_mainWindowPosX.load() };
			int y{ g_mainWindowPosY.load() };
			int width{ g_mainWindowWidth.load() };
			int height{ g_mainWindowHeight.load() };

			if (width >= 400 && height >= 300 && width <= 3840 && height <= 2160)
			{
				gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Window state loaded: pos({},{}) size({}x{})",
					x, y, width, height);
				result = true;
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Window state validation failed: size({}x{}) out of bounds", width, height);
			}
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to open window state file: {}", windowStateFile);
		}
	}
	else
	{
		gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Window state file not found: {}", windowStateFile);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CPresetManager::SaveLastBrowseLocation(std::string_view path)
{
	std::error_code mkdirEc;
	std::filesystem::create_directories(GetPresetsDirectory(), mkdirEc);

	std::string browseLocationFile{ GetPresetsDirectory() + "/.lastbrowse" };
	std::ofstream file(browseLocationFile);

	if (file.is_open())
	{
		// Store the directory path (not the file itself)
		std::filesystem::path pathObj(path);
		std::string directory{ pathObj.parent_path().string() };
		file << directory;
		gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Last browse location saved: {}", directory);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to save last browse location");
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetLastBrowseLocation() const
{
	std::string result{ "." }; // Current directory as final fallback
	bool found{ false };
	std::string browseLocationFile{ GetPresetsDirectory() + "/.lastbrowse" };

	if (std::filesystem::exists(browseLocationFile))
	{
		std::ifstream file(browseLocationFile);

		if (file.is_open())
		{
			std::string location;
			std::getline(file, location);

			if (!location.empty() && std::filesystem::exists(location) && std::filesystem::is_directory(location))
			{
				gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Last browse location loaded: {}", location);
				result = location;
				found = true;
			}
			else
			{
				gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Last browse location invalid or missing: {}", location);
			}
		}
	}

	if (!found)
	{
		// Return data directory as default browse location
		if (!g_dataDir.empty() && std::filesystem::exists(g_dataDir) && std::filesystem::is_directory(g_dataDir))
		{
			gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Using data directory as default browse location: {}", g_dataDir);
			result = g_dataDir;
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetPresetsDirectory() const
{
	return g_dataDir + "/config";
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetPresetFilePath(std::string_view name) const
{
	return std::format("{}/{}.preset", GetPresetsDirectory(), name);
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetCurrentDateString() const
{
	auto now = std::chrono::system_clock::now();
	auto time_t = std::chrono::system_clock::to_time_t(now);

	std::ostringstream ss;
	ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
	return ss.str();
}

} // namespace Ctrn
