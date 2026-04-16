#include "build/compiler_unit.hpp"
#include "build/clang_unit.hpp"
#include "build/gcc_unit.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "dependency/dependency_manager.hpp"
#include "common/process_executor.hpp"
#include <tge/init/assert.hpp>
#include <tge/logging/log_system.hpp>
#include <format>
#include <chrono>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <ranges>
#include <thread>
#include <functional>
#include <charconv>
#include <unistd.h>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
static std::string ResolveGit()
{
	std::string const selected{ g_dependencyManager.GetSelectedPath("git") };
	return selected.empty() ? "git" : selected;
}

//////////////////////////////////////////////////////////////////////////
// Checks whether target is an existing Git tag in the given repo directory
static bool IsGitTag(std::string_view sourcesDir, std::string_view target)
{
	std::string const git{ ResolveGit() };
	std::string const cmd{ "cd \"" + std::string{sourcesDir} + "\" && " + git + " show-ref --verify --quiet refs/tags/" + std::string{target} + " 2>/dev/null" };
	return CProcessExecutor::Execute(cmd).success;
}

//////////////////////////////////////////////////////////////////////////
// Get compiler fingerprint for cache validation — includes size+mtime to detect binary changes
static std::string GetCompilerFingerprint(std::string const& compilerPath)
{
	std::string result{ "unknown" };

	if (!compilerPath.empty() && std::filesystem::exists(compilerPath))
	{
		std::error_code ec;
		auto fileSize = std::filesystem::file_size(compilerPath, ec);

		if (ec == std::error_code{})
		{
			auto fileTime = std::filesystem::last_write_time(compilerPath, ec);

			if (ec == std::error_code{})
			{
				auto timeT = std::chrono::system_clock::to_time_t(
					std::chrono::time_point_cast<std::chrono::system_clock::duration>(
						fileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
					)
				);

				std::string fingerprint{ compilerPath + "_" + std::to_string(timeT) + "_" + std::to_string(fileSize) };
				std::hash<std::string> hasher;
				result = std::to_string(hasher(fingerprint));
			}
		}
	}

	return result;
}

CCompilerUnit::CCompilerUnit(ECompilerKind type, std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig)
	: m_type(type)
	, m_name(std::move(displayName))
	, m_unitLog{m_name}
	, m_globalSettings(globalSettings)
	, m_buildConfig(buildConfig)
{
}

