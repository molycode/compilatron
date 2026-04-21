#include "build/compiler_builder.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "common/process_executor.hpp"
#include "dependency/dependency_manager.hpp"
#include <tge/init/assert.hpp>
#include <fstream>
#include <format>
#include <optional>
#include <algorithm>
#include <filesystem>
#include <array>
#include <unistd.h>
#include <ctime>
#include <thread>

namespace Ctrn
{
namespace fs = std::filesystem;

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::Initialize()
{
	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Initializing compiler builder with {} jobs", m_numJobs);
	InitializeDependencies();

	fs::path const dataPath{ g_dataDir };
	m_buildDir = (dataPath / "build_compilers").string();
	m_sourceDir = (dataPath / "sources").string();
	m_installPrefix = (dataPath / "compilers").string();
	m_depsDir = (dataPath / "dependencies").string();
	m_depsBinDir = (dataPath / "dependencies" / "bin").string();

	m_progress.statusMessage = "Ready to build";

	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Build directories: build={}, source={}, install={}", m_buildDir, m_sourceDir, m_installPrefix);
}

//////////////////////////////////////////////////////////////////////////
CCompilerBuilder::~CCompilerBuilder()
{
	StopBuild();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::StartBuild(
	std::vector<CCompilerUnit*> units,
	SBuildSettings const& settings,
	ProgressCallback progressCb,
	CompletionCallback completionCb)
{
	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: StartBuild called - checking if already building");

	if (!m_isBuilding)
	{
		m_units = std::move(units);
		m_progressCallback = progressCb;
		m_completionCallback = completionCb;

		m_shouldStop = false;
		m_isBuilding = true;

		{
			std::lock_guard<std::mutex> lock(m_outputMutex);
			m_outputLines.clear();
		}

		if (m_buildThread.joinable())
		{
			m_buildThread.join();
		}

		gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Starting build thread with {} compiler entries", settings.compilerEntries.size());
		m_buildThread = std::thread(&CCompilerBuilder::BuildThreadFunc, this, settings);
	}
	else
	{
		gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Build already in progress, ignoring StartBuild request");
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::StopBuild()
{
	if (m_isBuilding)
	{
		gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Stopping build");
	}

	m_shouldStop = true;

	for (auto* unit : m_units)
	{
		if (unit != nullptr)
		{
			unit->RequestStop();
		}
	}

	if (m_buildThread.joinable())
	{
		m_buildThread.join();
	}

	// Clear callbacks and unit references after join — safe, build thread is done
	m_units.clear();
	m_progressCallback = nullptr;
	m_completionCallback = nullptr;

	{
		std::lock_guard<std::mutex> lock(m_compilerThreadsMutex);

		for (auto& thread : m_compilerThreads)
		{
			if (thread.joinable())
			{
				thread.join();
			}
		}
		m_compilerThreads.clear();
	}

	m_isBuilding = false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::IsBuilding() const
{
	return m_isBuilding;
}

//////////////////////////////////////////////////////////////////////////
SBuildProgress CCompilerBuilder::GetProgress() const
{
	std::lock_guard<std::mutex> lock(m_progressMutex);
	return m_progress;
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CCompilerBuilder::GetOutputLines() const
{
	std::lock_guard<std::mutex> lock(m_outputMutex);
	return m_outputLines;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::BuildThreadFunc(SBuildSettings const& settings)
{
	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Build thread started");
	m_sleepInhibitor.Acquire();
	bool success{ true };

	if (fs::path(settings.installDirectory).is_absolute())
	{
		m_installPrefix = settings.installDirectory;
	}
	else
	{
		if (g_dataDir.empty())
		{
			m_installPrefix = fs::temp_directory_path() / "compilatron" / settings.installDirectory;
		}
		else
		{
			m_installPrefix = fs::path(g_dataDir) / settings.installDirectory;
		}
	}

	gLog.Info(Tge::Logging::ETarget::File, "Install directory set to: {}", m_installPrefix);

	if (!m_shouldStop)
	{
		gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Phase 0: Starting cleanup of previous build artifacts");
		UpdateProgress(EBuildPhase::CleaningPreviousBuild, 0.0f, "Cleaning up previous build artifacts...", "Removing old build directories and previous installations");

		if (!CleanupPreviousBuild(settings))
		{
			gLog.Warning(Tge::Logging::ETarget::Console, "Failed to clean some previous build artifacts, continuing...");
		}
	}

	if (!m_shouldStop)
	{
		gLog.Info(Tge::Logging::ETarget::File, "Phase 1: Checking build dependencies");
		UpdateProgress(EBuildPhase::CheckingDependencies, 0.0f, "Checking build dependencies...");

		if (!CheckDependencies())
		{
			gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Phase 2: Dependencies missing, building locally");
			UpdateProgress(EBuildPhase::InstallingDependencies, 0.0f, "Building missing dependencies locally...", "CMake, Ninja, and other build tools");

			if (!BuildLocalDependencies())
			{
				gLog.Error(Tge::Logging::ETarget::Console, "Failed to build required dependencies");
				UpdateProgress(EBuildPhase::Failed, 0.0f, "Build failed: Failed to build required dependencies");
				success = false;
			}
		}
	}

	if (!m_shouldStop && success)
	{
		gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Phase 3: Building compilers using CCompilerUnit architecture");
		UpdateProgress(EBuildPhase::DownloadingSources, 0.0f, "Building compilers...", "Building compilers using unit architecture");
		BuildUsingCompilerUnits(m_units);
	}
	else if (m_shouldStop)
	{
		gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Build stopped by user");
		UpdateProgress(EBuildPhase::Failed, 0.0f, "Build cancelled");
		success = false;
	}

	if (success)
	{
		bool needsCleanup{ std::ranges::any_of(settings.compilerEntries, [](auto const& entry)
		{
			return !entry.keepDependencies || !entry.keepSources;
		}) };

		if (needsCleanup)
		{
			CleanupAfterBuild(settings);
		}
	}

	m_sleepInhibitor.Release();
	m_isBuilding = false;

	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Build thread finished, success={}", success ? "true" : "false");

	if (m_completionCallback)
	{
		m_completionCallback(success, success ? "Build completed successfully" : "Build failed");
		RequestRedraw();
	}
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::CleanupPreviousBuild(SBuildSettings const& settings)
{
	bool success{ true };

	// Note: Build directories are cleaned per-compiler by CheckAndCleanCompilerCache()
	// which provides detailed fingerprint logging. We only clean temp directories here.

	constexpr std::array tempPatterns = {"build_cmake_", "build_ninja_", "build_make_", "build_flex_", "build_m4_", "build_bison_"};

	for (auto const& pattern : tempPatterns)
	{
		std::error_code ec;
		fs::path currentPath = fs::current_path(ec);

		if (!ec)
		{
			for (auto const& entry : fs::directory_iterator(currentPath, ec))
			{
				if (entry.is_directory())
				{
					std::string dirName{ entry.path().filename().string() };

					if (dirName.find(pattern) == 0)
					{
						gLog.Info("Removing temporary directory: {}", dirName);
						std::error_code removeEc;
						fs::remove_all(entry.path(), removeEc);

						if (removeEc)
						{
							gLog.Warning("Failed to remove {}: {}", dirName, removeEc.message());
							success = false;
						}
					}
				}
			}
		}
	}

	gLog.Info(Tge::Logging::ETarget::File, "Previous build cleanup completed");

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::CheckDependencies()
{
	// Only perform actual dependency checking once per instance
	if (!m_dependenciesChecked)
	{
		gLog.Info(Tge::Logging::ETarget::File, "CCompilerBuilder::CheckDependencies() - performing initial check");

		for (auto& dep : m_dependencies)
		{
			UpdateProgress(EBuildPhase::CheckingDependencies, 0.0f,
			              "Checking " + dep.name + "...", dep.checkCommand);

			dep.available = CheckCommandExists(dep.checkCommand) || CheckLocalCommandExists(dep.checkCommand);

			if (!dep.available && dep.required)
			{
				gLog.Warning(Tge::Logging::ETarget::File, "Missing: {}", dep.name);
			}
			else if (dep.available)
			{
				gLog.Info(Tge::Logging::ETarget::File, "Found: {}", dep.name);
			}
		}

		m_dependenciesChecked = true;
	}
	else
	{
		gLog.Info(Tge::Logging::ETarget::File, "CCompilerBuilder::CheckDependencies() - using cached results");
	}

	bool allAvailable{ true };

	for (auto const& dep : m_dependencies)
	{
		if (!dep.available && dep.required)
		{
			allAvailable = false;
		}
	}

	return allAvailable;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::BuildLocalDependencies()
{
	fs::create_directories(m_depsDir);
	fs::create_directories(m_depsBinDir);

	bool success{ true };

	for (auto const& dep : m_dependencies)
	{
		if (!dep.available && dep.required)
		{
			if (dep.checkCommand == "cmake")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.2f, "Building CMake locally...");

				if (!DownloadAndBuildCMake())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "ninja")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.5f, "Building Ninja locally...");

				if (!DownloadAndBuildNinja())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "git")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.8f, "Building Git locally...");

				if (!DownloadAndBuildGit())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "python3")
			{
				gLog.Warning(Tge::Logging::ETarget::Console, "Python3 must be installed via the system package manager.");
				success = false; // Python3 is complex to build, require system installation
			}
			else if (dep.checkCommand == "make")
			{
				gLog.Info(Tge::Logging::ETarget::File, "Make not available system-wide. Using Ninja as primary build tool.");
				// Make is optional since we prefer Ninja
			}
			else if (dep.checkCommand == "curl")
			{
				gLog.Warning(Tge::Logging::ETarget::Console, "curl not available — downloading cmake/ninja from source will fail.");
				success = false;
			}
			else if (dep.checkCommand == "unzip")
			{
				gLog.Warning(Tge::Logging::ETarget::Console, "unzip not available — extracting zip archives will fail.");
				success = false;
			}
			else if (dep.checkCommand == "bison")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.15f, "Building Bison locally...");

				if (!DownloadAndBuildBison())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "flex")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.25f, "Building Flex locally...");

				if (!DownloadAndBuildFlex())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "autoconf")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.35f, "Building Autoconf locally...");

				if (!DownloadAndBuildAutoconf())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "automake")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.45f, "Building Automake locally...");

				if (!DownloadAndBuildAutomake())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "libtool")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.55f, "Building Libtool locally...");

				if (!DownloadAndBuildLibtool())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "pkg-config")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.65f, "Building Pkg-Config locally...");

				if (!DownloadAndBuildPkgConfig())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "perl")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.75f, "Building Perl locally...");

				if (!DownloadAndBuildPerl())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "gettext")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.85f, "Building Gettext locally...");

				if (!DownloadAndBuildGettext())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "makeinfo")
			{
				UpdateProgress(EBuildPhase::InstallingDependencies, 0.90f, "Building Texinfo locally...");

				if (!DownloadAndBuildTexinfo())
				{
					success = false;
				}
			}
			else if (dep.checkCommand == "gcc" || dep.checkCommand == "g++")
			{
				if (CheckCommandExists("clang") && CheckCommandExists("clang++"))
				{
					gLog.Info(Tge::Logging::ETarget::File, "Using system Clang as bootstrap compiler for GCC builds.");
				}
				else if (CheckLocalCommandExists("clang") && CheckLocalCommandExists("clang++"))
				{
					gLog.Info(Tge::Logging::ETarget::File, "Using locally built Clang as bootstrap compiler for GCC builds.");
				}
				else if (fs::exists("/home/thomas/compilers"))
				{
					gLog.Info(Tge::Logging::ETarget::File, "Searching for built compilers to use as bootstrap...");
					bool bootstrapFound{ false };

					for (auto const& entry : fs::directory_iterator("/home/thomas/compilers"))
					{
						if (!bootstrapFound && entry.is_directory())
						{
							std::string clangPath{ entry.path() / "bin" / "clang++" };

							if (fs::exists(clangPath))
							{
								gLog.Info(Tge::Logging::ETarget::File, "Found bootstrap compiler: {}", clangPath);
								bootstrapFound = true;
							}
						}
					}
				}
				else
				{
					gLog.Error(Tge::Logging::ETarget::Console, "No bootstrap compiler available. Need GCC, Clang, or built compiler for bootstrapping.");
					success = false;
				}
			}
		}
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildCMake()
{
	std::string cmakeVersion{ "3.30.3" };
	std::string cmakeTarball{ "cmake-" + cmakeVersion + "-linux-x86_64.tar.gz" };
	std::string downloadUrl{ "https://github.com/Kitware/CMake/releases/download/v" + cmakeVersion + "/" + cmakeTarball };

	std::string downloadCmd{ "cd " + m_depsDir + " && curl -L -o " + cmakeTarball + " " + downloadUrl };

	if (!ExecuteCommand(downloadCmd))
	{
		return false;
	}

	std::string extractCmd{ "cd " + m_depsDir + " && tar -xzf " + cmakeTarball };

	if (!ExecuteCommand(extractCmd))
	{
		return false;
	}

	std::string copyCmd{ "cd " + m_depsDir + " && cp -r cmake-" + cmakeVersion + "-linux-x86_64/bin/* " + m_depsBinDir + "/" };
	return ExecuteCommand(copyCmd);
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildNinja()
{
	std::string downloadCmd{ "cd " + m_depsDir + " && curl -L -o ninja-linux.zip https://github.com/ninja-build/ninja/releases/latest/download/ninja-linux.zip" };

	if (!ExecuteCommand(downloadCmd))
	{
		return false;
	}

	std::string extractCmd{ "cd " + m_depsDir + " && unzip -o ninja-linux.zip" };

	if (!ExecuteCommand(extractCmd))
	{
		return false;
	}

	std::string copyCmd{ "cd " + m_depsDir + " && cp ninja " + m_depsBinDir + "/" };
	return ExecuteCommand(copyCmd);
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildGit()
{
	// For Git, we typically rely on system installation due to complexity
	gLog.Warning(Tge::Logging::ETarget::Console, "Git must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildBison()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Bison must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildFlex()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Flex must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildAutoconf()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Autoconf must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildAutomake()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Automake must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildLibtool()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Libtool must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildPkgConfig()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Pkg-config must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildPerl()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Perl must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildGettext()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Gettext must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::DownloadAndBuildTexinfo()
{
	gLog.Warning(Tge::Logging::ETarget::Console, "Texinfo must be installed via the system package manager");
	return false;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::InitializeDependencies()
{
	m_dependencies = {
		{"CMake", "cmake", "cmake", EDependencyType::BuildTool, true, false},
		{"Ninja", "ninja-build", "ninja", EDependencyType::BuildTool, true, false},
		{"Git", "git", "git", EDependencyType::BuildTool, true, false},
		{"Python3", "python3", "python3", EDependencyType::BuildTool, true, false},
		{"GCC", "gcc", "gcc", EDependencyType::Compiler, false, false},
		{"G++", "g++", "g++", EDependencyType::Compiler, false, false},
		{"Make",  "make",  "make",  EDependencyType::BuildTool, false, false},
		{"Curl",  "curl",  "curl",  EDependencyType::BuildTool, false, false},
		{"Unzip", "unzip", "unzip", EDependencyType::BuildTool, false, false}
	};
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerBuilder::GetDistroId() const
{
	std::ifstream file("/etc/os-release");
	std::string line{};
	std::string distroId{};

	while (std::getline(file, line))
	{
		if (line.find("ID=") == 0)
		{
			distroId = line.substr(3);
		}
	}

	return distroId.empty() ? "unknown" : distroId;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerBuilder::StatusToString(ECompilerStatus status)
{
	switch (status)
	{
		case ECompilerStatus::NotStarted: return "NotStarted";
		case ECompilerStatus::Cloning:    return "Cloning";
		case ECompilerStatus::Waiting:    return "Waiting";
		case ECompilerStatus::Building:   return "Building";
		case ECompilerStatus::Success:    return "Success";
		case ECompilerStatus::Failed:     return "Failed";
		case ECompilerStatus::Aborted:    return "Aborted";
		default: TGE_UNREACHABLE("Unhandled ECompilerStatus value");
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::UpdateProgress(EBuildPhase phase, float phaseProgress,
                                    std::string const& statusMessage, std::string const& task)
{
	std::lock_guard<std::mutex> lock(m_progressMutex);
	m_progress.currentPhase = phase;
	m_progress.phaseProgress = phaseProgress;
	m_progress.statusMessage = statusMessage;
	m_progress.currentTask = task;

	float baseProgress{ 0.0f };

	switch (phase)
	{
		case EBuildPhase::CleaningPreviousBuild:  baseProgress = 0.0f; break;
		case EBuildPhase::CheckingDependencies:   baseProgress = 0.1f; break;
		case EBuildPhase::InstallingDependencies: baseProgress = 0.2f; break;
		case EBuildPhase::DownloadingSources:     baseProgress = 0.3f; break;
		case EBuildPhase::ConfiguringCompiler: baseProgress = 0.4f; break;
		case EBuildPhase::BuildingCompiler:    baseProgress = 0.5f; break;
		case EBuildPhase::InstallingCompiler:  baseProgress = 0.75f; break;
		case EBuildPhase::Completed:              baseProgress = 1.0f; break;
		case EBuildPhase::Failed:                 baseProgress = m_progress.overallProgress; break;
		default: TGE_UNREACHABLE("Unhandled EBuildPhase value");
	}

	if (phase != EBuildPhase::Failed)
	{
		m_progress.overallProgress = baseProgress + (phaseProgress * 0.1f);
	}

	if (m_progressCallback)
	{
		m_progressCallback(m_progress);
		RequestRedraw();
	}

	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Progress: {} (%.1f%%) - {} {}", statusMessage, m_progress.overallProgress * 100.0f, task.empty() ? "" : "- ", task);
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::ExecuteCommand(std::string_view command, bool)
{
	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Executing: {}", command);

	auto const result = CProcessExecutor::Execute(command);

	if (!result.success)
	{
		gLog.Warning(Tge::Logging::ETarget::Console, "Command failed (exit {}): {}", result.exitCode, result.output);
	}

	return result.success;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::ExecuteCommandWithOutput(std::string_view command)
{
	return ExecuteCommand(command, true);
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::CheckCommandExists(std::string_view command)
{
	return CProcessExecutor::Execute(std::format("which {} 2>/dev/null", command)).success;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::CleanupAfterBuild(SBuildSettings const& settings)
{
	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Starting post-build cleanup");

	for (auto const& entry : settings.compilerEntries)
	{
		if (!entry.keepSources && !entry.folderName.value.empty())
		{
			fs::path buildPath = fs::path(m_buildDir) / entry.folderName.value;

			if (fs::exists(buildPath))
			{
				gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Removing build directory: {}", buildPath.string());
				fs::remove_all(buildPath);
			}
		}

		if (!entry.keepSources && !entry.folderName.value.empty())
		{
			fs::path sourcePath = fs::path(m_sourceDir) / entry.folderName.value;

			if (fs::exists(sourcePath))
			{
				gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Removing source directory: {}", sourcePath.string());
				fs::remove_all(sourcePath);
			}
		}
	}

	gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Post-build cleanup completed");
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerBuilder::CheckLocalCommandExists(std::string_view command)
{
	fs::path localPath = fs::path(m_depsBinDir) / command;

	if (fs::exists(localPath) && fs::is_regular_file(localPath))
	{
		return true;
	}

	return CheckCommandExists(command);
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::BuildUsingCompilerUnits(std::vector<CCompilerUnit*> const& units)
{
	if (!units.empty())
	{
		BuildCompilerUnitsSequentially(units);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::Console, "CompilerBuilder: No compiler units to build");
		UpdateProgress(EBuildPhase::Failed, 0.0f, "No compilers configured");
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerBuilder::BuildCompilerUnitsSequentially(std::vector<CCompilerUnit*> const& units)
{
	size_t const totalUnits{ units.size() };

	for (size_t i = 0; i < units.size(); ++i)
	{
		units[i]->SetProgressCallback([this, i, totalUnits](std::string const& unitName, ECompilerStatus, float progress, std::string const&)
		{
			// Drive the overall bar: unit builds collectively span 30%→95%.
			// Each unit gets an equal slice; the current unit's fraction fills its slice.
			constexpr float OverallStart = 0.3f;
			constexpr float OverallEnd   = 0.95f;
			float const overallFraction{ (static_cast<float>(i) + progress) / static_cast<float>(totalUnits) };

			std::lock_guard<std::mutex> lock(m_progressMutex);
			m_progress.overallProgress = OverallStart + overallFraction * (OverallEnd - OverallStart);

			if (m_progressCallback)
			{
				m_progressCallback(m_progress);
			}

			gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Unit {} progress: {:.1f}%", unitName, progress * 100.0f);
		});
	}

	for (size_t i = 0; i < units.size() && !m_shouldStop; ++i)
	{
		CCompilerUnit* unit = units[i];

		// Register this build as active (prevents deletion during build)
		std::string installPath{ std::format("{}/{}", m_installPrefix, unit->GetFolderName()) };

		{
			std::lock_guard<std::mutex> lock(g_activeBuildsMutex);
			g_activeBuilds.push_back(installPath);
			gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Registered active build: {}", installPath);
		}

		gLog.Info(Tge::Logging::ETarget::Console, "CompilerBuilder: Starting build {}/{}: {}", i + 1, units.size(), unit->GetName());

		unit->StartBuildAsync();

		while (!unit->IsCompleted() && !m_shouldStop)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		// Unregister this build (now safe to delete)
		{
			std::lock_guard<std::mutex> lock(g_activeBuildsMutex);
			auto it = std::find(g_activeBuilds.begin(), g_activeBuilds.end(), installPath);

			if (it != g_activeBuilds.end())
			{
				g_activeBuilds.erase(it);
				gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Unregistered active build: {}", installPath);
			}
		}

		if (m_shouldStop)
		{
			unit->Stop();
			UpdateProgress(EBuildPhase::Failed, 0.0f, "Build aborted by user");
		}
	}

	if (!m_shouldStop)
	{
		UpdateProgress(EBuildPhase::Completed, 1.0f, "All compiler units completed");
	}
}
} // namespace Ctrn
