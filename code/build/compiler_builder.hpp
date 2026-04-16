#pragma once

#include "build/compiler_unit.hpp"
#include "build/build_settings.hpp"
#include "common/sleep_inhibitor.hpp"
#include "dependency/dependency.hpp"
#include "build/build_progress.hpp"
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <thread>
#include <atomic>
#include <mutex>

namespace Ctrn
{

struct SBuildSettings; // Forward declaration
enum class ECompilerStatus; // Forward declaration

class CCompilerBuilder final : private Tge::SNoCopyNoMove
{
public:

	using ProgressCallback = std::function<void(SBuildProgress const&)>;
	using CompletionCallback = std::function<void(bool success, std::string const& message)>;

	CCompilerBuilder() = default;
	~CCompilerBuilder();

	void Initialize();

	void StartBuild(std::vector<CCompilerUnit*> units,
	                SBuildSettings const& settings,
	                ProgressCallback progressCb = nullptr,
	                CompletionCallback completionCb = nullptr);

	void StopBuild();

	[[nodiscard]] bool IsBuilding() const;

	SBuildProgress GetProgress() const;

	// Returns only the most recent lines (for live GUI display)
	std::vector<std::string> GetOutputLines() const;

	void BuildUsingCompilerUnits(std::vector<CCompilerUnit*> const& units);
	void BuildCompilerUnitsSequentially(std::vector<CCompilerUnit*> const& units);

	static std::string StatusToString(ECompilerStatus status);

private:

	void BuildThreadFunc(SBuildSettings const& settings);

	bool CleanupPreviousBuild(SBuildSettings const& settings);
	bool CheckDependencies();
	bool BuildLocalDependencies();
	void CleanupAfterBuild(SBuildSettings const& settings);
	void UpdateProgress(EBuildPhase phase, float phaseProgress,
	                   std::string const& statusMessage, std::string const& task = "");

	bool ExecuteCommand(std::string_view command, bool captureOutput = true);
	bool ExecuteCommandWithOutput(std::string_view command);
	bool CheckCommandExists(std::string_view command);
	bool CheckLocalCommandExists(std::string_view command);
	bool DownloadAndBuildCMake();
	bool DownloadAndBuildNinja();
	bool DownloadAndBuildGit();
	bool DownloadAndBuildBison();
	bool DownloadAndBuildFlex();
	bool DownloadAndBuildAutoconf();
	bool DownloadAndBuildAutomake();
	bool DownloadAndBuildLibtool();
	bool DownloadAndBuildPkgConfig();
	bool DownloadAndBuildPerl();
	bool DownloadAndBuildGettext();
	bool DownloadAndBuildTexinfo();

	std::thread m_buildThread;
	std::atomic<bool> m_isBuilding{ false };
	std::atomic<bool> m_shouldStop{ false };

	CSleepInhibitor m_sleepInhibitor;

	std::vector<std::thread> m_compilerThreads;
	std::mutex m_compilerThreadsMutex;

	mutable std::mutex m_progressMutex;
	SBuildProgress m_progress;

	mutable std::mutex m_outputMutex;
	std::vector<std::string> m_outputLines;
	static constexpr size_t MAX_OUTPUT_LINES = 500;

	std::vector<CCompilerUnit*> m_units;

	ProgressCallback m_progressCallback;
	CompletionCallback m_completionCallback;

	std::vector<SDependency> m_dependencies;
	bool m_dependenciesChecked{ false };

	std::string m_buildDir;
	std::string m_sourceDir;
	std::string m_installPrefix;
	std::string m_depsDir;
	std::string m_depsBinDir;
	int m_numJobs{ std::max(1, static_cast<int>(std::thread::hardware_concurrency() / 2)) };

	void InitializeDependencies();
	std::string GetDistroId() const;
};

} // namespace Ctrn
