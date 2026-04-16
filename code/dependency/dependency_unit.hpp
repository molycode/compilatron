#pragma once

#include "dependency/dependency_manager.hpp"
#include <string>
#include <string_view>

namespace Ctrn
{
class CDependencyUnit final
{
public:

	struct SInstallationResult final
	{
		bool success = false;
		std::string message;
		std::string detailedLog;
		std::string unitName;
		std::string jobId;
	};

	explicit CDependencyUnit(SAdvancedDependencyInfo const& depInfo);
	~CDependencyUnit() = default;

	// Immutable interface — safe to call from any thread
	std::string GetIdentifier() const { return m_info.identifier; }
	std::string GetName() const { return m_info.name; }
	bool IsRequired() const { return m_info.IsRequired(); }
	EDependencyCategory GetCategory() const { return m_info.category; }

	// Operates on an immutable copy — no shared state, safe to call concurrently
	[[nodiscard]] SInstallationResult Install(std::string_view jobId) const;

private:

	SAdvancedDependencyInfo m_info; // Immutable copy for thread safety

	SInstallationResult DownloadAndBuildLocal(std::string_view jobId) const;
	std::string GetExecutableDirectory() const;
	void LogProgress(std::string_view jobId, std::string_view message) const;
};

} // namespace Ctrn
