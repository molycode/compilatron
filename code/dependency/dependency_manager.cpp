#include "dependency_manager.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "common/process_executor.hpp"
#include <tge/init/assert.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include <regex>
#include <set>
#include <thread>
#include <mutex>
#include <functional>
#include <chrono>
#include <format>
#include <charconv>
#include <sstream>

namespace Ctrn
{
namespace
{
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
} // namespace

namespace fs = std::filesystem;

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::InitializeAllDependencies()
{
	m_dependencies.clear();

	InitializeBuildTools();
	InitializeLibraries();
}

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::ScanAllDependencies()
{
	gDepLog.Info(Tge::Logging::ETarget::File, "Scanning all dependencies for available installations...");

	{
		std::lock_guard<std::mutex> lock(m_dependenciesMutex);
		for (auto& dep : m_dependencies)
		{
			ScanDependency(*dep);
		}
	}

	UpdateEnvironmentPaths();

	size_t available;
	{
		std::lock_guard<std::mutex> lock(m_dependenciesMutex);
		available = std::count_if(m_dependencies.begin(), m_dependencies.end(),
			[](auto const& dep) { return dep->status == EDependencyStatus::Available; });
	}

	gDepLog.Info(Tge::Logging::ETarget::File, "Dependency scan complete: {}/{} available", available, m_dependencies.size());
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::AreAllRequiredDependenciesAvailable() const
{
	std::lock_guard<std::mutex> lock(m_dependenciesMutex);

	bool allAvailable{ true };

	for (auto const& dep : m_dependencies)
	{
		if (dep->IsRequired() && dep->status != EDependencyStatus::Available)
		{
			allAvailable = false;
		}
	}

	return allAvailable;
}

//////////////////////////////////////////////////////////////////////////
std::vector<SAdvancedDependencyInfo*> CDependencyManager::GetMissingRequiredDependencies() const
{
	std::lock_guard<std::mutex> lock(m_dependenciesMutex);
	std::vector<SAdvancedDependencyInfo*> missing;

	for (auto const& dep : m_dependencies)
	{
		if (dep->IsRequired() && dep->status != EDependencyStatus::Available)
		{
			missing.push_back(dep.get());
		}
	}

	return missing;
}

//////////////////////////////////////////////////////////////////////////
std::vector<SAdvancedDependencyInfo*> CDependencyManager::GetAllDependencies() const
{
	std::lock_guard<std::mutex> lock(m_dependenciesMutex);
	std::vector<SAdvancedDependencyInfo*> all;

	for (auto const& dep : m_dependencies)
	{
		all.push_back(dep.get());
	}

	return all;
}

//////////////////////////////////////////////////////////////////////////
SAdvancedDependencyInfo* CDependencyManager::GetDependency(std::string_view identifier) const
{
	auto it = std::find_if(m_dependencies.begin(), m_dependencies.end(),
		[identifier](auto const& dep) { return dep->identifier == identifier; });

	return (it != m_dependencies.end()) ? it->get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
SAdvancedDependencyInfo* CDependencyManager::GetDependencyByName(std::string_view name) const
{
	auto it = std::find_if(m_dependencies.begin(), m_dependencies.end(),
		[name](auto const& dep) { return dep->name == name; });

	return (it != m_dependencies.end()) ? it->get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::ScanDependency(SAdvancedDependencyInfo& dep)
{
	dep.foundLocations.clear();
	dep.selectedLocation = nullptr;
	dep.status = EDependencyStatus::Missing;

	if (dep.category == EDependencyCategory::Library)
	{
		if (dep.identifier == "binutils-include")
		{
			// Enumerate every local install (any depsDir subdir containing plugin-api.h)
			std::string const depsDir{ g_dataDir + "/dependencies" };
			std::error_code scanEc;

			if (fs::exists(depsDir))
			{
				for (auto const& entry : fs::directory_iterator(depsDir, scanEc))
				{
					if (entry.is_directory() && fs::exists(entry.path() / "plugin-api.h"))
					{
						std::string const dirName{ entry.path().filename().string() };
						std::string const version{ ExtractVersionNumbers(dirName) };

						SDependencyLocation loc;
						loc.type = EInstallLocation::LocalApp;
						loc.path = entry.path().string();
						loc.isWorking = true;
						loc.priority = 1;
						loc.version = version.empty() ? "unknown" : version;
						dep.foundLocations.push_back(loc);
					}
				}
			}

			if (fs::exists("/usr/include/plugin-api.h"))
			{
				SDependencyLocation loc;
				loc.type = EInstallLocation::System;
				loc.path = "/usr/include";
				loc.isWorking = true;
				loc.priority = 2;
				loc.version = DetectVersion("", "ld --version 2>/dev/null | head -1");
				dep.foundLocations.push_back(loc);
			}

			if (!dep.foundLocations.empty())
			{
				dep.selectedLocation = &dep.foundLocations.front();
				dep.status = EDependencyStatus::Available;
			}

			return;
		}

		if (!dep.checkCommand.empty())
		{
			auto const result = CProcessExecutor::Execute(dep.checkCommand);

			if (result.success)
			{
				std::string path{ result.output };
				size_t const end{ path.find_last_not_of(" \t\n\r") };
				path = (end != std::string::npos) ? path.substr(0, end + 1) : "";

				SDependencyLocation location;
				location.type = EInstallLocation::System;
				location.path = path.empty() ? "(system)" : path;
				location.isWorking = true;
				location.priority = 2;
				location.version = DetectVersion("", dep.versionCommand);
				dep.foundLocations.push_back(location);
				dep.selectedLocation = &dep.foundLocations.front();
				dep.status = EDependencyStatus::Available;
			}
		}

		return;
	}

	dep.foundLocations = FindAllLocations(dep);

	RemoveDuplicateLocations(dep.foundLocations);

	if (!dep.foundLocations.empty())
	{
		std::sort(dep.foundLocations.begin(), dep.foundLocations.end(),
			[](SDependencyLocation const& a, SDependencyLocation const& b) {
				return a.priority < b.priority;
			});

		bool found{ false };

		for (auto& location : dep.foundLocations)
		{
			if (!found && location.isWorking)
			{
				dep.selectedLocation = &location;
				dep.status = EDependencyStatus::Available;
				found = true;
			}
		}

		if (!dep.selectedLocation)
		{
			dep.status = (dep.foundLocations.size() > 1) ?
				EDependencyStatus::MultipleFound : EDependencyStatus::Broken;
		}
	}
}

//////////////////////////////////////////////////////////////////////////
std::vector<SDependencyLocation> CDependencyManager::FindAllLocations(SAdvancedDependencyInfo const& dep)
{
	std::vector<SDependencyLocation> locations;

	// Scan system paths
	for (auto const& path : GetSystemPaths())
	{
		auto location = ScanLocation(EInstallLocation::System, path, dep);
		if (!location.path.empty())
		{
			locations.push_back(location);
		}
	}

	// Scan user paths
	for (auto const& path : GetUserPaths())
	{
		auto location = ScanLocation(EInstallLocation::UserWide, path, dep);
		if (!location.path.empty())
		{
			locations.push_back(location);
		}
	}

	// Scan local app paths
	for (auto const& path : GetLocalPaths())
	{
		auto location = ScanLocation(EInstallLocation::LocalApp, path, dep);
		if (!location.path.empty())
		{
			locations.push_back(location);
		}
	}

	return locations;
}

//////////////////////////////////////////////////////////////////////////
SDependencyLocation CDependencyManager::ScanLocation(EInstallLocation locationType,
	std::string_view basePath, SAdvancedDependencyInfo const& dep)
{
	SDependencyLocation location;
	location.type = locationType;
	location.priority = 999; // Default low priority

	// Try primary name first, then alternatives
	std::vector<std::string> namesToTry{ dep.identifier };
	namesToTry.insert(namesToTry.end(), dep.alternativeNames.begin(), dep.alternativeNames.end());

	bool found{ false };

	for (auto const& name : namesToTry)
	{
		std::string fullPath{ (fs::path(basePath) / name).string() };

		if (!found && fs::exists(fullPath) && fs::is_regular_file(fullPath))
		{
			location.path = fullPath;
			location.version = DetectVersion(fullPath, dep.versionCommand);
			location.isWorking = TestFunctionality(fullPath, dep.checkCommand);

			// Set default priorities based on location type (lower number = higher priority)
			// More local installations have higher priority
			switch (locationType)
			{
				case EInstallLocation::LocalApp:   location.priority = 0; break; // Highest priority - most local
				case EInstallLocation::UserWide:   location.priority = 1; break; // Medium priority
				case EInstallLocation::System:     location.priority = 2; break; // Lowest priority - system-wide
			}

			found = true;
		}
	}

	return location;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyManager::DetectVersion(std::string_view path, std::string_view versionCommand)
{
	std::string version{ "unknown" };

	if (!versionCommand.empty())
	{
		std::string command{versionCommand};
		size_t const pos = command.find("{path}");

		if (pos != std::string::npos)
		{
			command.replace(pos, 6, path);
		}
		else if (!versionCommand.contains("pkg-config") && !path.empty())
		{
			command = std::format("{} {}", path, versionCommand);
		}

		gDepLog.Info(Tge::Logging::ETarget::File, "DetectVersion executing: {}", command);

		auto const procResult = CProcessExecutor::Execute(command);

		if (procResult.success || !procResult.output.empty())
		{
			version = procResult.output;

			if (!version.empty())
			{
				size_t const newline = version.find('\n');

				if (newline != std::string::npos)
				{
					version = version.substr(0, newline);
				}

				std::string const extracted{ ExtractVersionNumbers(version) };

				if (!extracted.empty())
				{
					version = extracted;
				}
			}

			if (version.empty())
			{
				version = "unknown";
			}
		}
		else
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "DetectVersion: Command failed: {}", command);
		}
	}

	return version;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::TestFunctionality(std::string_view path, std::string_view checkCommand)
{
	bool result{ fs::exists(path) };

	if (!checkCommand.empty())
	{
		std::string command{checkCommand};
		size_t const pos = command.find("{path}");

		if (pos != std::string::npos)
		{
			command.replace(pos, 6, path);
		}
		else if (!checkCommand.contains("pkg-config"))
		{
			command = std::format("{} {}", path, checkCommand);
		}

		result = CProcessExecutor::Execute(command).success;
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CDependencyManager::GetSystemPaths() const
{
	return {"/usr/bin", "/usr/local/bin", "/bin", "/sbin", "/usr/sbin"};
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CDependencyManager::GetUserPaths() const
{
	std::vector<std::string> paths;

	char const* home = getenv("HOME");

	if (home != nullptr)
	{
		paths.push_back(std::string(home) + "/.local/bin");
		paths.push_back(std::string(home) + "/bin");
	}

	return paths;
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CDependencyManager::GetLocalPaths() const
{
	std::vector<std::string> paths;

	std::string depsDir{ g_dataDir + "/dependencies" };

	if (fs::exists(depsDir) && fs::is_directory(depsDir))
	{
		std::error_code ec;
		for (auto const& entry : fs::directory_iterator(depsDir, ec))
		{
			if (!ec && entry.is_directory())
			{
				std::string toolBinDir{ entry.path() / "bin" };
				if (fs::exists(toolBinDir) && fs::is_directory(toolBinDir))
				{
					paths.push_back(toolBinDir);
				}

				// Also check the directory itself (for dependencies like ninja that don't use bin/)
				paths.push_back(entry.path().string());
			}
		}
	}

	return paths;
}

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::UpdateEnvironmentPaths()
{
	std::string newPath;

	for (auto const& localPath : GetLocalPaths())
	{
		if (fs::exists(localPath))
		{
			newPath += fs::absolute(localPath).string() + ":";
		}
	}

	for (auto const& userPath : GetUserPaths())
	{
		if (fs::exists(userPath))
		{
			newPath += userPath + ":";
		}
	}

	char const* currentPath = getenv("PATH");

	if (currentPath != nullptr)
	{
		newPath += currentPath;
	}

	setenv("PATH", newPath.c_str(), 1);

	gDepLog.Info(Tge::Logging::ETarget::File, "Updated PATH with dependency locations");
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyManager::GetSelectedPath(std::string_view identifier) const
{
	auto* dep = GetDependency(identifier);
	std::string path;

	if (dep != nullptr && dep->selectedLocation != nullptr)
	{
		path = dep->selectedLocation->path;
	}

	return path;
}

//////////////////////////////////////////////////////////////////////////
SCompiler CDependencyManager::GetSelectedCompiler(std::string_view identifier) const
{
	SCompiler result;
	auto const* dep{ GetDependency(identifier) };

	if (dep != nullptr && dep->selectedLocation != nullptr)
	{
		ECompilerKind const kind{ identifier == "g++" || identifier == "gcc"
			? ECompilerKind::Gcc : ECompilerKind::Clang };
		std::string const typeLabel{ kind == ECompilerKind::Gcc ? "GCC" : "Clang" };
		auto const& loc{ *dep->selectedLocation };

		result.path               = loc.path;
		result.kind               = kind;
		result.version            = loc.version;
		result.displayName        = typeLabel + " " + loc.version;
		result.hasProblematicPath = HasProblematicPathCharacters(loc.path);
		result.isRemovable        = loc.type != EInstallLocation::System;
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::SetDynamicRequired(std::string_view identifier, bool requiredForGcc, bool requiredForClang)
{
	auto* dep = GetDependency(identifier);

	if (dep != nullptr)
	{
		dep->requiredForGcc = requiredForGcc;
		dep->requiredForClang = requiredForClang;
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::InitializeBuildTools()
{
	// CMake — required for Clang only; GCC uses autoconf/make exclusively
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "CMake";
		dep->identifier = "cmake";
		dep->description = "Cross-platform build system generator";
		dep->systemPackage = "cmake";
		dep->category = EDependencyCategory::BuildTool;
		dep->requiredForGcc = false;
		dep->requiredForClang = true;
		dep->minimumVersion = "3.16";
		dep->versionCommand = "--version";
		dep->checkCommand = "--version";
		dep->installFunc = [this]() {
			auto* dep = GetDependency("cmake");
			TGE_ASSERT(dep != nullptr, "cmake dependency not found after registration");
			return Install(*dep);
		};
		m_dependencies.push_back(std::move(dep));
	}

	// Ninja — required for Clang (CMake Ninja generator); GCC uses make
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "Ninja";
		dep->identifier = "ninja";
		dep->alternativeNames = {"ninja-build"};
		dep->description = "Fast build system (required for Clang builds)";
		dep->systemPackage = "ninja-build"; // Ubuntu/Debian/Fedora; Arch uses "ninja" — handled in UI
		dep->category = EDependencyCategory::BuildTool;
		dep->requiredForGcc = false;
		dep->requiredForClang = true;
		dep->versionCommand = "--version";
		dep->checkCommand = "--version";
		dep->installFunc = [this]() {
			auto* dep = GetDependency("ninja");
			TGE_ASSERT(dep != nullptr, "ninja dependency not found after registration");
			return Install(*dep);
		};
		m_dependencies.push_back(std::move(dep));
	}

	// Git — required for both (source download)
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "Git";
		dep->identifier = "git";
		dep->description = "Version control system (required for source download)";
		dep->systemPackage = "git";
		dep->category = EDependencyCategory::BuildTool;
		dep->requiredForGcc = true;
		dep->requiredForClang = true;
		dep->versionCommand = "--version";
		dep->checkCommand = "--version";
		m_dependencies.push_back(std::move(dep));
	}

	// Curl — required for both (downloading cmake/ninja/dependencies from source)
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "curl";
		dep->identifier = "curl";
		dep->description = "HTTP download tool (required for downloading build dependencies)";
		dep->systemPackage = "curl";
		dep->category = EDependencyCategory::SystemUtility;
		dep->requiredForGcc = true;
		dep->requiredForClang = true;
		dep->versionCommand = "--version";
		dep->checkCommand = "--version";
		m_dependencies.push_back(std::move(dep));
	}

	// Unzip — required for both (extracting ninja and other zip archives)
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "unzip";
		dep->identifier = "unzip";
		dep->description = "ZIP archive extractor (required for extracting downloaded archives)";
		dep->systemPackage = "unzip";
		dep->category = EDependencyCategory::SystemUtility;
		dep->requiredForGcc = true;
		dep->requiredForClang = true;
		dep->versionCommand = "-v";
		dep->checkCommand = "-v";
		m_dependencies.push_back(std::move(dep));
	}

	// GCC-only build prerequisites — not needed for Clang
	// autoconf, automake, m4, libtool are NOT listed here: they're only needed for GCC development
	// (regenerating configure.ac / ltmain.sh). Pre-generated configure scripts and libtool are
	// bundled in the GCC tarball; GCC generates its own libtool script during configure.
	// name, description, systemPackage
	std::vector<std::tuple<std::string_view, std::string_view, std::string_view>> gccTools = {
		{"bison",    "GNU Bison parser generator (required for GCC)",              "bison"},
		{"flex",     "GNU Flex lexer generator (required for GCC)",                "flex"},
		{"perl",     "Perl programming language (required for GCC build scripts)", "perl"},
		{"make",     "GNU Make build tool (required for GCC)",                     "make"},
	};

	for (auto const& [name, desc, pkg] : gccTools)
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = std::string{name};
		dep->identifier = std::string{name};
		dep->description = std::string{desc};
		dep->systemPackage = std::string{pkg};
		dep->category = EDependencyCategory::BuildTool;
		dep->requiredForGcc = true;
		dep->requiredForClang = false;
		dep->versionCommand = "--version";
		dep->checkCommand = "--version";
		m_dependencies.push_back(std::move(dep));
	}

	// Fix Perl version detection: --version has empty first line, use $^V for single-line output
	auto* perlDep = GetDependency("perl");

	if (perlDep != nullptr)
	{
		perlDep->versionCommand = "-e 'print $^V'";
	}

	// Python3 — required for Clang (LLVM build scripts)
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "python3";
		dep->identifier = "python3";
		dep->description = "Python 3 (required for LLVM/Clang build scripts)";
		dep->systemPackage = "python3";
		dep->category = EDependencyCategory::BuildTool;
		dep->requiredForGcc = false;
		dep->requiredForClang = true;
		dep->versionCommand = "--version";
		dep->checkCommand = "--version";
		m_dependencies.push_back(std::move(dep));
	}

	// Set up prerequisite relationships for GCC autotools chain
	auto setPrerequisites = [this](std::string_view identifier, std::vector<std::string> prerequisites) {
		auto* dep = GetDependency(identifier);

		if (dep != nullptr)
		{
			dep->prerequisites = std::move(prerequisites);
		}
	};

	setPrerequisites("bison",     {"make"});
	setPrerequisites("flex",      {"make"});
}

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::InitializeLibraries()
{
	// m4 — required for GCC's autoconf-based configure system
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "m4";
		dep->identifier = "m4";
		dep->description = "GNU M4 macro processor (required for GCC configure)";
		dep->systemPackage = "m4";
		dep->category = EDependencyCategory::SystemUtility;
		dep->requiredForGcc = true;
		dep->requiredForClang = false;
		dep->versionCommand = "--version";
		dep->checkCommand = "--version";
		m_dependencies.push_back(std::move(dep));
	}

	// libffi dev headers — optional for LLVM/Clang; required only when LLVM_ENABLE_FFI=ON
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "libffi";
		dep->identifier = "libffi";
		dep->description = "libffi development headers (required when libffi is enabled in Clang settings)";
		dep->systemPackage = "libffi-dev"; // Ubuntu/Debian; Fedora/openSUSE: libffi-devel; Arch: libffi
		dep->category = EDependencyCategory::Library;
		dep->requiredForGcc = false;
		dep->requiredForClang = false; // Updated dynamically based on enableLibffi setting
		dep->checkCommand = "p=/usr/include/ffi.h; q=/usr/include/x86_64-linux-gnu/ffi.h; [ -f \"$p\" ] && echo \"$p\" || { [ -f \"$q\" ] && echo \"$q\"; }";
		dep->versionCommand = "pkg-config --modversion libffi";
		m_dependencies.push_back(std::move(dep));
	}

	// zlib dev headers — required for LLVM/Clang (LLVM_ENABLE_ZLIB defaults to FORCE_ON)
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "zlib";
		dep->identifier = "zlib";
		dep->description = "zlib development headers (recommended for LLVM — enables compression in debug info and object files)";
		dep->systemPackage = "zlib1g-dev"; // Ubuntu/Debian; Fedora/openSUSE: zlib-devel; Arch: zlib
		dep->category = EDependencyCategory::Library;
		dep->requiredForGcc = false;
		dep->requiredForClang = false; // Updated dynamically based on enableZlib setting
		dep->checkCommand = "[ -f /usr/include/zlib.h ] && echo /usr/include/zlib.h";
		dep->versionCommand = "pkg-config --modversion zlib";
		m_dependencies.push_back(std::move(dep));
	}

	// libtinfo dev headers — required for LLVM_ENABLE_TERMINFO=ON (colorized output via ncurses)
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "libtinfo";
		dep->identifier = "libtinfo";
		dep->description = "ncurses/terminfo development headers (required when Terminfo is enabled in Clang settings)";
		dep->systemPackage = "libncurses-dev"; // Ubuntu/Debian; Fedora/openSUSE: ncurses-devel; Arch: ncurses
		dep->category = EDependencyCategory::Library;
		dep->requiredForGcc = false;
		dep->requiredForClang = false; // Updated dynamically based on enableTerminfo setting
		dep->checkCommand = "[ -f /usr/include/curses.h ] && echo /usr/include/curses.h";
		dep->versionCommand = "pkg-config --modversion ncurses";
		m_dependencies.push_back(std::move(dep));
	}

	// libxml2 dev headers — optional for LLVM (LLVM_ENABLE_LIBXML2); needed by some LLVM tools
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "libxml2";
		dep->identifier = "libxml2";
		dep->description = "libxml2 development headers (required when libxml2 is enabled in Clang settings)";
		dep->systemPackage = "libxml2-dev"; // Ubuntu/Debian; Fedora/openSUSE: libxml2-devel; Arch: libxml2
		dep->category = EDependencyCategory::Library;
		dep->requiredForGcc = false;
		dep->requiredForClang = false; // Updated dynamically based on enableLibxml2 setting
		dep->checkCommand = "[ -f /usr/include/libxml2/libxml/parser.h ] && echo /usr/include/libxml2";
		dep->versionCommand = "pkg-config --modversion libxml-2.0";
		m_dependencies.push_back(std::move(dep));
	}

	// binutils plugin-api.h — required when building the LLVM gold linker plugin (LLVMgold.so).
	// Only plugin-api.h is needed; extracted locally from a binutils source tarball — no sudo required.
	{
		auto dep = std::make_unique<SAdvancedDependencyInfo>();
		dep->name = "binutils-include";
		dep->identifier = "binutils-include";
		dep->description = "binutils plugin-api.h header (enables building LLVMgold.so gold linker plugin)";
		dep->systemPackage = "binutils-dev"; // Ubuntu/Debian; Fedora/openSUSE: binutils-devel; Arch: binutils
		dep->category = EDependencyCategory::Library;
		dep->requiredForGcc = false;
		dep->requiredForClang = false; // Updated dynamically based on enableGoldPlugin setting

		// Detection and version are handled directly in ScanDependency via filesystem scan

		// Non-null installFunc marks this dep as user-locatable (shows Locate/Override buttons).
		// Actual installation goes through DownloadAndBuildLocal via the Locate dialog path.
		dep->installFunc = []() -> bool { return false; };

		m_dependencies.push_back(std::move(dep));
	}
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::Install(SAdvancedDependencyInfo const& dep)
{
	return DownloadAndBuildLocal(dep);
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::Install(SAdvancedDependencyInfo const& dep, std::string_view path, std::string& error)
{
	return DownloadAndBuildLocal(dep, path, error);
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyManager::PeekArchiveDirectory(std::string_view archivePath) const
{
	std::string command;

	if (archivePath.ends_with(".tar.xz"))
	{
		command = std::format("tar -tf \"{}\" | head -1", archivePath);
	}
	else if (archivePath.ends_with(".tar.gz") || archivePath.ends_with(".tgz"))
	{
		command = std::format("tar -tzf \"{}\" | head -1", archivePath);
	}
	else if (archivePath.ends_with(".tar.bz2") || archivePath.ends_with(".tbz2"))
	{
		command = std::format("tar -tjf \"{}\" | head -1", archivePath);
	}
	else if (archivePath.ends_with(".zip"))
	{
		command = std::format("unzip -l \"{}\" | awk 'NR==4 {{print $4}}'", archivePath);
	}
	else
	{
		gDepLog.Warning(Tge::Logging::ETarget::File, "PeekArchiveDirectory: Unsupported archive format: {}", archivePath);
	}

	std::string result;

	if (!command.empty())
	{
		auto const procResult = CProcessExecutor::Execute(command);

		if (procResult.success)
		{
			result = procResult.output;

			if (!result.empty() && result.back() == '\n')
			{
				result.pop_back();
			}

			size_t const slashPos = result.find('/');

			if (slashPos != std::string::npos)
			{
				result = result.substr(0, slashPos);
			}

			gDepLog.Info(Tge::Logging::ETarget::File, "PeekArchiveDirectory: {} -> {}", archivePath, result);
		}
		else
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "PeekArchiveDirectory: Command failed: {}", command);
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::DownloadAndBuildLocal(SAdvancedDependencyInfo const& dep)
{
	gDepLog.Info(Tge::Logging::ETarget::File, "Installation of {} requires user-provided source (local file/archive/URL)", dep.name);
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::DownloadAndBuildLocal(SAdvancedDependencyInfo const& dep, std::string_view path, std::string& error)
{
	std::string depsDir{ g_dataDir + "/dependencies" };
	std::string sourceDir{ depsDir + "/sources" };

	std::error_code ec;
	std::filesystem::create_directories(sourceDir, ec);

	if (ec)
	{
		error = std::format("Failed to create directories: {}", ec.message());
		gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
		return false;
	}

	// Use the provided URL or local path
	std::string sourcePath{path};
	gDepLog.Info(Tge::Logging::ETarget::File, "Using source for {}: {}", dep.identifier, sourcePath);

	// binutils-include: directory or direct plugin-api.h file → copy to deps dir immediately
	if (dep.identifier == "binutils-include" && !sourcePath.contains("://") && fs::exists(sourcePath))
	{
		fs::path const src{ sourcePath };
		std::string srcHeader;

		if (fs::is_directory(src))
		{
			fs::path const headerInDir{ src / "plugin-api.h" };

			if (fs::exists(headerInDir))
			{
				srcHeader = headerInDir.string();
			}
		}
		else if (src.filename() == "plugin-api.h")
		{
			srcHeader = sourcePath;
		}

		if (!srcHeader.empty())
		{
			std::string const outputDir{ depsDir + "/binutils-include" };
			std::string const headerPath{ outputDir + "/plugin-api.h" };
			std::error_code mkdirEc;
			fs::create_directories(outputDir, mkdirEc);
			bool result{ false };

			if (!mkdirEc)
			{
				std::error_code copyEc;
				fs::copy_file(srcHeader, headerPath, fs::copy_options::overwrite_existing, copyEc);

				if (!copyEc)
				{
					gDepLog.Info(Tge::Logging::ETarget::Console, "binutils-include: Installed plugin-api.h to {}", outputDir);
					result = true;
				}
				else
				{
					error = std::format("Failed to copy plugin-api.h: {}", copyEc.message());
					gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
				}
			}
			else
			{
				error = std::format("Failed to create binutils-include directory: {}", mkdirEc.message());
				gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
			}

			return result;
		}
		// else: local tarball — fall through to normal download/extract flow
	}

	// Special case: if pointing directly to an executable, install it directly
	if (std::filesystem::exists(sourcePath) && !std::filesystem::is_directory(sourcePath))
	{
		// Check if this is already an executable file (not an archive)
		std::filesystem::path pathObj(sourcePath);
		std::string filename{ pathObj.filename().string() };

		// Check if it's not an archive format
		if (!filename.ends_with(".tar.gz") && !filename.ends_with(".tar.xz") &&
		    !filename.ends_with(".tar.bz2") && !filename.ends_with(".zip") &&
		    !filename.ends_with(".tgz") && !filename.ends_with(".tbz2"))
		{
			// This appears to be a direct executable - install it directly
			std::string installDir{ depsDir + "/" + dep.identifier + "-custom" };
			std::string binDir{ installDir + "/bin" };

			std::error_code mkdirEc;
			fs::create_directories(binDir, mkdirEc);
			if (!mkdirEc)
			{
				std::string targetPath{ binDir + "/" + dep.identifier };

				// Copy the executable
				std::error_code copyEc;
				fs::copy_file(sourcePath, targetPath, copyEc);

				if (!copyEc)
				{
					// Make sure it's executable
					fs::permissions(targetPath,
						fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);

					gDepLog.Info(Tge::Logging::ETarget::File, "Successfully installed {} executable to {}", dep.identifier, targetPath);
					return true;
				}
				else
				{
					error = std::format("Failed to copy executable: {}", copyEc.message());
					gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
					return false;
				}
			}
			else
			{
				error = std::format("Failed to create install directory: {}", mkdirEc.message());
				gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
				return false;
			}
		}
	}

	bool isLocalFile{ false };
	std::string filename;

	if (sourcePath.starts_with("file://"))
	{
		isLocalFile = true;
		sourcePath = sourcePath.substr(7);
		size_t const lastSlash = sourcePath.find_last_of('/');

		if (lastSlash != std::string::npos)
		{
			filename = sourcePath.substr(lastSlash + 1);
		}
		else
		{
			filename = sourcePath;
		}
	}
	else if (std::filesystem::exists(sourcePath))
	{
		isLocalFile = true;
		filename = std::filesystem::path(sourcePath).filename().string();
	}
	else if (sourcePath.find("://") == std::string::npos)
	{
		if (sourcePath.starts_with("/") || sourcePath.starts_with("./") || sourcePath.starts_with("../") ||
		    sourcePath.contains('/') || sourcePath.contains('\\'))
		{
			isLocalFile = true;
		}
		else
		{
			std::string lower{ sourcePath };
			std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

			if (lower.ends_with(".tar.gz") || lower.ends_with(".tar.xz") || lower.ends_with(".tar.bz2") ||
			    lower.ends_with(".tar.zst") || lower.ends_with(".zip") || lower.ends_with(".tgz") || lower.ends_with(".tbz2"))
			{
				isLocalFile = true;
			}
		}

		if (isLocalFile)
		{
			filename = std::filesystem::path(sourcePath).filename().string();
		}
	}
	else
	{
		size_t const lastSlash = sourcePath.find_last_of('/');

		if (lastSlash != std::string::npos)
		{
			filename = sourcePath.substr(lastSlash + 1);
		}
		else
		{
			error = std::format("Invalid URL/path format: {}", sourcePath);
			gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
			return false;
		}
	}

	std::string sourceTarball{ sourceDir + "/" + filename };

	if (!fs::exists(sourceTarball))
	{
		if (isLocalFile)
		{
			if (std::filesystem::exists(sourcePath))
			{
				gDepLog.Info(Tge::Logging::ETarget::File, "Copying local file {} to {}", sourcePath, sourceTarball);
				std::error_code copyEc;
				std::filesystem::copy_file(sourcePath, sourceTarball, copyEc);

				if (copyEc)
				{
					error = std::format("Failed to copy local file: {}", copyEc.message());
					gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
					return false;
				}
			}
			else
			{
				error = std::format("Local file not found: {}", sourcePath);
				gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
				return false;
			}
		}
		else
		{
			std::string const curlCommand = "curl -L -o \"" + sourceTarball + "\" \"" + sourcePath + "\"";
			gDepLog.Info(Tge::Logging::ETarget::File, "Downloading {}: {}", dep.name, curlCommand);

			auto const downloadResult = CProcessExecutor::Execute(curlCommand);

			if (!downloadResult.success)
			{
				error = std::format("Failed to download: {}", sourcePath);
				gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
				return false;
			}
		}
	}
	else
	{
		gDepLog.Info(Tge::Logging::ETarget::File, "Source file already exists: {}", sourceTarball);
	}

	if (dep.identifier == "binutils-include")
	{
		// Derive versioned directory name from tarball (e.g. binutils-2.46.0.tar.xz → binutils-2.46.0)
		std::string archiveStem{ fs::path{ sourceTarball }.stem().string() };

		if (archiveStem.ends_with(".tar"))
		{
			archiveStem = fs::path{ archiveStem }.stem().string();
		}

		std::string const outputDir{ depsDir + "/" + archiveStem };
		std::string const headerPath{ outputDir + "/plugin-api.h" };
		std::error_code mkdirEc;
		fs::create_directories(outputDir, mkdirEc);
		bool result{ false };

		if (!mkdirEc)
		{
			std::string const extractCmd{ "tar --wildcards --strip-components=2 -C \"" + outputDir + "\" -xf \"" + sourceTarball + "\" '*/include/plugin-api.h'" };
			gDepLog.Info(Tge::Logging::ETarget::File, "Extracting plugin-api.h: {}", extractCmd);
			auto const extractResult = CProcessExecutor::Execute(extractCmd);

			if (extractResult.success && fs::exists(headerPath))
			{
				gDepLog.Info(Tge::Logging::ETarget::Console, "binutils-include: Installed plugin-api.h to {}", outputDir);
				result = true;
			}
			else
			{
				error = extractResult.output.empty()
					? std::string{ "Failed to extract plugin-api.h from archive" }
					: extractResult.output;
				gDepLog.Warning(Tge::Logging::ETarget::File, "binutils-include extract failed: {}", error);
			}
		}
		else
		{
			error = std::format("Failed to create directory {}: {}", outputDir, mkdirEc.message());
			gDepLog.Warning(Tge::Logging::ETarget::File, "binutils-include: {}", error);
		}

		return result;
	}

	// Use archive peeking to determine the actual extraction directory
	std::string actualDirName{ PeekArchiveDirectory(sourceTarball) };
	if (actualDirName.empty())
	{
		error = std::format("Failed to peek archive directory for {}", filename);
		gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
		return false;
	}

	std::string extractedDir{ sourceDir + "/" + actualDirName };
	std::string versionSpecificToolDir{ depsDir + "/" + actualDirName };

	if (dep.identifier != "ninja")
	{
		if (fs::exists(extractedDir))
		{
			gDepLog.Info(Tge::Logging::ETarget::File, "Removing existing directory: {}", extractedDir);
			fs::remove_all(extractedDir);
		}

		std::string extractCommand;

		if (filename.ends_with(".zip"))
		{
			extractCommand = "cd \"" + sourceDir + "\" && unzip -q \"" + filename + "\"";
		}
		else if (filename.ends_with(".tar.gz") || filename.ends_with(".tar.xz") ||
		         filename.ends_with(".tar.bz2") || filename.ends_with(".tar.zst") ||
		         filename.ends_with(".tgz") || filename.ends_with(".tbz2"))
		{
			extractCommand = "cd \"" + sourceDir + "\" && tar -xf \"" + filename + "\"";
		}
		else
		{
			error = std::format("Unsupported archive format: {}", filename);
			gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
			return false;
		}

		gDepLog.Info(Tge::Logging::ETarget::File, "Extracting {}: {}", dep.name, extractCommand);

		auto const extractResult = CProcessExecutor::Execute(extractCommand);

		if (!extractResult.success)
		{
			error = std::format("Failed to extract {}: {}", filename, extractResult.output.empty() ? extractResult.errorMessage : extractResult.output);
			gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
			return false;
		}

		if (!fs::exists(extractedDir))
		{
			error = std::format("Extraction failed - directory not found: {}", extractedDir);
			gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
			return false;
		}

		gDepLog.Info(Tge::Logging::ETarget::File, "Found extracted directory: {}", extractedDir);
	}

	if (dep.identifier == "cmake")
	{
		bool success{ fs::exists(versionSpecificToolDir) };

		if (success)
		{
			gDepLog.Info(Tge::Logging::ETarget::File, "CMake already installed at {}", versionSpecificToolDir);
		}
		else
		{
			std::error_code renameEc;
			fs::rename(extractedDir, versionSpecificToolDir, renameEc);

			if (!renameEc)
			{
				gDepLog.Info(Tge::Logging::ETarget::File, "Successfully installed CMake binary to {}", versionSpecificToolDir);
				success = true;
			}
			else
			{
				error = std::format("Failed to move CMake directory: {}", renameEc.message());
				gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
			}
		}

		return success;
	}

	if (dep.identifier == "bison" || dep.identifier == "flex")
	{
		return BuildFromSource(dep, extractedDir, versionSpecificToolDir, error);
	}

	if (dep.identifier == "ninja" &&
	    (filename.ends_with(".zip") || filename.ends_with(".tar.gz") || filename.ends_with(".tar.xz")))
	{
		std::string tempId{ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) };
		std::string tempExtractionDir{ sourceDir + "/temp-ninja-" + tempId };
		fs::create_directories(tempExtractionDir);

		// Pick extraction command based on archive type
		std::string extractCommand;

		if (filename.ends_with(".zip"))
		{
			extractCommand = "cd \"" + tempExtractionDir + "\" && unzip -q \"" + sourceDir + "/" + filename + "\"";
		}
		else
		{
			extractCommand = "cd \"" + tempExtractionDir + "\" && tar -xf \"" + sourceDir + "/" + filename + "\"";
		}

		gDepLog.Info(Tge::Logging::ETarget::File, "Extracting ninja archive: {}", extractCommand);

		auto const extractResult = CProcessExecutor::Execute(extractCommand);
		bool ninjaInstalled{ false };

		if (!extractResult.success)
		{
			error = std::format("Failed to extract ninja archive: {}", extractResult.output.empty() ? extractResult.errorMessage : extractResult.output);
			gDepLog.Error(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
		}
		else
		{
			// Bootstrap from source if configure.py is present (source tarball); skip for prebuilt binaries
			std::string configurePy;

			for (auto const& entry : fs::recursive_directory_iterator(tempExtractionDir))
			{
				if (entry.is_regular_file() && entry.path().filename() == "configure.py" && configurePy.empty())
				{
					configurePy = entry.path().string();
				}
			}

			bool bootstrapOk{ true };

			if (!configurePy.empty())
			{
				std::string const srcDir{ fs::path(configurePy).parent_path().string() };
				gDepLog.Info(Tge::Logging::ETarget::File, "Source archive detected — bootstrapping ninja: {}", srcDir);

				std::string const bootstrapCmd{ "cd \"" + srcDir + "\" && python3 configure.py --bootstrap 2>&1" };
				auto const bootstrapResult = CProcessExecutor::Execute(bootstrapCmd);

				if (!bootstrapResult.success)
				{
					error = std::format("Failed to bootstrap ninja: {}", bootstrapResult.output.empty() ? bootstrapResult.errorMessage : bootstrapResult.output);
					gDepLog.Error(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
					bootstrapOk = false;
				}
			}

			if (bootstrapOk)
			{
				// Find the ninja executable — works for both prebuilt and freshly bootstrapped
				std::string foundBinary;

				for (auto const& entry : fs::recursive_directory_iterator(tempExtractionDir))
				{
					if (entry.is_regular_file() && entry.path().filename() == "ninja" && foundBinary.empty())
					{
						auto const perms = fs::status(entry).permissions();

						if ((perms & fs::perms::owner_exec) != fs::perms::none)
						{
							foundBinary = entry.path().string();
						}
					}
				}

				if (foundBinary.empty())
				{
					error = "Ninja executable not found in archive";
					gDepLog.Error(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
				}
				else
				{
					std::string detectedVersion{ DetectVersion(foundBinary, dep.versionCommand) };

					if (detectedVersion.empty() || detectedVersion == "unknown")
					{
						error = "Failed to detect ninja version from binary";
						gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
					}
					else
					{
						versionSpecificToolDir = depsDir + "/ninja-" + detectedVersion;
						fs::create_directories(versionSpecificToolDir);
						std::string targetPath{ versionSpecificToolDir + "/ninja" };

						if (fs::exists(targetPath))
						{
							gDepLog.Info(Tge::Logging::ETarget::File, "Ninja {} already installed at {}, skipping copy", detectedVersion, versionSpecificToolDir);
							ninjaInstalled = true;
						}
						else
						{
							std::error_code copyEc;
							fs::copy_file(foundBinary, targetPath, copyEc);

							if (copyEc)
							{
								error = std::format("Failed to copy ninja binary: {}", copyEc.message());
								gDepLog.Error(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
							}
							else
							{
								fs::permissions(targetPath,
									fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
								gDepLog.Info(Tge::Logging::ETarget::File, "Successfully installed ninja {} to {}", detectedVersion, versionSpecificToolDir);
								ninjaInstalled = true;
							}
						}
					}
				}
			}
		}

		std::error_code cleanEc;
		uintmax_t const numRemoved = fs::remove_all(tempExtractionDir, cleanEc);

		if (cleanEc)
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "Failed to clean up temp directory {}: {} (error code: {})",
				tempExtractionDir, cleanEc.message(), cleanEc.value());
		}
		else
		{
			gDepLog.Info(Tge::Logging::ETarget::File, "Cleaned up temp directory {} ({} items removed)",
				tempExtractionDir, numRemoved);
		}

		if (!ninjaInstalled && error.empty())
		{
			error = "Failed to install ninja from archive";
			gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);
		}

		return ninjaInstalled;
	}

	error = std::format("Dependency {} requires prebuilt binary archives. Source building is not supported.", dep.name);
	gDepLog.Warning(Tge::Logging::ETarget::File, "DownloadAndBuildLocal: {}", error);

	return false;
}

static std::string ExtractErrorLine(std::string_view output)
{
	std::string lastError;
	std::istringstream stream{std::string{output}};
	std::string line;

	while (std::getline(stream, line))
	{
		if (line.find(": error:") != std::string::npos ||
		    line.find(": Error:") != std::string::npos ||
		    line.starts_with("error:") ||
		    line.starts_with("Error:"))
		{
			lastError = line;
		}
	}

	return lastError;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::BuildFromSource(SAdvancedDependencyInfo const& dep, std::string_view sourceDir, std::string_view installDir, std::string& error)
{
	gDepLog.Info(Tge::Logging::ETarget::File, "Building {} from source: {} -> {}", dep.name, sourceDir, installDir);

	std::string const executablePath = std::format("{}/bin/{}", installDir, dep.identifier);
	bool success{ fs::exists(executablePath) };

	if (success)
	{
		gDepLog.Info(Tge::Logging::ETarget::File, "{} already built at {}", dep.name, installDir);
	}
	else
	{
		std::vector<std::string> const requiredTools = {"make", "gcc"};
		bool toolsAvailable{ true };

		for (auto const& tool : requiredTools)
		{
			if (toolsAvailable && !CProcessExecutor::Execute("which " + tool).success)
			{
				error = std::format("Cannot build {}: required tool '{}' not found", dep.name, tool);
				gDepLog.Warning(Tge::Logging::ETarget::File, "BuildFromSource: {}", error);
				toolsAvailable = false;
			}
		}

		if (toolsAvailable)
		{
			std::vector<std::string> const commands = {
				std::format("cd \"{}\" && ./configure --prefix=\"{}\"", sourceDir, installDir),
				std::format("cd \"{}\" && make -j{}", sourceDir, std::thread::hardware_concurrency()),
				std::format("cd \"{}\" && make install", sourceDir)
			};

			bool buildSuccess{ true };

			for (auto const& command : commands)
			{
				if (buildSuccess)
				{
					gDepLog.Info(Tge::Logging::ETarget::File, "Executing: {}", command);

					auto const buildResult = CProcessExecutor::Execute(command);

					if (!buildResult.success)
					{
						std::string_view const output = buildResult.output.empty() ? buildResult.errorMessage : buildResult.output;
						std::string const errorLine{ ExtractErrorLine(output) };
						error = errorLine.empty()
							? std::format("Build step failed (exit {})", buildResult.exitCode)
							: errorLine;
						gDepLog.Warning(Tge::Logging::ETarget::File, "Build command failed (exit {}): {}\nOutput: {}", buildResult.exitCode, command, output);
						buildSuccess = false;
					}
				}
			}

			if (buildSuccess)
			{
				if (fs::exists(executablePath))
				{
					gDepLog.Info(Tge::Logging::ETarget::File, "Successfully built and installed {} to {}", dep.name, installDir);
					success = true;
				}
				else
				{
					error = std::format("Build completed but {} executable not found at {}", dep.name, executablePath);
				gDepLog.Warning(Tge::Logging::ETarget::File, "BuildFromSource: {}", error);
				}
			}
		}
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::LoadConfiguration()
{
	bool success{ true };

	if (fs::exists(m_configFilePath))
	{
		std::ifstream file(m_configFilePath);

		if (!file.is_open())
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "Failed to open config file: {}", m_configFilePath);
			success = false;
		}
		else
		{
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
						size_t const equalPos = line.find('=');

						if (equalPos != std::string::npos && !currentSection.empty())
						{
							std::string key{ line.substr(0, equalPos) };
							std::string value{ line.substr(equalPos + 1) };

							auto* dep = GetDependency(currentSection);

							if (dep != nullptr && key == "selected_location")
							{
								std::regex const locationRegex(R"((\w+):([^:]+):(\d+))");
								std::smatch match;

								if (std::regex_match(value, match, locationRegex))
								{
									EInstallLocation locType = EInstallLocation::System;

									if (match[1] == "UserWide") locType = EInstallLocation::UserWide;
									else if (match[1] == "LocalApp") locType = EInstallLocation::LocalApp;

									std::string matchPath{ match[2] };
									std::string priorityStr{ match[3] };
									int priority{};
									std::from_chars(priorityStr.data(), priorityStr.data() + priorityStr.size(), priority);

									bool locFound{ false };

									for (auto& loc : dep->foundLocations)
									{
										if (!locFound && loc.type == locType && loc.path == matchPath)
										{
											loc.priority = priority;
											locFound = true;
										}
									}
								}
							}
						}
					}
				}
			}

			gDepLog.Info(Tge::Logging::ETarget::File, "Loaded dependency configuration from {}", m_configFilePath);
		}
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::SaveConfiguration()
{
	std::ofstream file(m_configFilePath);
	if (!file.is_open())
	{
		gDepLog.Warning(Tge::Logging::ETarget::File, "Failed to create config file: {}", m_configFilePath);
		return false;
	}

	file << "# Compilatron Dependency Configuration\n";
	file << "# Generated automatically - edit with care\n\n";

	for (auto const& dep : m_dependencies)
	{
		if (dep->selectedLocation)
		{
			file << "[" << dep->identifier << "]\n";

			std::string locTypeStr;
			switch (dep->selectedLocation->type)
			{
				case EInstallLocation::System: locTypeStr = "System"; break;
				case EInstallLocation::UserWide: locTypeStr = "UserWide"; break;
				case EInstallLocation::LocalApp: locTypeStr = "LocalApp"; break;
			}

			file << "selected_location=" << locTypeStr << ":"
				 << dep->selectedLocation->path << ":"
				 << dep->selectedLocation->priority << "\n\n";
		}
	}

	gDepLog.Info(Tge::Logging::ETarget::File, "Saved dependency configuration to {}", m_configFilePath);
	return true;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::InstallDependency(std::string_view identifier)
{
	auto* dep = GetDependency(identifier);
	bool success{ false };

	if (dep != nullptr && dep->installFunc)
	{
		success = dep->installFunc();

		if (success)
		{
			ScanDependency(*dep);
		}
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::SetSelectedLocation(std::string_view identifier,
	EInstallLocation location, std::string_view path)
{
	auto* dep = GetDependency(identifier);
	bool found{ false };

	if (dep != nullptr)
	{
		auto it = std::ranges::find_if(dep->foundLocations, [&](auto const& loc) {
			return loc.type == location && loc.path == path;
		});
		found = it != dep->foundLocations.end();

		if (found)
		{
			dep->selectedLocation = &*it;

			if (!SaveConfiguration())
			{
				gDepLog.Warning("Failed to save dependency configuration after setting location for: {}", identifier);
			}
		}
	}

	return found;
}

//////////////////////////////////////////////////////////////////////////
void CDependencyManager::RemoveDuplicateLocations(std::vector<SDependencyLocation>& locations)
{
	// Deduplication uses canonical paths to handle symlinks (e.g. /usr/bin/cmake and /bin/cmake resolving to the same file)
	std::vector<SDependencyLocation> unique;
	std::set<std::string> seenCanonicalPaths;

	for (auto const& location : locations)
	{
		std::error_code ec;
		std::string const canonicalPath = fs::canonical(location.path, ec).string();

		if (!ec)
		{
			if (seenCanonicalPaths.find(canonicalPath) == seenCanonicalPaths.end())
			{
				seenCanonicalPaths.insert(canonicalPath);
				unique.push_back(location);
			}
		}
		else
		{
			unique.push_back(location);
		}
	}

	locations = std::move(unique);
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CDependencyManager::GetMissingPrerequisites(std::string_view identifier) const
{
	std::vector<std::string> missing;

	auto* dep = GetDependency(identifier);

	if (dep != nullptr)
	{
		for (std::string const& prereqId : dep->prerequisites)
		{
			auto* prereq = GetDependency(prereqId);

			if (prereq == nullptr || prereq->status != EDependencyStatus::Available)
			{
				missing.push_back(prereqId);
			}
		}
	}

	return missing;
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CDependencyManager::GetInstallationOrder(std::vector<std::string> const& identifiers) const
{
	std::vector<std::string> result;
	std::set<std::string> processed;

	std::function<void(std::string const&)> addWithPrerequisites = [&](std::string const& id) {
		if (processed.find(id) == processed.end())
		{
			auto* dep = GetDependency(id);

			if (dep != nullptr)
			{
				for (std::string const& prereqId : dep->prerequisites)
				{
					addWithPrerequisites(prereqId);
				}

				if (std::find(identifiers.begin(), identifiers.end(), id) != identifiers.end())
				{
					result.push_back(id);
					processed.insert(id);
				}
			}
		}
	};

	for (std::string const& id : identifiers)
	{
		addWithPrerequisites(id);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyManager::RegisterAdditionalVersion(std::string_view identifier, std::string_view executablePath, std::string_view version)
{
	std::string newBinaryDir;
	bool success{ false };

	{
		std::lock_guard<std::mutex> lock(m_dependenciesMutex);

		auto* dep = GetDependency(identifier);

		if (dep != nullptr)
		{
			bool const alreadyRegistered = std::ranges::any_of(dep->foundLocations, [&](auto const& loc) {
				return loc.path == executablePath;
			});

			if (!alreadyRegistered)
			{
				EInstallLocation locationType = EInstallLocation::LocalApp;

				if (executablePath.find("/usr/") == 0 || executablePath.find("/bin/") == 0)
				{
					locationType = EInstallLocation::System;
				}
				else
				{
					char const* home = std::getenv("HOME");

					if (home != nullptr && executablePath.find(home) == 0)
					{
						locationType = EInstallLocation::UserWide;
					}
				}

				bool const isWorking = TestFunctionality(executablePath, dep->checkCommand);

				SDependencyLocation newLocation;
				newLocation.type = locationType;
				newLocation.path = std::string{executablePath};
				newLocation.version = std::string{version};
				newLocation.isWorking = isWorking;
				newLocation.priority = static_cast<int>(dep->foundLocations.size());

				// Save current selected path before push_back — it may reallocate and invalidate the pointer
				std::string const prevSelectedPath = (dep->selectedLocation != nullptr) ? dep->selectedLocation->path : "";

				dep->foundLocations.push_back(std::move(newLocation));

				// Restore selectedLocation pointer after potential reallocation
				if (!prevSelectedPath.empty())
				{
					for (auto& loc : dep->foundLocations)
					{
						if (loc.path == prevSelectedPath)
						{
							dep->selectedLocation = &loc;
							break;
						}
					}
				}

				// Explicit user registration always selects the new path when working
				if (isWorking)
				{
					dep->status = EDependencyStatus::Available;
					dep->selectedLocation = &dep->foundLocations.back();
					newBinaryDir = fs::path(executablePath).parent_path().string();
				}

				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyManager: Registered additional version of '{}': {} ({}) - {}",
					identifier, executablePath, version, isWorking ? "working" : "not working");
				success = true;
			}
			else
			{
				// Path already registered — treat as success; re-select it if working
				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyManager: Path '{}' already registered for dependency '{}' — re-selecting", executablePath, identifier);

				for (auto& loc : dep->foundLocations)
				{
					if (loc.path == executablePath)
					{
						if (loc.isWorking)
						{
							dep->status = EDependencyStatus::Available;
							dep->selectedLocation = &loc;
							newBinaryDir = fs::path(executablePath).parent_path().string();
						}
						break;
					}
				}

				success = true;
			}
		}
		else
		{
			gDepLog.Warning(Tge::Logging::ETarget::File, "DependencyManager: Cannot register version - dependency '{}' not found", identifier);
		}
	}

	// Add the external binary's directory to PATH so the build system can find it
	if (!newBinaryDir.empty())
	{
		char const* currentPath = getenv("PATH");

		if (currentPath != nullptr && std::string_view{currentPath}.find(newBinaryDir) == std::string_view::npos)
		{
			std::string const newPath = newBinaryDir + ":" + currentPath;
			setenv("PATH", newPath.c_str(), 1);
			gDepLog.Info(Tge::Logging::ETarget::File, "DependencyManager: Added {} to PATH for registered dependency", newBinaryDir);
		}
	}

	return success;
}

} // namespace Ctrn
