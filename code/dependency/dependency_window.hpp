#pragma once

#include "common/log_entry.hpp"
#include "dependency/dependency_unit.hpp"
#include "common/log_saver.hpp"
#include <tge/threading/serial_job_queue.hpp>
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <future>
#include <array>
#include <unordered_map>
struct ImVec2;

namespace Ctrn
{
class CDependencyWindow final : private Tge::SNoCopyNoMove
{
public:

	CDependencyWindow() = default;
	~CDependencyWindow() = default;

	// Global object lifecycle
	void Initialize();
	void Terminate();
	
	// Window rendering - returns false if user closed the window
	[[nodiscard]] bool Render();
	
	// Public methods for preset integration - called when presets are loaded/saved
	void LoadLocationSelectionsFromPresets();
	void SaveLocationSelectionsToPresets();
	
private:

	Tge::Threading::CSerialJobQueue<CDependencyUnit::SInstallationResult> m_installationQueue;
	std::vector<SLogEntry> m_logDisplay;
	
	std::unordered_map<std::string, bool> m_activeInstallations;
	std::unordered_map<std::string, std::string> m_installationStatus;
	
	std::unordered_map<std::string, std::string> m_perDependencyPaths;
	
	// File dialog state (non-blocking)
	std::unordered_map<std::string, std::future<std::string>> m_fileBrowseFutures;
	std::unordered_map<std::string, bool> m_fileBrowseActive;
	
	// Path processing system - unified handling for executables/folders/archives/URLs
	enum class EPathType
	{
		DirectExecutable,  // Points to a single executable file
		DirectoryWithExecutables,  // Directory containing one or more executables  
		Archive,  // Archive file (zip, tar.gz, etc.) 
		Url,  // Web URL for download
		Error  // Invalid path or processing error
	};
	
	struct SExecutableInfo final
	{
		std::string path;
		std::string version{ "unknown" };
		bool isWorking{ false };
	};
	
	struct SPathProcessingResult final
	{
		EPathType type{ EPathType::Error };
		std::vector<SExecutableInfo> executables;
		std::string identifier;
		std::string finalPath;
		std::string errorMessage;
	};
	
	struct SExecutableSelectionState final
	{
		bool showSelectionPopup{ false };
		std::vector<SExecutableInfo> availableExecutables;
		int selectedIndex{ 0 };
		std::string dependencyIdentifier;
	};

	struct SLocateDialog final
	{
		bool isOpen{ false };
		std::string identifier;
		std::string depName;
		std::array<char, 1024> pathBuffer{};
	};
	
	Tge::Threading::CSerialJobQueue<SPathProcessingResult> m_pathQueue;
	std::unordered_map<std::string, SExecutableSelectionState> m_executableSelectionStates;
	SLocateDialog m_locateDialog;
	
	// Preset integration (removed old Save to Preset dialog)
	std::vector<std::string> m_availablePresets;
	
	// Rendering methods
	void RenderTabContent();
	void RenderReadinessSection(
		std::vector<SAdvancedDependencyInfo*> const& missingGcc,
		std::vector<SAdvancedDependencyInfo*> const& missingClang);
	void RenderDepsTable(std::vector<SAdvancedDependencyInfo*> const& deps);
	void RenderLocateDialog();
	void RenderStatusIcon(EDependencyStatus status);
	void RenderLogArea();

	// Helper methods
	std::string GetInstallAdvice(std::vector<SAdvancedDependencyInfo*> const& deps) const;
	
	// Clean installation handling - no threading complexity
	void StartInstallation(std::string_view identifier, std::string_view url);
	void StartSingleInstallation(std::string_view identifier, std::string_view url);

	// Path processing system - unified handling
	void ProcessPath(std::string_view identifier, std::string_view path);
	void HandlePathProcessingResults();
	void RenderExecutableSelectionPopup(std::string_view identifier);
	SPathProcessingResult ProcessPathSync(std::string_view identifier, std::string_view path);

	// Shared UI components
	std::vector<SExecutableInfo> ScanDirectoryForExecutables(std::string_view directoryPath, std::string_view dependencyName);
	SExecutableInfo CreateExecutableInfo(std::string_view executablePath, std::string_view dependencyName);
	bool IsArchiveFile(std::string_view path);
	bool IsUrl(std::string_view path) const;
	
	// Path display helpers
	std::string GetDisplayPath(std::string_view absolutePath) const;
	std::string GetAbsolutePath(std::string_view displayPath) const;

	// File dialog methods
	void LaunchFileBrowser(std::string_view identifier, std::string_view depName);
	void CheckFileDialogResults();
	void UpdateInstallationProgress(); // Process completed jobs
	void CancelInstallation(std::string_view identifier);
	void SaveLog();
	
	// Preset integration
	std::string GetPresetDescription(std::string_view presetName);
	
	void RecalculateColumnWidths();

	// Dialog state persistence - internal only
	void SaveDialogState();
	void LoadDialogState();

private:
	std::string GetDialogStateFilePath() const;
	
	CLogSaver m_logSaver;

	// Window state variables
	float m_dialogWidth{ 1400.0f };
	float m_dialogHeight{ 1000.0f };
	float m_dialogPosX{ -1.0f };  // -1 means center on first use
	float m_dialogPosY{ -1.0f };

	// Cached column widths — recalculated only after dep list changes
	float m_nameColumnWidth{ 0.0f };
	float m_locationColumnWidth{ 0.0f };
	float m_versionColumnWidth{ 0.0f };
	float m_actionColumnWidth{ 0.0f };
	bool m_columnWidthsDirty{ true };
	
};

} // namespace Ctrn
