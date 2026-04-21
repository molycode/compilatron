#pragma once

#include "common/common.hpp"
#include "common/log_entry.hpp"
#include "common/log_saver.hpp"
#include "gui/status_icons.hpp"
#include <tge/logging/log_level.hpp>
#include "gui/version_manager.hpp"
#include "gui/version_selector_dialog.hpp"
#include "build/compiler_validation.hpp"
#include "build/compiler_unit.hpp"
#include "build/build_settings.hpp"
#include "gui/preset_manager.hpp"
#include "gui/compiler_tab.hpp"
#include "build/compiler.hpp"
#include "build/compiler_scan_result.hpp"
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <ctime>
#include <functional>
#include <atomic>
#include <expected>
#include <future>

namespace Ctrn
{

// UI Layout Constants
inline constexpr float g_elementWidth{ 300.0f };

class CCompilerBuilder;
struct SBuildProgress;

class CCompilerGUI final : private Tge::SNoCopyNoMove
{
public:

	CCompilerGUI();
	~CCompilerGUI();

	void Initialize();

	void Render();

	SBuildSettings const& GetSettings() const { return g_buildSettings; }

private:

	void RenderMainPanel();
	void RenderPresetControls();
	void RenderSettingsDialog();
	void RenderCompilerBrowserDialog();
	void RenderCommandDialog();
	std::string GenerateCompilerCommands(SCompilerTab const& tab) const;
	void RenderGccAdvancedDialog();
	void RenderClangAdvancedDialog();
	void RenderUnifiedCompilerSelector(std::string_view label, std::string_view currentCompiler,
		std::function<void(std::string const&)> onSelectionChanged, bool isPerTab = false,
		std::string_view compilerType = "");
	void StartActualRefresh(ECompilerKind kind);

	[[nodiscard]] bool AreRequiredDependenciesAvailable(ECompilerKind kind) const;
	std::string GetMissingDependenciesMessage() const;

	[[nodiscard]] static bool IsTabComplete(SCompilerTab const& tab);

	[[nodiscard]] bool IsCompilerValid(std::string_view compilerPath) const;
	[[nodiscard]] bool IsTabCompilerValid(SCompilerTab const& tab) const;
	std::string GetInvalidCompilerMessage(SCompilerTab const& tab) const;
	std::string GetActualCompilerForTab(SCompilerTab const& tab) const;
	[[nodiscard]] SCompiler GetActualSCompilerForTab(SCompilerTab const& tab) const;
	ECompilerValidationResult ValidateCompilerForBuild(SCompilerTab const& tab) const;
	[[nodiscard]] bool WouldInstallToSystemDirectory(SCompilerTab const& tab) const;
	[[nodiscard]] std::string FindLldForTab(SCompilerTab const& tab) const;
	[[nodiscard]] bool IsLldAvailableForTab(SCompilerTab const& tab) const;

	void StartBuild();
	void StartSingleCompilerBuild(SCompilerTab const& tab);
	void DeleteCompilerSources(SCompilerTab const& tab);
	void DeleteCompilerBuild(SCompilerTab const& tab);
	void RenderDeleteButtons(SCompilerTab const& tab, bool isBuilding);
	void RenderDeleteSourcesButton(SCompilerTab const& tab, bool isBuilding);
	void RenderDeleteBuildButton(SCompilerTab const& tab, bool isBuilding);
	static std::uintmax_t GetDirectorySize(std::string_view path);
	static std::string FormatSize(std::uintmax_t bytes);
	void StopBuild();
	void UpdateBuildStatus();
	std::string GetCpuName() const;
	std::string GenerateClangCMakeCommand();
	std::string GenerateClangCMakeCommandFor(std::string_view version);
	std::string GenerateGccConfigureCommand();
	std::string GenerateGccConfigureCommandFor(std::string_view version);
	std::string BuildTargetsString(SClangSettings const& clangConfig) const;
	std::string BuildProjectsString(SClangSettings const& clangConfig) const;

	void RenderProgressBarWithStatus(CCompilerUnit& unit);

	void RenderCompilerSelection();
	void RenderCompilerSelectionNew();
	[[nodiscard]] std::expected<void, std::string> OpenFolder(std::string_view path);
	std::string GetFolderNameFromCompilerName(std::string_view compilerName) const;
	std::string GetResolvedInstallPath() const;
	void AddCompilerEntry(std::string_view compilerType);

	void RenderCompilerNotification(CCompilerUnit& unit, ImVec2 const& buttonPos);

	void RegisterUnitLogListener(uint16_t tabId, CCompilerUnit& unit);
	void ClearUnitLog(uint16_t tabId);

