#include "gui/preset_manager.hpp"
#include "gui/compiler_gui.hpp"
#include "build/property_io.hpp"
#include "common/loggers.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace
{
template<typename T>
nlohmann::json PropToJson(T const& val)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		return val;
	}
	else if constexpr (std::is_same_v<T, int>)
	{
		return val;
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		return val;
	}
	else if constexpr (std::is_enum_v<T>)
	{
		return static_cast<int>(val);
	}
}

template<typename T>
void PropFromJson(nlohmann::json const& json, T& val)
{
	if constexpr (std::is_same_v<T, bool>)
	{
		if (json.is_boolean())
		{
			val = json.get<bool>();
		}
	}
	else if constexpr (std::is_same_v<T, int>)
	{
		if (json.is_number_integer())
		{
			val = json.get<int>();
		}
	}
	else if constexpr (std::is_same_v<T, std::string>)
	{
		if (json.is_string())
		{
			val = json.get<std::string>();
		}
	}
	else if constexpr (std::is_enum_v<T>)
	{
		if (json.is_number_integer())
		{
			val = static_cast<T>(json.get<int>());
		}
	}
}

template<typename TSettings>
nlohmann::json SettingsToJson(TSettings const& settings)
{
	auto json = nlohmann::json::object();
	std::apply([&](auto const&... props)
	{
		((json[std::string{ props.internalName }] = PropToJson(props.value)), ...);
	}, settings.Properties());
	return json;
}

template<typename TSettings>
void SettingsFromJson(nlohmann::json const& json, TSettings& settings)
{
	std::apply([&](auto&... props)
	{
		([&]()
		{
			std::string_view const key{ props.internalName };

			if (json.contains(key))
			{
				PropFromJson(json[key], props.value);
			}
		}(), ...);
	}, settings.Properties());
}
} // namespace

namespace Ctrn
{
namespace
{
void PopulateFromJson(nlohmann::json const& json, SBuildSettings& settings)
{
	settings.installDirectory = json.value("installDirectory", std::string{ "compilers" });
	settings.globalHostCompiler = json.value("globalCompiler", std::string{});

	settings.dependencyLocationSelections.clear();

	if (json.contains("dependencyLocations") && json["dependencyLocations"].is_object())
	{
		for (auto const& [key, locPath] : json["dependencyLocations"].items())
		{
			if (locPath.is_string())
			{
				settings.dependencyLocationSelections[key] = locPath.get<std::string>();
			}
		}
	}

	settings.compilerEntries.clear();

	if (json.contains("compilerEntries") && json["compilerEntries"].is_array())
	{
		size_t entryIdx{ 0 };

		for (auto const& entryJson : json["compilerEntries"])
		{
			if (entryJson.is_object())
			{
				SCompilerEntry entry;
				entry.id = static_cast<uint16_t>(entryIdx + 1);
				SettingsFromJson(entryJson, entry);

				if (entry.numJobs.value == 0)
				{
					entry.numJobs = g_cpuInfo.GetDefaultNumJobs();
				}

				if (entry.compilerType.value == "clang" && entryJson.contains("clangSettings"))
				{
					SettingsFromJson(entryJson["clangSettings"], entry.clangSettings);
				}

				if (entry.compilerType.value == "gcc" && entryJson.contains("gccSettings"))
				{
					SettingsFromJson(entryJson["gccSettings"], entry.gccSettings);
				}

				settings.compilerEntries.push_back(std::move(entry));
				++entryIdx;
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::File,
					"PresetManager: Skipping malformed compilerEntries[{}] — expected object, got {}",
					entryIdx, static_cast<int>(entryJson.type()));
			}
		}
	}
}
} // namespace

