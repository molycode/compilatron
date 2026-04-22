#pragma once

#include "build/compiler.hpp"

#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>

namespace Ctrn
{

enum class EDependencyCategory
{
	BuildTool,      // cmake, ninja, git, bison, flex, etc. — path-overridable
	SystemUtility,  // curl, unzip, m4, etc. — must be on PATH, no custom path needed
	Library,        // zlib, libffi, etc. — dev headers checked via pkg-config
};

enum class EInstallLocation
{
	System,         // /usr/bin, /usr/lib (detection only)
	UserWide,       // ~/.local/bin, ~/.local/lib (detection only)
	LocalApp        // ./dependencies/{toolname}/bin, ./dependencies/{toolname}/lib (detection and installation)
};

enum class EDependencyStatus
{
	Available,      // Found and working
	Missing,        // Not found anywhere
	MultipleFound,  // Found in multiple locations
	Outdated,       // Found but version too old
	Broken          // Found but not working properly
};

struct SDependencyLocation final
{
	EInstallLocation type;
	std::string path;           // Full path to executable/library
	std::string version;        // Detected version string
	bool isWorking;            // Tested and confirmed working
	int priority;              // User preference priority (0=highest)
};

struct SAdvancedDependencyInfo final
{
	std::string name;                                    // Display name
	std::string identifier;                              // Unique identifier
	std::vector<std::string> alternativeNames;           // Alternative command names
	std::string description;                             // User-friendly description
	std::string systemPackage;                           // Package name for distro install advice (apt/dnf/pacman)
	EDependencyCategory category;
	bool requiredForGcc{ false };                        // Required to build GCC
	bool requiredForClang{ false };                      // Required to build Clang
	std::string minimumVersion;                          // Minimum required version
	std::string checkCommand;                            // Command to verify functionality
	std::string versionCommand;                          // Command to get version
	std::vector<SDependencyLocation> foundLocations;     // All found installations
	SDependencyLocation* selectedLocation;               // User's selected location
	EDependencyStatus status;

	// Custom installation function (local installation only)
	std::function<bool()> installFunc;

	std::vector<std::string> prerequisites;              // Dependencies that must be installed first

	[[nodiscard]] bool IsRequired() const { return requiredForGcc || requiredForClang; }
};

class CDependencyManager final : private Tge::SNoCopyNoMove
{
public:

	CDependencyManager() = default;
	~CDependencyManager() = default;

	void InitializeAllDependencies();
	void ScanAllDependencies();
	[[nodiscard]] bool AreAllRequiredDependenciesAvailable() const;
	std::vector<SAdvancedDependencyInfo*> GetMissingRequiredDependencies() const;
	std::vector<SAdvancedDependencyInfo*> GetAllDependencies() const;
	SAdvancedDependencyInfo* GetDependency(std::string_view identifier) const;
	SAdvancedDependencyInfo* GetDependencyByName(std::string_view name) const;

	[[nodiscard]] bool InstallDependency(std::string_view identifier);

	std::vector<std::string> GetMissingPrerequisites(std::string_view identifier) const;
	std::vector<std::string> GetInstallationOrder(std::vector<std::string> const& identifiers) const;

	void UpdateEnvironmentPaths();
	std::string GetSelectedPath(std::string_view identifier) const;
	[[nodiscard]] SCompiler GetSelectedCompiler(std::string_view identifier) const;
	void SetDynamicRequired(std::string_view identifier, bool requiredForGcc, bool requiredForClang);
	std::string DetectVersion(std::string_view path, std::string_view versionCommand);

	// Installation targets local paths only (not system/user-wide)
	[[nodiscard]] bool Install(SAdvancedDependencyInfo const& dep);
	[[nodiscard]] bool Install(SAdvancedDependencyInfo const& dep, std::string_view path, std::string& error);

	[[nodiscard]] bool RegisterAdditionalVersion(std::string_view identifier, std::string_view executablePath, std::string_view version);

private:

	std::vector<std::unique_ptr<SAdvancedDependencyInfo>> m_dependencies;
	mutable std::mutex m_dependenciesMutex;  // Protect shared dependency data from race conditions

	void ScanDependency(SAdvancedDependencyInfo& dep);
	std::vector<SDependencyLocation> FindAllLocations(SAdvancedDependencyInfo const& dep);
	SDependencyLocation ScanLocation(EInstallLocation locationType, std::string_view basePath, SAdvancedDependencyInfo const& dep);
	void RemoveDuplicateLocations(std::vector<SDependencyLocation>& locations);
	bool TestFunctionality(std::string_view path, std::string_view checkCommand);

	bool DownloadAndBuildLocal(SAdvancedDependencyInfo const& dep);
	bool DownloadAndBuildLocal(SAdvancedDependencyInfo const& dep, std::string_view path, std::string& error);
	bool BuildFromSource(SAdvancedDependencyInfo const& dep, std::string_view sourceDir, std::string_view installDir, std::string& error);
	std::string PeekArchiveDirectory(std::string_view archivePath) const;

	std::vector<std::string> GetLocalPaths() const;

	void InitializeBuildTools();
	void InitializeLibraries();
};

} // namespace Ctrn