	void RenderGlobalLogPanel();
	void CopyCompilerLogToClipboard(uint16_t tabId);
	void SaveCompilerLogToFile(uint16_t tabId);
	void SaveGlobalLogToFile();

	void RefreshPresetNames();
	void SelectPreset(std::string_view name);
	void ShowPresetSaveDialog(bool saveAs);
	void RenderPresetSaveDialog();

	void RenderRemoveCompilerDialog();
	void RenderAboutDialog();

	void SaveToPreset(std::string_view presetName, std::string_view description);
	void SaveActivePreset();
	void ShowDuplicateDialog();
	void ShowRenameDialog();

	SBuildSettings CreateBuildSettingsFromTabs() const;
	void CreateTabsFromBuildSettings(SBuildSettings const& settings);

	std::uintmax_t m_sourcesSize{ 0 };
	std::uintmax_t m_buildSize{ 0 };
	std::unique_ptr<CCompilerBuilder> m_compilerBuilder;
	time_t m_lastDisplayStringUpdate{ 0 };
	std::future<SCompilerScanResult> m_scanFuture;
	std::future<std::string> m_installDirBrowseFuture;
	CLogSaver m_logSaver;

	std::vector<SLogEntry> m_globalLog;

	std::unordered_map<uint16_t, std::vector<SLogEntry>> m_unitLogs;
	std::vector<SCompiler> m_scannedCompilers;
	std::vector<SCompiler> m_scannedAlreadyRegistered;
	std::vector<int> m_compilerSelectionStates;
	std::vector<std::string> m_availablePresets;
	std::vector<SCompilerTab> m_compilerTabs;
	std::unordered_map<uint16_t, std::unique_ptr<CCompilerUnit>> m_compilerUnits;
	uint16_t m_confirmTabId{ 0 };
	std::string m_currentPresetName;
	std::string m_tokenTestResult;
	std::string m_compilerToRemove;
	uint16_t m_commandDialogTabId{ 0 };
	std::string m_commandDialogTabName;
	std::string m_commandDialogText;
	uint16_t m_gccAdvancedTabId{ 0 };
	uint16_t m_clangAdvancedTabId{ 0 };
	std::string m_pendingScanDirectory;

	mutable std::mutex m_unitLogsMutex;
	std::unordered_map<ECompilerKind, std::string> m_cachedDisplayStrings;
	std::unordered_map<std::string, std::string> m_presetDescriptions;
	CVersionSelectorDialog m_versionSelectorDialog;
	CVersionManager m_versionManager;
	CPresetManager m_presetManager;

	std::atomic<float> m_buildProgress{ 0.0f };
	float m_logPanelHeight{ 200.0f };
	uint16_t m_nextCompilerTabId{ 1 };
	ImVec2 m_sourcesClickPos{ 0, 0 };
	ImVec2 m_buildClickPos{ 0, 0 };

	bool m_showSourcesConfirm{ false };
	bool m_showBuildConfirm{ false };
	bool m_sourcesDialogJustOpened{ false };
	bool m_buildDialogJustOpened{ false };
	bool m_showSettingsDialog{ false };
	bool m_tokenTestInProgress{ false };
	std::atomic<bool> m_isBuilding{ false };
	bool m_showPresetSaveDialog{ false };
	bool m_presetRenaming{ false };
	bool m_showDependencyWindow{ false };
	bool m_showAboutDialog{ false };
	bool m_aboutUrlOpenFailed{ false };
	bool m_showRemoveCompilerDialog{ false };
	bool m_showCommandDialog{ false };
	bool m_showGccAdvancedDialog{ false };
	bool m_showClangAdvancedDialog{ false };
	bool m_showCompilerBrowserDialog{ false };
	std::atomic<bool> m_isScanningDirectory{ false };
	std::atomic<bool> m_scanCompleted{ false };
	bool m_installDirBrowseActive{ false };

	char m_tokenInputBuffer[128]{};
	char m_presetNameBuffer[256]{};
	char m_presetDescriptionBuffer[512]{};
	char m_customDirectoryBuffer[1024]{};

	[[nodiscard]] SCompilerBuildConfig BuildConfigFromTab(SCompilerTab const& tab) const;
	void CreateUnitForTab(SCompilerTab const& tab);

	void RenderCompilerTab(SCompilerTab& tab);

	// Reduces duplication between GCC/Clang tab rendering
	void RenderCompilerProgressSection(SCompilerTab const& tab);
	void RenderCompilerLogSection(SCompilerTab const& tab);

	bool RenderTextFieldWithContextMenu(char const* label, char* buffer, size_t bufferSize);
	bool OpenUrlInBrowser(std::string_view url);
};

} // namespace Ctrn
