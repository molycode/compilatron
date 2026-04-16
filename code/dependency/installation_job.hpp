#pragma once

#include "dependency/dependency_unit.hpp"
#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <chrono>

namespace Ctrn
{
class CInstallationJob final : private Tge::SNoCopy
{
public:

	explicit CInstallationJob(CDependencyUnit const& unit, std::string_view jobId);
	~CInstallationJob() = default;

	CInstallationJob(CInstallationJob&&) noexcept = default;
	CInstallationJob& operator=(CInstallationJob&&) noexcept = default;

	// Pure execution method — no side effects, thread-safe
	[[nodiscard]] CDependencyUnit::SInstallationResult Execute() const;

	std::string GetJobId() const { return m_jobId; }
	std::string GetUnitName() const { return m_unit.GetName(); }
	std::string GetUnitIdentifier() const { return m_unit.GetIdentifier(); }
	bool IsRequired() const { return m_unit.IsRequired(); }

private:

	CDependencyUnit m_unit;      // Immutable copy for complete thread isolation
	std::string m_jobId;
	std::chrono::steady_clock::time_point m_createdAt;

	static std::string GenerateJobId(std::string_view identifier);
};
} // namespace Ctrn
