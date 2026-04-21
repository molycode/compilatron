#include "dependency/dependency_window.hpp"
#include "dependency/dependency_manager.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "common/process_executor.hpp"
#include <format>
#include <chrono>
#include <filesystem>
#include <future>
#include <algorithm>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::ProcessPath(std::string_view identifier, std::string_view path)
{
	if (!path.empty())
	{
		std::string const identStr{identifier};
		std::string const pathStr{path};

		m_pathQueue.Submit(identStr, [this, identStr, pathStr]() -> SPathProcessingResult
		{
			return ProcessPathSync(identStr, pathStr);
		});

		gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Started path processing for {}: {}", identStr, pathStr);
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::HandlePathProcessingResults()
{
	while (auto polled = m_pathQueue.Poll())
	{
		SPathProcessingResult const& result = *polled;

		switch (result.type)
		{
			case EPathType::DirectExecutable:
			{
				std::string const& version = !result.executables.empty() ? result.executables[0].version : "detected";

				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Direct executable found for {}: {} (v{})", result.identifier, result.finalPath, version);

				bool const success = g_dependencyManager.RegisterAdditionalVersion(result.identifier, result.finalPath, version);

				if (success)
				{
					m_columnWidthsDirty = true;
					SaveActivePreset();
					gDepLog.Info(Tge::Logging::ETarget::Console, "Registered {} executable: {} (v{})", result.identifier, result.finalPath, version);
				}
				else
				{
					gDepLog.Warning(Tge::Logging::ETarget::Console, "Failed to register {} executable: {}", result.identifier, result.finalPath);
				}
				break;
			}

			case EPathType::DirectoryWithExecutables:
			{
				if (result.executables.size() == 1)
				{
					SExecutableInfo const& exe = result.executables[0];
					gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Single executable found for {}: {}", result.identifier, exe.path);

					bool const success = g_dependencyManager.RegisterAdditionalVersion(result.identifier, exe.path, exe.version);

					if (success)
					{
						m_columnWidthsDirty = true;
						SaveActivePreset();
						gDepLog.Info(Tge::Logging::ETarget::Console, "Registered {} executable: {} (v{})", result.identifier, exe.path, exe.version);
					}
					else
					{
						gDepLog.Warning(Tge::Logging::ETarget::Console, "Failed to register {} executable: {} (v{})", result.identifier, exe.path, exe.version);
					}
				}
				else if (result.executables.size() > 1)
				{
					gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Multiple executables found for {}: {} options", result.identifier, result.executables.size());

					SExecutableSelectionState& selectionState = m_executableSelectionStates[result.identifier];
					selectionState.showSelectionPopup = true;
					selectionState.availableExecutables = result.executables;
					selectionState.selectedIndex = 0;
					selectionState.dependencyIdentifier = result.identifier;

					gDepLog.Info(Tge::Logging::ETarget::Console, "Found {} {} executables - please select one", result.executables.size(), result.identifier);
				}
				else
				{
					gDepLog.Info(Tge::Logging::ETarget::Console, "No {} executables found in directory: {}", result.identifier, result.finalPath);
				}
				break;
			}

			case EPathType::Archive:
			case EPathType::Url:
			{
				gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Archive/URL processing for {}: {}", result.identifier, result.finalPath);

				m_perDependencyPaths[result.identifier] = result.finalPath;
				StartInstallation(result.identifier, result.finalPath);
				break;
			}

			case EPathType::Error:
			{
				gDepLog.Warning(Tge::Logging::ETarget::Console, "Error processing {} path: {}", result.identifier, result.errorMessage);
				break;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
CDependencyWindow::SPathProcessingResult CDependencyWindow::ProcessPathSync(std::string_view identifier, std::string_view path)
{
	std::string const identStr{identifier};
	std::string const pathStr{path};

	SPathProcessingResult result;
	result.identifier = identStr;
	result.finalPath = pathStr;

	if (IsUrl(pathStr))
	{
		result.type = EPathType::Url;
	}
	else if (!std::filesystem::exists(pathStr))
	{
		result.type = EPathType::Error;
		result.errorMessage = std::format("Path does not exist: {}", pathStr);
	}
	else if (std::filesystem::is_regular_file(pathStr) && IsArchiveFile(pathStr))
	{
		std::filesystem::path const archivePath{pathStr};
		std::string stem{ archivePath.stem().string() };

		// Strip second extension for double-suffixed archives (.tar.gz, .tar.xz, .tar.bz2)
		if (archivePath.extension() == ".gz" || archivePath.extension() == ".xz" || archivePath.extension() == ".bz2")
		{
			stem = std::filesystem::path{stem}.stem().string();
		}

		std::string lowerStem{stem};
		std::string lowerId{identStr};
		std::transform(lowerStem.begin(), lowerStem.end(), lowerStem.begin(), ::tolower);
		std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);

		// Also accept archives named after the system package (e.g. texinfo-7.3 for makeinfo)
		// or the base package name without the -dev/-devel suffix (e.g. binutils-2.46 for binutils-dev)
		auto const* dep{ g_dependencyManager.GetDependency(identStr) };
		std::string lowerPkg;
		std::string lowerPkgBase;

		if (dep != nullptr && !dep->systemPackage.empty())
		{
			lowerPkg = dep->systemPackage;
			std::transform(lowerPkg.begin(), lowerPkg.end(), lowerPkg.begin(), ::tolower);
			lowerPkgBase = lowerPkg;

			for (std::string_view const suffix : { "-dev", "-devel" })
			{
				if (lowerPkgBase.ends_with(suffix))
				{
					lowerPkgBase = lowerPkgBase.substr(0, lowerPkgBase.size() - suffix.size());
					break;
				}
			}
		}

		bool const stemMatches{ lowerStem.find(lowerId) != std::string::npos
			|| (!lowerPkg.empty() && lowerStem.find(lowerPkg) != std::string::npos)
			|| (!lowerPkgBase.empty() && lowerPkgBase != lowerPkg && lowerStem.find(lowerPkgBase) != std::string::npos) };

		if (!stemMatches)
		{
			result.type = EPathType::Error;
			result.errorMessage = std::format("'{}' doesn't look like a {} archive",
				archivePath.filename().string(), identStr);
		}
		else
		{
			result.type = EPathType::Archive;
		}
	}
	else if (std::filesystem::is_regular_file(pathStr))
	{
		SExecutableInfo execInfo{ CreateExecutableInfo(pathStr, identStr) };

		if (execInfo.isWorking)
		{
			result.type = EPathType::DirectExecutable;
			result.executables.push_back(execInfo);
		}
		else
		{
			result.type = EPathType::Error;
			result.errorMessage = std::format("File is not a working {} executable", identStr);
		}
	}
	else if (std::filesystem::is_directory(pathStr))
	{
		std::vector<SExecutableInfo> executables{ ScanDirectoryForExecutables(pathStr, identStr) };
		result.type = EPathType::DirectoryWithExecutables;
		result.executables = executables;
	}
	else
	{
		result.type = EPathType::Error;
		result.errorMessage = "Unknown file type or unsupported path";
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::LaunchFileBrowser(std::string_view identifier, std::string_view depName)
{
	std::string const identStr{identifier};
	std::string const depNameStr{depName};

	m_fileBrowseActive[identStr] = true;

	std::string startLocation{ g_stateManager.GetLastBrowseDir() };

	if (startLocation.empty() || !std::filesystem::exists(startLocation))
	{
		startLocation = g_dataDir;
	}

	gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Launching file browser for {} at location: {}", identStr, startLocation);

	m_fileBrowseFutures[identStr] = std::async(std::launch::async, [identStr, depNameStr, startLocation]() -> std::string
	{
		std::string const cmd = std::format(
			"zenity --file-selection --title=\"Select {}\" --filename=\"{}/\" 2>/dev/null",
			depNameStr, startLocation);

		auto const result = CProcessExecutor::Execute(cmd);

		if (!result.success && !result.output.empty())
		{
			gDepLog.Warning(Tge::Logging::ETarget::File,
				"DependencyTab: zenity file picker failed for {}: {}", identStr, result.output);
		}

		RequestRedraw();
		return result.output;
	});
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::CheckFileDialogResults()
{
	// Check all active file dialogs for completion
	std::vector<std::string> completedDialogs;

	for (auto const& [identifier, future] : m_fileBrowseFutures)
	{
		if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
		{
			completedDialogs.push_back(identifier);
		}
	}

	// Process completed dialogs
	for (auto const& identifier : completedDialogs)
	{
		std::string selectedFile{ m_fileBrowseFutures[identifier].get() };

		// Trim trailing whitespace/newline from zenity stdout
		while (!selectedFile.empty() && std::isspace(static_cast<unsigned char>(selectedFile.back())))
		{
			selectedFile.pop_back();
		}

		if (!selectedFile.empty())
		{
			g_stateManager.SetLastBrowseDir(selectedFile);

			std::string displayPath{ GetDisplayPath(selectedFile) };
			size_t const len{ std::min(displayPath.length(), size_t(1023)) };

			if (m_locateDialog.identifier == identifier)
			{
				displayPath.copy(m_locateDialog.pathBuffer.data(), len);
				m_locateDialog.pathBuffer[len] = '\0';
			}

			m_perDependencyPaths[identifier] = selectedFile;
		}

		m_fileBrowseFutures.erase(identifier);
		m_fileBrowseActive[identifier] = false;
	}
}

//////////////////////////////////////////////////////////////////////////
std::vector<CDependencyWindow::SExecutableInfo> CDependencyWindow::ScanDirectoryForExecutables(std::string_view directoryPath, std::string_view dependencyName)
{
	std::string const dirStr{directoryPath};
	std::string const depStr{dependencyName};

	std::vector<SExecutableInfo> executables;

	// Recursive directory iteration
	std::error_code ec;
	for (auto const& entry : std::filesystem::recursive_directory_iterator(dirStr, ec))
	{
		if (!ec && entry.is_regular_file())
		{
			std::string filename{ entry.path().filename().string() };
			std::string filepath{ entry.path().string() };

			// Check if filename matches dependency name patterns
			std::vector<std::string> patterns = {
				depStr,                    // exact match (e.g., "ninja")
				depStr + "-build",         // with suffix (e.g., "ninja-build")
				depStr + ".exe",           // windows executable
				filename.starts_with(depStr) ? filename : "" // starts with dependency name
			};

			bool matched{ false };
			for (std::string const& pattern : patterns)
			{
				if (!matched && !pattern.empty() && filename == pattern)
				{
					SExecutableInfo execInfo{ CreateExecutableInfo(filepath, depStr) };

					if (execInfo.isWorking)
					{
						executables.push_back(execInfo);
					}

					matched = true;
				}
			}
		}
	}

	if (ec)
	{
		gDepLog.Info(Tge::Logging::ETarget::File, "DependencyTab: Exception scanning directory {}: {}", dirStr, ec.message());
	}

	return executables;
}

//////////////////////////////////////////////////////////////////////////
CDependencyWindow::SExecutableInfo CDependencyWindow::CreateExecutableInfo(std::string_view executablePath, std::string_view dependencyName)
{
	std::string const pathStr{executablePath};

	SExecutableInfo info;
	info.path = pathStr;

	std::error_code ec;
	std::filesystem::perms const perms{ std::filesystem::status(pathStr, ec).permissions() };
	bool const isExecutable{ !ec &&
		((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none ||
		 (perms & std::filesystem::perms::group_exec) != std::filesystem::perms::none ||
		 (perms & std::filesystem::perms::others_exec) != std::filesystem::perms::none) };

	if (isExecutable)
	{
		auto const* dep{ g_dependencyManager.GetDependency(dependencyName) };
		std::string const versionCommand{ dep != nullptr ? dep->versionCommand : "--version" };
		info.version = g_dependencyManager.DetectVersion(pathStr, versionCommand);
		info.isWorking = !info.version.empty() && info.version != "unknown";
	}

	return info;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyWindow::IsArchiveFile(std::string_view path)
{
	std::string lowercase{path};
	std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(), ::tolower);

	return lowercase.ends_with(".tar.gz") ||
	       lowercase.ends_with(".tar.xz") ||
	       lowercase.ends_with(".tar.bz2") ||
	       lowercase.ends_with(".zip") ||
	       lowercase.ends_with(".tgz") ||
	       lowercase.ends_with(".tbz2");
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyWindow::IsUrl(std::string_view path) const
{
	return path.starts_with("http://") ||
	       path.starts_with("https://") ||
	       path.starts_with("ftp://") ||
	       path.starts_with("ftps://");
}
} // namespace Ctrn
