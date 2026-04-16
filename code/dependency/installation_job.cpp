#include "dependency/installation_job.hpp"
#include <format>
#include <random>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
CInstallationJob::CInstallationJob(CDependencyUnit const& unit, std::string_view jobId)
	: m_unit(unit)
	, m_jobId(jobId.empty() ? GenerateJobId(unit.GetIdentifier()) : std::string(jobId))
	, m_createdAt(std::chrono::steady_clock::now())
{
}

//////////////////////////////////////////////////////////////////////////
CDependencyUnit::SInstallationResult CInstallationJob::Execute() const
{
	// Pure function — operates only on immutable copies, no shared state, no synchronization needed
	return m_unit.Install(m_jobId);
}

//////////////////////////////////////////////////////////////////////////
std::string CInstallationJob::GenerateJobId(std::string_view identifier)
{
	auto now = std::chrono::steady_clock::now();
	auto duration = now.time_since_epoch();
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(1000, 9999);

	return std::format("{}_{}_{}", identifier, millis, dis(gen));
}
} // namespace Ctrn
