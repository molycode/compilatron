#include "dependency/dependency_window.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include <format>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::StartInstallation(std::string_view identifier, std::string_view url)
{
	// Prerequisites are handled in UI (grayed out buttons with tooltips)
	// This function is only called for dependencies without missing prerequisites
	StartSingleInstallation(identifier, url);
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::StartSingleInstallation(std::string_view identifier, std::string_view url)
{
	std::string const identStr{identifier};
	std::string const urlStr{url};

	m_activeInstallations[identStr] = true;
	m_installationStatus[identStr] = "Installing...";

	std::string const sourceType = IsUrl(urlStr) ? "URL" : "local file";
	gDepLog.Info(Tge::Logging::ETarget::Console, "Starting installation of {} from {}: {}", identStr, sourceType, urlStr);

	SAdvancedDependencyInfo* depInfo = g_dependencyManager.GetDependency(identStr);

	if (depInfo != nullptr)
	{
		SAdvancedDependencyInfo const depInfoCopy = *depInfo;

		m_installationQueue.Submit(identStr, [depInfoCopy, urlStr, identStr]() -> CDependencyUnit::SInstallationResult
		{
			CDependencyUnit::SInstallationResult result;
			result.unitName = depInfoCopy.name;
			result.jobId = identStr;
			std::string error;
			result.success = g_dependencyManager.Install(depInfoCopy, urlStr, error);
			result.message = result.success
				? std::format("Installed {} successfully", depInfoCopy.name)
				: (error.empty() ? "Unknown error" : error);
			RequestRedraw();
			return result;
		});
	}
	else
	{
		gDepLog.Error(Tge::Logging::ETarget::File, "Dependency not found: {}", identStr);
		m_activeInstallations[identStr] = false;
		m_installationStatus[identStr] = "Error: Dependency not found";
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::UpdateInstallationProgress()
{
	while (auto result = m_installationQueue.Poll())
	{
		std::string const& identifier = result->jobId;

		m_activeInstallations[identifier] = false;

		if (result->success)
		{
			m_installationStatus[identifier] = "Installation completed successfully";
			gDepLog.Info(Tge::Logging::ETarget::Console, "Successfully installed {} to ./dependencies/", result->unitName);
			g_dependencyManager.ScanAllDependencies();
			m_columnWidthsDirty = true;
		}
		else
		{
			m_installationStatus[identifier] = "Installation failed";
			gDepLog.Error(Tge::Logging::ETarget::Console, "Failed to install {}: {}", result->unitName, result->message);
		}
	}
}

//////////////////////////////////////////////////////////////////////////
void CDependencyWindow::CancelInstallation(std::string_view identifier)
{
	std::string const identStr{identifier};
	m_activeInstallations[identStr] = false;
	m_installationStatus[identStr] = "Installation cancelled";
	m_installationQueue.Cancel(identStr);
	gDepLog.Info(Tge::Logging::ETarget::Console, "Installation cancelled for {}", identStr);
}

} // namespace Ctrn
