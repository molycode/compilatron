#include "build/compiler_registry.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "common/process_executor.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <unistd.h>

namespace Ctrn
{
namespace
{
namespace fs = std::filesystem;

// Extracts the first X.Y.Z version number found in a raw tool --version string.
// e.g. "Ubuntu clang version 18.1.3 (1ubuntu1)" → "18.1.3"
// e.g. "13.3.0" → "13.3.0"
std::string ExtractVersionNumbers(std::string_view versionString)
{
	size_t const start{ versionString.find_first_of("0123456789") };
	std::string result;

	if (start != std::string_view::npos)
	{
		size_t i{ start };
		bool inVersion{ true };

		while (i < versionString.size() && inVersion)
		{
			char const c{ versionString[i] };
			bool const isDigit{ std::isdigit(static_cast<unsigned char>(c)) != 0 };
			bool const nextIsDigit{ c == '.' && (i + 1 < versionString.size())
				&& (std::isdigit(static_cast<unsigned char>(versionString[i + 1])) != 0) };

			if (isDigit || nextIsDigit)
			{
				result += c;
				++i;
			}
			else
			{
				inVersion = false;
			}
		}
	}

	return result;
}

std::vector<std::string> GetPathDirs()
{
	std::vector<std::string> dirs;
	char const* pathEnv{ getenv("PATH") };

	if (pathEnv != nullptr)
	{
		std::string const pathStr{ pathEnv };
		std::stringstream ss{ pathStr };
		std::string dir;

		while (std::getline(ss, dir, ':'))
		{
			if (!dir.empty())
			{
				dirs.push_back(dir);
			}
		}
	}

	return dirs;
}

bool IsCompilerExecutableName(std::string_view name)
{
	bool result{ name == "g++" || name == "clang++" };

	if (!result)
	{
		// Match versioned names: g++-N, clang++-N (numeric suffix only)
		auto matchVersioned = [&](std::string_view prefix) -> bool
		{
			bool matched{ false };

			if (name.starts_with(prefix))
			{
				std::string_view const suffix{ name.substr(prefix.size()) };
				matched = !suffix.empty() && std::all_of(suffix.begin(), suffix.end(), ::isdigit);
			}

			return matched;
		};

		result = matchVersioned("g++-") || matchVersioned("clang++-");
	}

	return result;
}

bool IsVersionedCompilerName(std::string_view name)
{
	return name != "g++" && name != "clang++";
}

ECompilerKind KindFromFilename(std::string_view filename)
{
	return filename.find("clang") != std::string_view::npos
		? ECompilerKind::Clang : ECompilerKind::Gcc;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
void CCompilerRegistry::Initialize()
{
	m_configFilePath = g_dataDir + "/config/config.json";
	LoadConfig();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerRegistry::Terminate()
{
	SaveConfig();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerRegistry::Scan()
{
	// Preserve current custom entries before clearing
	std::vector<SCompiler> removable;

	for (auto const& c : m_compilers)
	{
		if (c.isRemovable)
		{
			removable.push_back(c);
		}
	}

	m_compilers.clear();

	// Collect all candidates, keyed by canonical path.
	// Unversioned names (g++, clang++) win over versioned (g++-13, clang++-18).
	std::unordered_map<std::string, std::string> canonicalToBestPath;

	for (std::string const& dir : GetPathDirs())
	{
		std::error_code ec;

		if (fs::is_directory(dir, ec) && !ec)
		{
			std::error_code iterEc;

			for (auto const& entry : fs::directory_iterator(dir, iterEc))
			{
				std::string const filename{ entry.path().filename().string() };
				std::error_code fileEc;

				if (IsCompilerExecutableName(filename) && entry.is_regular_file(fileEc) && !fileEc)
				{
					std::string const fullPath{ entry.path().string() };
					std::error_code canonEc;
					fs::path const canonicalPath{ fs::canonical(entry.path(), canonEc) };
					std::string const canonicalStr{ canonEc ? fullPath : canonicalPath.string() };

					auto const it{ canonicalToBestPath.find(canonicalStr) };
					bool const isNew{ it == canonicalToBestPath.end() };
					bool const currentIsVersioned{ IsVersionedCompilerName(filename) };
					bool const storedIsVersioned{ !isNew
						&& IsVersionedCompilerName(fs::path(it->second).filename().string()) };

					if (isNew || (!currentIsVersioned && storedIsVersioned))
					{
						canonicalToBestPath[canonicalStr] = fullPath;
					}
				}
			}
		}
	}

	for (auto const& [canonical, path] : canonicalToBestPath)
	{
		m_compilers.push_back(BuildCompilerEntry(path, false));
	}

	// Re-add preserved custom entries (only if path still exists on disk)
	for (auto const& c : removable)
	{
		std::error_code ec;

		if (fs::exists(c.path, ec) && !ec && !IsAlreadyRegistered(c.path))
		{
			m_compilers.push_back(c);
		}
	}

	gLog.Info(Tge::Logging::ETarget::File,
		"CompilerRegistry: Scan complete — {} compiler(s) found", m_compilers.size());
}

//////////////////////////////////////////////////////////////////////////
std::vector<SCompiler> CCompilerRegistry::ScanDirectory(std::string_view dir)
{
	std::vector<SCompiler> newEntries;
	std::string const dirStr{ dir };

	std::vector<std::string> dirsToScan;
	dirsToScan.push_back(dirStr);

	std::string const binDir{ (fs::path(dirStr) / "bin").string() };
	std::error_code binEc;

	if (fs::is_directory(binDir, binEc) && !binEc)
	{
		dirsToScan.push_back(binDir);
	}

	std::unordered_map<std::string, std::string> canonicalToBestPath;

	for (std::string const& scanDir : dirsToScan)
	{
		std::error_code dirEc;

		if (fs::is_directory(scanDir, dirEc) && !dirEc)
		{
			std::error_code iterEc;

			for (auto const& entry : fs::directory_iterator(scanDir, iterEc))
			{
				std::string const filename{ entry.path().filename().string() };
				std::error_code fileEc;

				if (IsCompilerExecutableName(filename) && entry.is_regular_file(fileEc) && !fileEc)
				{
					std::string const fullPath{ entry.path().string() };
					std::error_code canonEc;
					fs::path const canonicalPath{ fs::canonical(entry.path(), canonEc) };
					std::string const canonicalStr{ canonEc ? fullPath : canonicalPath.string() };

					if (!IsAlreadyRegistered(fullPath))
					{
						auto const it{ canonicalToBestPath.find(canonicalStr) };
						bool const isNew{ it == canonicalToBestPath.end() };
						bool const currentIsVersioned{ IsVersionedCompilerName(filename) };
						bool const storedIsVersioned{ !isNew
							&& IsVersionedCompilerName(fs::path(it->second).filename().string()) };

						if (isNew || (!currentIsVersioned && storedIsVersioned))
						{
							canonicalToBestPath[canonicalStr] = fullPath;
						}
					}
				}
			}
		}
	}

	for (auto const& [canonical, path] : canonicalToBestPath)
	{
		if (!IsAlreadyRegistered(path))
		{
			SCompiler compiler{ BuildCompilerEntry(path, true) };
			newEntries.push_back(compiler);
			m_compilers.push_back(std::move(compiler));
		}
	}

	gLog.Info(Tge::Logging::ETarget::File,
		"CompilerRegistry: ScanDirectory({}) found {} new compiler(s)", dir, newEntries.size());

	return newEntries;
}

//////////////////////////////////////////////////////////////////////////
std::vector<SCompiler> const& CCompilerRegistry::GetCompilers() const
{
	return m_compilers;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerRegistry::Contains(std::string_view path) const
{
	return IsAlreadyRegistered(path);
}

//////////////////////////////////////////////////////////////////////////
void CCompilerRegistry::AddCompiler(std::string_view path)
{
	if (!path.empty() && !IsAlreadyRegistered(path))
	{
		std::error_code ec;

		if (fs::is_regular_file(path, ec) && !ec)
		{
			m_compilers.push_back(BuildCompilerEntry(path, true));
			gLog.Info(Tge::Logging::ETarget::File, "CompilerRegistry: Added compiler: {}", path);
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::File,
				"CompilerRegistry: AddCompiler — not a file: {}", path);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerRegistry::RemoveCompiler(std::string_view path)
{
	std::string const pathStr{ path };
	auto const it{ std::find_if(m_compilers.begin(), m_compilers.end(),
		[&](SCompiler const& c) { return c.path == pathStr; }) };

	if (it != m_compilers.end())
	{
		m_compilers.erase(it);
		gLog.Info(Tge::Logging::ETarget::File, "CompilerRegistry: Removed compiler: {}", path);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerRegistry::LoadConfig()
{
	bool success{ true };
	std::error_code existsEc;

	if (fs::exists(m_configFilePath, existsEc) && !existsEc)
	{
		std::ifstream file(m_configFilePath);

		if (file.is_open())
		{
			auto parsed = nlohmann::json::parse(file, nullptr, false);

			if (!parsed.is_discarded() && parsed.contains("customCompilers") && parsed["customCompilers"].is_array())
			{
				for (auto const& entry : parsed["customCompilers"])
				{
					if (entry.is_string())
					{
						AddCompiler(entry.get<std::string>());
					}
				}
			}

			gLog.Info(Tge::Logging::ETarget::File,
				"CompilerRegistry: Loaded config from {}", m_configFilePath);
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::File,
				"CompilerRegistry: Failed to open config: {}", m_configFilePath);
			success = false;
		}
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerRegistry::SaveConfig()
{
	std::error_code ec;
	fs::create_directories(fs::path(m_configFilePath).parent_path(), ec);

	std::ofstream file(m_configFilePath);
	bool success{ file.is_open() };

	if (success)
	{
		nlohmann::json config;
		config["customCompilers"] = nlohmann::json::array();

		for (auto const& c : m_compilers)
		{
			if (c.isRemovable)
			{
				config["customCompilers"].push_back(c.path);
			}
		}

		file << config.dump(2) << "\n";

		gLog.Info(Tge::Logging::ETarget::File,
			"CompilerRegistry: Saved config to {}", m_configFilePath);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File,
			"CompilerRegistry: Failed to write config: {}", m_configFilePath);
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerRegistry::DetectVersion(std::string_view path, ECompilerKind kind) const
{
	std::string const command{ kind == ECompilerKind::Gcc
		? std::format("\"{}\" -dumpfullversion 2>/dev/null", path)
		: std::format("\"{}\" --version 2>/dev/null", path) };

	auto const result{ CProcessExecutor::Execute(command) };
	std::string version{ "unknown" };

	if (result.success || !result.output.empty())
	{
		std::string raw{ result.output };
		size_t const newline{ raw.find('\n') };

		if (newline != std::string::npos)
		{
			raw = raw.substr(0, newline);
		}

		std::string const extracted{ ExtractVersionNumbers(raw) };

		if (!extracted.empty())
		{
			version = extracted;
		}
	}

	return version;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerRegistry::IsAlreadyRegistered(std::string_view path) const
{
	bool found{ false };

	for (auto const& c : m_compilers)
	{
		if (!found && c.path == path)
		{
			found = true;
		}
	}

	return found;
}

//////////////////////////////////////////////////////////////////////////
SCompiler CCompilerRegistry::BuildCompilerEntry(std::string_view path, bool isRemovable) const
{
	SCompiler compiler;
	compiler.path = std::string{ path };

	std::string const filename{ fs::path(path).filename().string() };
	compiler.kind = KindFromFilename(filename);

	std::string const typeLabel{ compiler.kind == ECompilerKind::Gcc ? "GCC" : "Clang" };
	compiler.version = DetectVersion(path, compiler.kind);
	compiler.displayName = typeLabel + " " + compiler.version;
	compiler.hasProblematicPath = HasProblematicPathCharacters(path);
	compiler.isRemovable = isRemovable;

	return compiler;
}

//////////////////////////////////////////////////////////////////////////
std::string GetHostCompilerCPath(std::string_view cppCompilerPath)
{
	fs::path const p{ cppCompilerPath };
	std::string filename{ p.filename().string() };
	std::string result;

	if (filename.starts_with("clang++"))
	{
		filename.replace(0, 7, "clang");
	}
	else if (filename.starts_with("g++"))
	{
		filename.replace(0, 3, "gcc");
	}

	if (filename != p.filename().string())
	{
		result = (p.parent_path() / filename).string();
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File,
			"GetHostCompilerCPath: Could not derive C compiler from: {}", cppCompilerPath);
	}

	return result;
}

} // namespace Ctrn
