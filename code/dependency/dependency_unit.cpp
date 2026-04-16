#include "dependency/dependency_unit.hpp"
#include "common/loggers.hpp"
#include <filesystem>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
CDependencyUnit::CDependencyUnit(SAdvancedDependencyInfo const& depInfo)
	: m_info(depInfo) // Immutable copy for thread safety
{
}

//////////////////////////////////////////////////////////////////////////
CDependencyUnit::SInstallationResult CDependencyUnit::Install(std::string_view jobId) const
{
	SInstallationResult result;
	result.unitName = m_info.name;
	result.jobId = jobId;
	
	result = DownloadAndBuildLocal(jobId);
	
	return result;
}

//////////////////////////////////////////////////////////////////////////
CDependencyUnit::SInstallationResult CDependencyUnit::DownloadAndBuildLocal(std::string_view jobId) const
{
	// For now, if installFunc is available, use it directly (it handles everything)
	if (m_info.installFunc)
	{
		SInstallationResult result;
		result.unitName = m_info.name;
		result.jobId = jobId;
		
		LogProgress(jobId, "Using existing installation method for " + m_info.name);
		bool success{ m_info.installFunc() };
		result.success = success;
		result.message = success ? "Successfully installed " + m_info.name : "Installation failed for " + m_info.name;
		return result;
	}
	
	// No generic download/extract path - all dependencies should use installFunc
	SInstallationResult result;
	result.unitName = m_info.name;
	result.jobId = jobId;
	result.success = false;
	result.message = "No installFunc configured for " + m_info.name;
	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyUnit::GetExecutableDirectory() const
{
	return std::filesystem::current_path().string();
}

//////////////////////////////////////////////////////////////////////////
void CDependencyUnit::LogProgress(std::string_view jobId, std::string_view message) const
{
	gDepLog.Info(Tge::Logging::ETarget::File, "[{}] {}", jobId, message);
}
} // namespace Ctrn
