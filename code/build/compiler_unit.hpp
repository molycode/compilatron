#pragma once

#include "build/build_settings.hpp"
#include "build/compiler_kind.hpp"
#include <tge/logging/log.hpp>
#include <cstdint>
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <expected>

namespace Ctrn
{
struct SCompilerBuildConfig final
{
	int numJobs{ 0 };
	bool keepDependencies{ true };
	bool keepSources{ true };
	bool updateSources{ true };       // Whether to fetch latest changes before building
	std::string folderName;      // Installation folder name (e.g., "gcc_15", "clang_21")
	std::string buildDir;        // Full build directory path
	std::string sourcesDir;      // Full sources directory path
	std::string dependenciesDir; // Dependencies bin directory for PATH
	std::string hostCompiler;    // Override compiler for this specific build (empty = use global)
	uint16_t id{ 0 };           // Tab ID
};

enum class ECompilerStatus
{
	NotStarted,
	Cloning,
	Waiting,
	Building,
	Success,
	Failed,
	Aborted
};

// Base class for compiler build units; derived classes provide compiler-specific hooks
class CCompilerUnit : private Tge::SNoCopyNoMove
{
public:

	CCompilerUnit(ECompilerKind type, std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig);
	virtual ~CCompilerUnit();

	[[nodiscard]] static std::unique_ptr<CCompilerUnit> Create(ECompilerKind type, std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig);

	// Post-construction initialization — generates command strings from current settings
	void Initialize();

	// Regenerates and stores all three command strings from current settings.
	// Call after changing type-specific settings (SetClangSettings / SetGccSettings).
	void GenerateCommands();

	std::string_view GetConfigureCommand() const { return m_configureCommand; }
	std::string_view GetBuildCommand()     const { return m_buildCommand; }
	std::string_view GetInstallCommand()   const { return m_installCommand; }

	// Core lifecycle (implemented in base using the hook interface below)
	[[nodiscard]] bool ValidateSources();
	[[nodiscard]] bool DownloadSources();
	[[nodiscard]] bool Configure();
	[[nodiscard]] bool Build();
	[[nodiscard]] bool Install();
	void Cleanup();

	ECompilerStatus GetStatus() const { return m_status.load(); }
	float GetProgress() const { return m_progress.load(); }
	std::string GetCurrentTask() const { std::lock_guard lock(m_stateMutex); return m_currentTask; }
	std::string GetFailureReason() const { std::lock_guard lock(m_stateMutex); return m_failureReason; }

	ECompilerKind GetKind() const { return m_type; }
	std::string_view GetName() const { return m_name; }
	void SetName(std::string_view name) { m_name = name; m_unitLog.SetName(name); }
	std::string_view GetFolderName() const { return m_buildConfig.folderName; }

	[[nodiscard]] bool HasNotification() const { return m_showNotification; }
	std::string_view GetNotificationMessage() const { return m_notificationMessage; }
	float GetNotificationTimer() const { return m_notificationTimer; }
	void ShowNotification(std::string_view message);
	void UpdateNotificationTimer(float deltaTime);
	void ClearNotification() { m_showNotification = false; }

	void StartBuildAsync();
	void RequestStop() { m_shouldStop = true; }
	void Stop();
	[[nodiscard]] bool IsRunning() const;
	[[nodiscard]] bool IsCompleted() const;

	// External systems can subscribe to progress and completion events
	using ProgressCallback = std::function<void(std::string const& unitName, ECompilerStatus status, float progress, std::string const& task)>;
	using CompletionCallback = std::function<void(std::string const& unitName, bool success, std::string const& errorMessage)>;

	void SetProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }
	void SetCompletionCallback(CompletionCallback callback) { m_completionCallback = callback; }

	void RegisterLogListener(void* key, Tge::Logging::LogMessageCallback callback) { m_unitLog.RegisterListener(key, callback, Tge::Logging::EMessageFormat::Raw); }
	void UnregisterLogListener(void* key) { m_unitLog.UnregisterListener(key); }

	void UpdateBuildConfig(SCompilerBuildConfig const& config) { m_buildConfig = config; }

	SBuildSettings const& GetGlobalSettings() const { return m_globalSettings; }
	SCompilerBuildConfig const& GetBuildConfig() const { return m_buildConfig; }

protected:

	std::string GetResolvedCompiler() const;
	void CheckAndCleanCompilerCache(std::string_view buildPath);

	void SetStatus(ECompilerStatus status, std::string_view task = "");
	void SetProgress(float progress);
	void SetFailureReason(std::string const& reason);

	bool ExecuteCommand(std::string_view command, bool captureOutput = true,
	                    std::function<void(std::string_view)> lineObserver = nullptr);

	bool ExecuteGitFetchWithRetry(std::string_view sourcesDir, std::string_view targetBranch);

private:

	ECompilerKind m_type;
	std::string m_name;
	std::string m_configureCommand;
	std::string m_buildCommand;
	std::string m_installCommand;

protected:

	Tge::Logging::CLog m_unitLog;

	std::atomic<float> m_progress{ 0.0f };
	std::string m_currentTask;
	std::string m_failureReason;
	mutable std::mutex m_stateMutex;

	bool m_showNotification{ false };
	std::string m_notificationMessage;
	float m_notificationTimer{ 0.0f };

	SBuildSettings const& m_globalSettings;

	ProgressCallback m_progressCallback;
	CompletionCallback m_completionCallback;

protected:

	SCompilerBuildConfig m_buildConfig;
	std::atomic<ECompilerStatus> m_status{ ECompilerStatus::NotStarted };

	std::thread m_buildThread;
	std::atomic<bool> m_shouldStop{ false };

	void ExecuteBuildLifecycle();

	// Notifies external subscribers of progress/completion events
	void ReportProgress(ECompilerStatus status, float progress, std::string const& task);
	void ReportCompletion(bool success, std::string_view errorMessage = "");

	// Compiler-specific hook interface — derived classes provide these
	virtual std::function<void(std::string_view)> CreateBuildObserver();
	virtual std::string              GetSourcePath()          const = 0;
	virtual std::string              GetBuildPath()           const = 0;
	virtual std::string              GetInstallPath()         const = 0;
	virtual std::string              GetDefaultSourceUrl()    const = 0;
	virtual std::vector<std::string> GetRequiredSourcePaths() const = 0;
	[[nodiscard]] virtual std::expected<std::string, std::string> GenerateConfigureCommand() const = 0;
	virtual std::string              GenerateBuildCommand()   const = 0;
	virtual std::string              GenerateInstallCommand() const = 0;
	virtual bool                     PostDownloadHook(std::string_view sourcesDir);
	[[nodiscard]] virtual bool       PreConfigureHook();
};
} // namespace Ctrn
