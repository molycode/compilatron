#include "build/compiler_builder.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "common/process_executor.hpp"
#include "dependency/dependency_manager.hpp"
#include <tge/init/assert.hpp>
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

	fs::path const dataPath{ g_dataDir };
	m_buildDir = (dataPath / "build_compilers").string();
	m_sourceDir = (dataPath / "sources").string();
	m_installPrefix = (dataPath / "compilers").string();

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
			gLog.Warning(Tge::Logging::ETarget::Listeners, "Failed to clean some previous build artifacts, continuing...");
		}
	}

	if (!m_shouldStop)
	{
		gLog.Info(Tge::Logging::ETarget::File, "Phase 1: Checking build dependencies");
		UpdateProgress(EBuildPhase::CheckingDependencies, 0.0f, "Checking build dependencies...");

		// Trust the manager's current state — it mirrors the Dependencies tab and preserves
		// user-registered custom locations. Do NOT re-scan here: ScanDependency rebuilds
		// foundLocations from standard bin dirs only, which would wipe a custom path the user set.
		if (!g_dependencyManager.AreAllRequiredDependenciesAvailable())
		{
			gLog.Info(Tge::Logging::ETarget::File, "CompilerBuilder: Phase 2: Dependencies missing, provisioning locally");
			UpdateProgress(EBuildPhase::InstallingDependencies, 0.0f, "Installing missing dependencies locally...");

			if (!ProvisionMissingDependencies())
			{
				gLog.Error(Tge::Logging::ETarget::Listeners, "Failed to provision required dependencies");
				UpdateProgress(EBuildPhase::Failed, 0.0f, "Build failed: Failed to provision required dependencies");
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
bool CCompilerBuilder::ProvisionMissingDependencies()
{
	// Delegate provisioning to CDependencyManager (the single owner of download/build-from-source
	// for build tools). The manager must already be scanned and have its required-flags set by the
	// GUI before the build starts; BuildThreadFunc re-scans immediately before calling this.
	std::vector<SAdvancedDependencyInfo*> const missing{ g_dependencyManager.GetMissingRequiredDependencies() };

	std::vector<std::string> identifiers;
	identifiers.reserve(missing.size());

	for (auto const* dep : missing)
	{
		identifiers.emplace_back(dep->identifier);
	}

	std::vector<std::string> const order{ g_dependencyManager.GetInstallationOrder(identifiers) };
	bool success{ true };

	for (auto const& identifier : order)
	{
		if (!m_shouldStop)
		{
			UpdateProgress(EBuildPhase::InstallingDependencies, 0.0f, "Installing " + identifier + "...");

			if (!g_dependencyManager.InstallDependency(identifier))
			{
				gLog.Error(Tge::Logging::ETarget::Listeners, "Failed to install dependency: {}", identifier);
				success = false;
			}
		}
	}

	g_dependencyManager.UpdateEnvironmentPaths();

	return success;
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
void CCompilerBuilder::BuildUsingCompilerUnits(std::vector<CCompilerUnit*> const& units)
{
	if (!units.empty())
	{
		BuildCompilerUnitsSequentially(units);
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::Listeners, "CompilerBuilder: No compiler units to build");
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

		gLog.Info(Tge::Logging::ETarget::Listeners, "CompilerBuilder: Starting build {}/{}: {}", i + 1, units.size(), unit->GetName());

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
