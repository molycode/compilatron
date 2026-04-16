#pragma once

#include <tge/non_copyable.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace Ctrn
{

struct SDependencyInfo final
{
	std::string name;
	std::string packageName;  // For apt/system package manager
	std::string checkCommand; // Command to check if installed
	bool required{ false };
	bool available{ false };
};

class CDependencyChecker final : private Tge::SNoCopyNoMove
{
public:

	CDependencyChecker();

	[[nodiscard]] bool CheckAllDependencies();

	std::vector<SDependencyInfo> GetMissingDependencies() const;

	std::string GetInstallCommand() const;

	[[nodiscard]] bool InstallDependencies();

	// Checks that required deps are present and a display server is available
	[[nodiscard]] bool CanRunGui() const;

	std::string GetStatusSummary() const;

	std::string GetLocalDepsPath() const;
	[[nodiscard]] bool SetupLocalEnvironment();

	std::string GetLastInstallError() const;

	static std::string GetDistroId();

private:

	std::vector<SDependencyInfo> m_dependencies;
	bool m_hasDisplayServer{ false };
	mutable std::string m_lastInstallError;

	bool CheckCommandExists(std::string_view command) const;
	bool CheckPackageInstalled(std::string_view package) const;
	void DetectSystem();
};

} // namespace Ctrn