//////////////////////////////////////////////////////////////////////////
std::unique_ptr<CCompilerUnit> CCompilerUnit::Create(ECompilerKind type, std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig)
{
	std::unique_ptr<CCompilerUnit> result;

	if (type == ECompilerKind::Clang)
	{
		result = std::make_unique<CClangUnit>(std::move(displayName), globalSettings, buildConfig);
	}
	else if (type == ECompilerKind::Gcc)
	{
		result = std::make_unique<CGccUnit>(std::move(displayName), globalSettings, buildConfig);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
CCompilerUnit::~CCompilerUnit()
{
	if (m_buildThread.joinable())
	{
		m_shouldStop = true;  // Signal thread to stop
		m_buildThread.join();
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::ShowNotification(std::string_view message)
{
	m_notificationMessage = message;
	m_notificationTimer = 5.0f;
	m_showNotification = true;

	m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Notification: {}", message));
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::UpdateNotificationTimer(float deltaTime)
{
	if (m_showNotification)
	{
		m_notificationTimer -= deltaTime;

		if (m_notificationTimer <= 0.0f)
		{
			ClearNotification();
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::SetStatus(ECompilerStatus status, std::string_view task)
{
	{
		std::lock_guard lock(m_stateMutex);
		m_currentTask = task;
	}
	m_status = status;

	std::string statusName;
	switch (status)
	{
		case ECompilerStatus::NotStarted: statusName = "Not Started"; break;
		case ECompilerStatus::Cloning:    statusName = "Cloning"; break;
		case ECompilerStatus::Waiting:    statusName = "Waiting"; break;
		case ECompilerStatus::Building:   statusName = "Building"; break;
		case ECompilerStatus::Success:    statusName = "Success"; break;
		case ECompilerStatus::Failed:     statusName = "Failed"; break;
		case ECompilerStatus::Aborted:    statusName = "Aborted"; break;
		default: TGE_UNREACHABLE("Unhandled ECompilerStatus value");
	}

	if (task.empty())
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Status: " + statusName);
	}
	else
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Status: {} - {}", statusName, task));
	}

	gLog.Info(Tge::Logging::ETarget::File, "{}: Status change: {}{}", m_name, statusName, task.empty() ? "" : std::format(" ({})", task));
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::SetProgress(float progress)
{
	m_progress = progress;

	static float lastLoggedProgress = -1.0f;

	if (progress - lastLoggedProgress >= 0.1f || progress >= 1.0f)
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Progress: " + std::to_string(static_cast<int>(progress * 100)) + "%");
		lastLoggedProgress = progress;
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::SetFailureReason(std::string const& reason)
{
	std::lock_guard lock(m_stateMutex);
	m_failureReason = reason;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::ExecuteCommand(std::string_view command, bool captureOutput,
                                   std::function<void(std::string_view)> lineObserver)
{
	m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Executing: {}", command));
	gLog.Info(Tge::Logging::ETarget::File, "{}: Command: {}", m_name, command);

	CProcessExecutor::SProcessResult result;

	if (captureOutput)
	{
		// Streaming execution — callback receives raw chunks and handles \r/\n line logic
		std::string partialLine;

		auto streamCallback = [this, &partialLine, &lineObserver](std::string_view chunk) -> bool
		{
			size_t pos{ 0 };

			while (pos < chunk.size())
			{
				size_t const found{ chunk.find_first_of("\n\r", pos) };

				if (found == std::string::npos)
				{
					partialLine += chunk.substr(pos);
					break;
				}

				partialLine += chunk.substr(pos, found - pos);

				if (!partialLine.empty())
				{
					if (lineObserver)
					{
						lineObserver(partialLine);
					}

					// \r lines are intermediate progress updates (e.g. git clone) — skip logging
					if (chunk[found] == '\n')
					{
						m_unitLog.Info(Tge::Logging::ETarget::Console, partialLine);
					}
				}

				partialLine.clear();
				pos = found + 1;
			}

			return !m_shouldStop.load();
		};

		result = CProcessExecutor::Execute(command, {}, streamCallback);

		if (!partialLine.empty())
		{
			if (lineObserver)
			{
				lineObserver(partialLine);
			}

			m_unitLog.Info(Tge::Logging::ETarget::Console, partialLine);
		}
	}
	else
	{
		result = CProcessExecutor::Execute(command);
	}

	bool success{ false };

	if (result.errorMessage == "Process cancelled")
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Command execution stopped");
	}
	else
	{
		success = result.success;

		if (success && captureOutput)
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console, "Command completed successfully");
		}
		else if (!success)
		{
			m_unitLog.Error(Tge::Logging::ETarget::Console, captureOutput
				? "Command failed with exit code: " + std::to_string(result.exitCode)
				: std::format("Command failed (exit {}): {}", result.exitCode, result.output));
		}

		gLog.Info(Tge::Logging::ETarget::File, "{}: Command result: {}", m_name, result.exitCode);
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::ExecuteGitFetchWithRetry(std::string_view sourcesDir, std::string_view targetBranch)
{
	if (GetBuildConfig().updateSources)
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Found existing source directory, updating to latest version...");

		// Retry git fetch with exponential backoff for network resilience
		bool fetchSucceeded{ false };
		int maxRetries{ 3 };
		int delaySeconds{ 2 };

		for (int attempt = 1; attempt <= maxRetries && !m_shouldStop; attempt++)
		{
			std::string fetchCmd{ std::format("cd \"{}\" && {} fetch --depth 1 origin \"{}\" --progress 2>&1", sourcesDir, ResolveGit(), targetBranch) };

			if (ExecuteCommand(fetchCmd))
			{
				fetchSucceeded = true;
				break;
			}

			if (attempt < maxRetries && !m_shouldStop)
			{
				m_unitLog.Info(Tge::Logging::ETarget::Console, "Network error on attempt " + std::to_string(attempt) + "/" + std::to_string(maxRetries) +
				    " - retrying in " + std::to_string(delaySeconds) + " seconds...");

				// Interruptible sleep: wake every 100ms to check for stop signal
				int const totalMs{ delaySeconds * 1000 };
				int elapsed{ 0 };

				while (elapsed < totalMs && !m_shouldStop)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					elapsed += 100;
				}

				delaySeconds *= 2; // Exponential backoff: 2s, 4s, 8s...
			}
		}

		if (!fetchSucceeded && !m_shouldStop)
		{
			m_unitLog.Error(Tge::Logging::ETarget::Console, "Failed to fetch latest changes after " + std::to_string(maxRetries) + " attempts");
			ReportProgress(ECompilerStatus::Failed, 0.2f, "Source fetch failed");
			return false;
		}

		return true;
	}

	m_unitLog.Info(Tge::Logging::ETarget::Console, "Skipping source update (update disabled) - using existing sources...");
	return true;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerUnit::GetResolvedCompiler() const
{
	std::string result{ m_buildConfig.hostCompiler.empty()
		? GetEffectiveHostCompiler()
		: m_buildConfig.hostCompiler };
	return result;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::CheckAndCleanCompilerCache(std::string_view buildPath)
{
	std::string currentCompilerPath{ GetResolvedCompiler() };
	if (!currentCompilerPath.empty())
	{
		gLog.Info(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: Current compiler path: '{}'", currentCompilerPath);

		// Create robust fingerprint that detects binary changes AND installation directory changes
		std::string compilerFingerprint{ GetCompilerFingerprint(currentCompilerPath) };

		if (compilerFingerprint != "unknown")
		{
			// Include installation directory in fingerprint to detect installation path changes
			// This prevents autotools cache conflicts when changing where the compiler will be installed
			std::filesystem::path installPath = std::filesystem::path(g_dataDir) / GetGlobalSettings().installDirectory / GetFolderName();
			std::string installDir{ installPath.string() };

			std::hash<std::string> hasher;
			std::string combinedFingerprint{ compilerFingerprint + "_" + std::to_string(hasher(installDir)) };
			std::string currentCompiler{ combinedFingerprint };

			gLog.Info(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: Current compiler fingerprint: '{}', install dir: '{}'", compilerFingerprint, installDir);

			if (std::filesystem::exists(buildPath))
			{
				std::string compilerIdFile{ std::string{buildPath} + "/.compiler_id" };
				std::string storedCompiler{};
				bool hasStoredId{ false };

				if (std::filesystem::exists(compilerIdFile))
				{
					std::ifstream file(compilerIdFile);
					std::getline(file, storedCompiler);
					file.close();
					hasStoredId = true;
					gLog.Info(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: Stored compiler fingerprint: '{}'", storedCompiler);
				}
				else
				{
					gLog.Info(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: No .compiler_id file found");

					// If no stored ID but build directory exists, it's a previous build with unknown compiler
					// We should clean to avoid config.cache conflicts
					if (!std::filesystem::is_empty(buildPath))
					{
						gLog.Info(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: Build directory exists with no .compiler_id - cleaning for safety");

						std::error_code cleanEc;
						std::filesystem::remove_all(buildPath, cleanEc);

						if (!cleanEc)
						{
							std::filesystem::create_directories(buildPath, cleanEc);
						}

						if (cleanEc)
						{
							gLog.Warning(Tge::Logging::ETarget::Console, "Failed to clean build directory: {}", cleanEc.message());
						}
						else
						{
							m_unitLog.Info(Tge::Logging::ETarget::Console, "Cleaned existing build directory (no compiler ID found)");
						}
					}
				}

				if (hasStoredId && storedCompiler != currentCompiler)
				{
					if (!storedCompiler.empty())
					{
						m_unitLog.Info(Tge::Logging::ETarget::Console, "Compiler change detected");
						m_unitLog.Info(Tge::Logging::ETarget::Console, "Previous compiler fingerprint: " + storedCompiler);
						m_unitLog.Info(Tge::Logging::ETarget::Console, "Current compiler fingerprint: " + currentCompiler);
						m_unitLog.Info(Tge::Logging::ETarget::Console, "Current compiler path: " + currentCompilerPath);
						m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Removing entire build directory for clean rebuild: {}", buildPath));

						std::error_code changeEc;
						std::filesystem::remove_all(buildPath, changeEc);

						if (!changeEc)
						{
							std::filesystem::create_directories(buildPath, changeEc);
						}

						if (changeEc)
						{
							gLog.Warning(Tge::Logging::ETarget::Console, "Failed to clean build directory: {}", changeEc.message());
						}
						else
						{
							m_unitLog.Info(Tge::Logging::ETarget::Console, "Build directory cleaned successfully");
						}
					}
				}

				if (!hasStoredId || storedCompiler != currentCompiler)
				{
					std::ofstream idFile(compilerIdFile);

					if (idFile.is_open())
					{
						idFile << currentCompiler << std::endl;
					}
					else
					{
						gLog.Warning(Tge::Logging::ETarget::File, "Failed to store compiler identifier");
					}
				}
			}
			else
			{
				gLog.Info(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: Build directory doesn't exist, nothing to clean");
			}
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: Cannot fingerprint compiler");
		}
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "CCompilerUnit: CheckAndCleanCompilerCache: No compiler configured");
	}
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::PostDownloadHook(std::string_view)
{
	return true;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::ValidateSources()
{
	m_unitLog.Info(Tge::Logging::ETarget::Console, "Validating sources...");

	namespace fs = std::filesystem;
	std::string const sourcePath{ GetSourcePath() };
	bool valid{ fs::exists(sourcePath) };

	if (valid)
	{
		for (auto const& path : GetRequiredSourcePaths())
		{
			if (!fs::exists(fs::path(sourcePath) / path))
			{
				m_unitLog.Error(Tge::Logging::ETarget::Console, "Missing required component: " + path);
				valid = false;
			}
		}

		if (valid)
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console, "Sources validated successfully");
		}
	}
	else
	{
		m_unitLog.Error(Tge::Logging::ETarget::Console, "Source directory does not exist: " + sourcePath);
	}

	return valid;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::DownloadSources()
{
	SetStatus(ECompilerStatus::Cloning, "Updating sources");
	m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Updating sources for {}", GetName()));

	namespace fs = std::filesystem;
	std::string const sourcesDir{ GetSourcePath() };
	std::string const targetBranch{ GetName() };
	std::string const sourceUrl{ GetDefaultSourceUrl() };
	bool success{ false };

	if (fs::exists(sourcesDir))
	{
		if (ExecuteGitFetchWithRetry(sourcesDir, targetBranch))
		{
			bool const isTag{ IsGitTag(sourcesDir, targetBranch) };
			std::string checkoutCmd{};

			if (isTag)
			{
				m_unitLog.Info(Tge::Logging::ETarget::Console, "Target '" + targetBranch + "' is a tag, checking out tag...");
				checkoutCmd = "cd \"" + sourcesDir + "\" && " + ResolveGit() + " checkout \"tags/" + targetBranch + "\"";
			}
			else
			{
				m_unitLog.Info(Tge::Logging::ETarget::Console, "Target '" + targetBranch + "' is a branch, updating branch...");
				checkoutCmd = "cd \"" + sourcesDir + "\" && " + ResolveGit() + " checkout -B " + targetBranch + " origin/" + targetBranch;
			}

			if (ExecuteCommand(checkoutCmd))
			{
				m_unitLog.Info(Tge::Logging::ETarget::Console, isTag ? "Checked out tag: " + targetBranch : "Updated to latest: " + targetBranch);
				SetStatus(ECompilerStatus::Cloning, "Sources updated");
				success = true;
			}
			else
			{
				m_unitLog.Error(Tge::Logging::ETarget::Console, "Failed to update existing repository");
				ReportProgress(ECompilerStatus::Failed, 0.2f, "Source update failed");
			}
		}
	}
	else
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Cloning fresh repository for target: " + targetBranch);
		fs::create_directories(fs::path(sourcesDir).parent_path());

		std::string const shallowBranchCloneCmd{ ResolveGit() + " clone --branch \"" + targetBranch + "\" --depth 1 --progress \"" + sourceUrl + "\" \"" + sourcesDir + "\" 2>&1" };
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Attempting shallow clone for tag/recent branch: " + shallowBranchCloneCmd);

		bool cloned{ ExecuteCommand(shallowBranchCloneCmd) };

		if (!cloned)
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console, "Shallow clone with branch failed, trying shallow clone + fetch + checkout...");
			std::string const defaultCloneCmd{ ResolveGit() + " clone --depth 1 --progress \"" + sourceUrl + "\" \"" + sourcesDir + "\" 2>&1" };

			if (ExecuteCommand(defaultCloneCmd))
			{
				cloned = true;
				std::string const fetchCmd{ "cd \"" + sourcesDir + "\" && " + ResolveGit() + " fetch --depth 1 origin \"" + targetBranch + "\":\"" + targetBranch + "\" 2>&1" };
				m_unitLog.Info(Tge::Logging::ETarget::Console, "Fetching target branch: " + fetchCmd);

				if (ExecuteCommand(fetchCmd))
				{
					std::string const checkoutCmd{ "cd \"" + sourcesDir + "\" && " + ResolveGit() + " checkout \"" + targetBranch + "\" 2>&1" };
					m_unitLog.Info(Tge::Logging::ETarget::Console, "Checking out target branch: " + checkoutCmd);

					if (ExecuteCommand(checkoutCmd))
					{
						m_unitLog.Info(Tge::Logging::ETarget::Console, "Successfully fetched and checked out: " + targetBranch);
					}
					else
					{
						m_unitLog.Info(Tge::Logging::ETarget::Console, "Could not checkout specific branch, using default branch from shallow clone");
					}
				}
				else
				{
					m_unitLog.Info(Tge::Logging::ETarget::Console, "Could not fetch specific branch, using default branch from shallow clone");
				}
			}
			else
			{
				m_unitLog.Error(Tge::Logging::ETarget::Console, "Failed to clone sources");
				ReportProgress(ECompilerStatus::Failed, 0.2f, "Source download failed");
			}
		}

		if (cloned)
		{
			if (PostDownloadHook(sourcesDir))
			{
				m_unitLog.Info(Tge::Logging::ETarget::Console, "Sources downloaded successfully");
				SetStatus(ECompilerStatus::Cloning, "Sources downloaded");
				SetProgress(0.2f);
				success = true;
			}
		}
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::Initialize()
{
	GenerateCommands();
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::GenerateCommands()
{
	auto configureResult = GenerateConfigureCommand();

	if (configureResult)
	{
		m_configureCommand = std::move(configureResult.value());
	}
	else
	{
		m_configureCommand.clear();
		gLog.Warning(Tge::Logging::ETarget::File, "GenerateCommands: Failed to generate configure command for '{}': {}", m_name, configureResult.error());
	}

	m_buildCommand   = GenerateBuildCommand();
	m_installCommand = GenerateInstallCommand();
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::Configure()
{
	SetStatus(ECompilerStatus::Building, "Configuring");
	m_unitLog.Info(Tge::Logging::ETarget::Console, "Configuring build...");

	namespace fs = std::filesystem;
	fs::create_directories(GetBuildPath());
	CheckAndCleanCompilerCache(GetBuildPath());

	// Always remove CMakeCache.txt before configuring to prevent stale cmake flags
	// from persisting across builds when configuration options change.
	// Object files in subdirectories are unaffected — only cmake's variable cache is cleared.
	fs::path const cmakeCache{ std::string{GetBuildPath()} + "/CMakeCache.txt" };
	if (fs::exists(cmakeCache))
	{
		std::error_code ec;
		fs::remove(cmakeCache, ec);

		if (ec)
		{
			gLog.Warning(Tge::Logging::ETarget::Console, "Failed to remove CMakeCache.txt: {}", ec.message());
		}
		else
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console, "Cleared cmake cache");
		}
	}

	bool success{ false };

	if (!m_configureCommand.empty())
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Configure command: " + m_configureCommand);
		success = ExecuteCommand(m_configureCommand);

		if (success)
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console, "Configuration completed successfully");
			SetProgress(0.4f);
		}
		else if (!m_shouldStop)
		{
			m_unitLog.Error(Tge::Logging::ETarget::Console, "Configuration failed");
			SetFailureReason("Configure command failed");
			ReportProgress(ECompilerStatus::Failed, 0.4f, "Configuration failed");
		}
	}
	else
	{
		gLog.Warning(Tge::Logging::ETarget::File, "Configure: command is empty for '{}' — was Initialize() called?", m_name);
		SetFailureReason("Configure command could not be generated");
		ReportProgress(ECompilerStatus::Failed, 0.4f, "Configuration failed");
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
std::function<void(std::string_view)> CCompilerUnit::CreateBuildObserver()
{
	// Default: parse Ninja's [current/total] progress lines.
	// Derived classes override this for other build systems (e.g. CGccUnit for make).
	constexpr float CompileStart{ 0.6f };
	constexpr float CompileEnd{ 0.9f };

	return [this](std::string_view line)
	{
		if (!line.empty() && line[0] == '[')
		{
			size_t const slash{ line.find('/') };
			size_t const closeBracket{ line.find(']') };

			if (slash != std::string_view::npos && closeBracket != std::string_view::npos && slash < closeBracket)
			{
				std::string_view currentSv{ line.substr(1, slash - 1) };
				std::string_view totalSv{ line.substr(slash + 1, closeBracket - slash - 1) };

				while (!currentSv.empty() && currentSv[0] == ' ')
				{
					currentSv.remove_prefix(1);
				}

				while (!totalSv.empty() && totalSv[0] == ' ')
				{
					totalSv.remove_prefix(1);
				}

				int current{ 0 };
				int total{ 0 };
				auto const [p1, e1] = std::from_chars(currentSv.data(), currentSv.data() + currentSv.size(), current);
				auto const [p2, e2] = std::from_chars(totalSv.data(),   totalSv.data()   + totalSv.size(),   total);

				if (e1 == std::errc{} && e2 == std::errc{} && total > 0)
				{
					float const fraction{ static_cast<float>(current) / static_cast<float>(total) };
					float const progress{ CompileStart + fraction * (CompileEnd - CompileStart) };
					ReportProgress(ECompilerStatus::Building, progress,
					               std::format("Compiling {}/{} files", current, total));
				}
			}
		}
	};
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::Build()
{
	SetStatus(ECompilerStatus::Building, "Compiling");
	m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Building {}...", GetName()));

	m_unitLog.Info(Tge::Logging::ETarget::Console, "Build command: " + m_buildCommand);

	bool const success{ ExecuteCommand(m_buildCommand, true, CreateBuildObserver()) };

	if (success)
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Build completed successfully");
	}
	else if (!m_shouldStop)
	{
		SetFailureReason("Build command failed");
		ReportProgress(ECompilerStatus::Failed, 0.6f, "Build failed");
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::Install()
{
	namespace fs = std::filesystem;

	SetStatus(ECompilerStatus::Building, "Installing");
	m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Installing {}...", GetName()));

	// Remove previous installation immediately before installing — not at build start —
	// so an aborted or failed build leaves the existing compiler intact.
	fs::path const installPath{ fs::path{g_dataDir} / GetGlobalSettings().installDirectory / GetFolderName() };

	if (fs::exists(installPath))
	{
		// Safety check: don't delete the compiler currently being used to build
		std::string const hostCompiler{ GetResolvedCompiler() };
		bool hostIsHere{ false };

		if (!hostCompiler.empty())
		{
			std::error_code ec;
			fs::path const hostPath{ fs::canonical(hostCompiler, ec) };

			if (!ec)
			{
				hostIsHere = hostPath.string().find(installPath.string()) == 0;
			}
		}

		if (hostIsHere)
		{
			m_unitLog.Warning(Tge::Logging::ETarget::Console,
				std::format("Skipping removal of {} — host compiler is located there; installing over existing files", installPath.string()));
		}
		else
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console,
				std::format("Removing previous installation: {}", installPath.string()));
			std::error_code ec;
			fs::remove_all(installPath, ec);

			if (ec)
			{
				m_unitLog.Warning(Tge::Logging::ETarget::Console,
					std::format("Failed to remove previous installation: {}", ec.message()));
			}
		}
	}

	m_unitLog.Info(Tge::Logging::ETarget::Console, "Install command: " + m_installCommand);

	bool const success{ ExecuteCommand(m_installCommand) };

	if (success)
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "Installation completed successfully");
		SetStatus(ECompilerStatus::Success, "Ready");
		SetProgress(1.0f);
	}
	else if (!m_shouldStop)
	{
		m_unitLog.Error(Tge::Logging::ETarget::Console, "Installation failed");
		SetFailureReason("Install command failed");
		ReportProgress(ECompilerStatus::Failed, 0.9f, "Install failed");
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::Cleanup()
{
	m_unitLog.Info(Tge::Logging::ETarget::Console, "Cleaning up build artifacts...");

	namespace fs = std::filesystem;
	SCompilerBuildConfig const& config = GetBuildConfig();

	if (!config.keepDependencies)
	{
		if (fs::exists(config.buildDir))
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console, "Removing build directory: " + config.buildDir);
			fs::remove_all(config.buildDir);
		}
	}

	if (!config.keepSources)
	{
		if (fs::exists(config.sourcesDir))
		{
			m_unitLog.Info(Tge::Logging::ETarget::Console, "Removing sources directory: " + config.sourcesDir);
			fs::remove_all(config.sourcesDir);
		}
	}

	m_unitLog.Info(Tge::Logging::ETarget::Console, "Cleanup completed");
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::Stop()
{
	m_unitLog.Info(Tge::Logging::ETarget::Console, "Stopping build...");
	m_shouldStop = true;

	if (m_buildThread.joinable())
	{
		m_buildThread.join();
	}
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::IsRunning() const
{
	return m_status == ECompilerStatus::Cloning ||
	       m_status == ECompilerStatus::Building;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::StartBuildAsync()
{
	if (!IsRunning())
	{
		// Stop any previous thread and clean up
		if (m_buildThread.joinable())
		{
			m_buildThread.join();
		}

		m_shouldStop = false;
		m_status = ECompilerStatus::NotStarted;

		// Start build in own thread
		m_buildThread = std::thread([this]() {
			ExecuteBuildLifecycle();
		});

		m_unitLog.Info(Tge::Logging::ETarget::Console, "Started asynchronous build for " + m_name);
	}
	else
	{
		m_unitLog.Error(Tge::Logging::ETarget::Console, "Build already running for " + m_name);
	}
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerUnit::IsCompleted() const
{
	return m_status == ECompilerStatus::Success ||
	       m_status == ECompilerStatus::Failed ||
	       m_status == ECompilerStatus::Aborted;
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::ExecuteBuildLifecycle()
{
	m_unitLog.Info(Tge::Logging::ETarget::Console, "Starting build lifecycle for " + m_name);

	bool success{ false };

	if (!m_shouldStop)
	{
		ReportProgress(ECompilerStatus::Cloning, 0.2f, "Downloading/updating sources");

		if (DownloadSources())
		{
			if (!m_shouldStop)
			{
				ReportProgress(ECompilerStatus::NotStarted, 0.1f, "Validating sources");

				if (ValidateSources())
				{
					if (!m_shouldStop)
					{
						ReportProgress(ECompilerStatus::Building, 0.4f, "Configuring build");

						if (Configure())
						{
							if (!m_shouldStop)
							{
								ReportProgress(ECompilerStatus::Building, 0.6f, "Compiling");

								if (Build())
								{
									if (!m_shouldStop)
									{
										ReportProgress(ECompilerStatus::Building, 0.9f, "Installing");

										if (Install())
										{
											success = true;
											ReportProgress(ECompilerStatus::Success, 1.0f, "Ready");
											m_unitLog.Info(Tge::Logging::ETarget::Console, "Build completed successfully for " + m_name);
										}
										else
										{
											if (m_shouldStop)
											{
												SetStatus(ECompilerStatus::Aborted, "Stopped by user");
											}

											gLog.Warning(Tge::Logging::ETarget::Console, "Install step failed for {}", m_name);
										}
									}
									else
									{
										SetStatus(ECompilerStatus::Aborted, "Stopped by user");
									}
								}
								else
								{
									if (m_shouldStop)
									{
										SetStatus(ECompilerStatus::Aborted, "Stopped by user");
									}

									gLog.Warning(Tge::Logging::ETarget::Console, "Build step failed for {}", m_name);
								}
							}
							else
							{
								SetStatus(ECompilerStatus::Aborted, "Stopped by user");
							}
						}
						else
						{
							if (m_shouldStop)
							{
								SetStatus(ECompilerStatus::Aborted, "Stopped by user");
							}

							gLog.Warning(Tge::Logging::ETarget::Console, "Configure step failed for {}", m_name);
						}
					}
					else
					{
						SetStatus(ECompilerStatus::Aborted, "Stopped by user");
					}
				}
				else
				{
					gLog.Warning(Tge::Logging::ETarget::Console, "Source validation failed for {}", m_name);
					SetStatus(ECompilerStatus::Failed, "Source validation failed");
					SetFailureReason("Source validation failed after download");
				}
			}
			else
			{
				SetStatus(ECompilerStatus::Aborted, "Stopped by user");
			}
		}
		else
		{
			gLog.Warning(Tge::Logging::ETarget::Console, "Source download failed for {}", m_name);
			SetFailureReason("Failed to download/update sources");

			if (!IsCompleted())
			{
				SetStatus(ECompilerStatus::Failed, "Source download failed");
			}
		}
	}
	else
	{
		SetStatus(ECompilerStatus::Aborted, "Stopped by user");
	}

	TGE_ASSERT(IsCompleted(), "ExecuteBuildLifecycle exited without terminal status");

	if (!GetBuildConfig().keepSources || !GetBuildConfig().keepDependencies)
	{
		Cleanup();
	}

	ReportCompletion(success, m_failureReason);
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::ReportProgress(ECompilerStatus status, float progress, std::string const& task)
{
	SetStatus(status, task);
	SetProgress(progress);

	if (m_progressCallback)
	{
		m_progressCallback(m_name, status, progress, task);
	}
}

//////////////////////////////////////////////////////////////////////////
void CCompilerUnit::ReportCompletion(bool success, std::string_view errorMessage)
{
	if (success)
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Build completed: {}", m_name));
	}
	else if (m_status == ECompilerStatus::Aborted)
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, std::format("Build aborted: {}", m_name));
	}
	else
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, errorMessage.empty()
			? std::format("Build failed: {}", m_name)
			: std::format("Build failed: {} — {}", m_name, errorMessage));
	}

	if (m_completionCallback)
	{
		m_completionCallback(m_name, success, std::string{errorMessage});
	}
}

} // namespace Ctrn