//////////////////////////////////////////////////////////////////////////
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
				nlohmann::json preset;
				preset["name"] = std::string{ name };
				preset["description"] = std::string{ description };
				preset["createdDate"] = GetCurrentDateString();
				preset["installDirectory"] = settings.installDirectory;
				preset["globalCompiler"] = settings.globalHostCompiler;

				auto depLocs = nlohmann::json::object();

				for (auto const& [id, path] : settings.dependencyLocationSelections)
				{
					depLocs[id] = path;
				}

				preset["dependencyLocations"] = std::move(depLocs);

				auto entries = nlohmann::json::array();

				for (auto const& entry : settings.compilerEntries)
				{
					auto entryJson = SettingsToJson(entry);
					entryJson["clangSettings"] = SettingsToJson(entry.clangSettings);
					entryJson["gccSettings"] = SettingsToJson(entry.gccSettings);
					entries.push_back(std::move(entryJson));
				}

				preset["compilerEntries"] = std::move(entries);
				file << preset.dump(2) << "\n";

				gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Successfully saved preset: {} with {} compiler entries",
					name, settings.compilerEntries.size());
				result = true;
			}
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool CPresetManager::LoadPreset(std::string_view name, SBuildSettings& settings)
{
	gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Loading preset: {}", name);

	bool result{ false };
	std::string const filePath{ GetPresetFilePath(name) };

	if (!std::filesystem::exists(filePath))
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Load failed: preset file not found");
	}
	else
	{
		std::ifstream file(filePath);

		if (!file.is_open())
		{
			gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Failed to open preset file: {}", filePath);
		}
		else
		{
			auto parsed = nlohmann::json::parse(file, nullptr, false);

			if (parsed.is_discarded())
			{
				gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Load failed: invalid JSON in '{}'", filePath);
			}
			else
			{
				PopulateFromJson(parsed, settings);

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
						// Compiler is outside PATH (e.g. a locally built compiler) — add it explicitly
						g_compilerRegistry.AddCompiler(settings.globalHostCompiler);
					}
				}

				gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Successfully loaded preset: {} with {} compiler entries",
					name, settings.compilerEntries.size());
				result = true;
			}
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
bool CPresetManager::RenamePreset(std::string_view oldName, std::string_view newName)
{
	bool result{ false };
	std::string const oldPath{ GetPresetFilePath(oldName) };
	std::string const newPath{ GetPresetFilePath(newName) };

	if (!std::filesystem::exists(oldPath))
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Rename failed: '{}' not found", oldName);
	}
	else if (std::filesystem::exists(newPath))
	{
		gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Rename failed: '{}' already exists", newName);
	}
	else
	{
		std::error_code ec;
		std::filesystem::rename(oldPath, newPath, ec);

		if (ec)
		{
			gLog.Warning(Tge::Logging::ETarget::File, "PresetManager: Rename failed: {}", ec.message());
		}
		else
		{
			gLog.Info(Tge::Logging::ETarget::File, "PresetManager: Renamed '{}' to '{}'", oldName, newName);
			result = true;
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CPresetManager::GetPresetNames() const
{
	std::vector<std::string> names;
	std::string const presetsDir{ GetPresetsDirectory() };

	if (std::filesystem::exists(presetsDir))
	{
		std::error_code ec;

		for (auto const& entry : std::filesystem::directory_iterator(presetsDir, ec))
		{
			if (!ec && entry.is_regular_file() && entry.path().extension() == ".json")
			{
				names.push_back(entry.path().stem().string());
			}
		}
	}

	return names;
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetDescription(std::string_view name) const
{
	std::string const filePath{ GetPresetFilePath(name) };
	std::ifstream file(filePath);
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

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetPresetsDirectory() const
{
	return g_dataDir + "/config/presets";
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetPresetFilePath(std::string_view name) const
{
	return std::format("{}/{}.json", GetPresetsDirectory(), name);
}

//////////////////////////////////////////////////////////////////////////
std::string CPresetManager::GetCurrentDateString() const
{
	auto now{ std::chrono::system_clock::now() };
	auto timeT{ std::chrono::system_clock::to_time_t(now) };

	std::ostringstream ss;
	ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
	return ss.str();
}

} // namespace Ctrn
