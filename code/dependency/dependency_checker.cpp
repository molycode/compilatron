#include "dependency/dependency_checker.hpp"
#include "common/loggers.hpp"
#include "common/process_executor.hpp"
#include <algorithm>
#include <format>
#include <fstream>
#include <filesystem>
#include <optional>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
CDependencyChecker::CDependencyChecker()
{
	m_dependencies = {
		{"OpenGL Dev", "libgl1-mesa-dev", "pkg-config --exists gl", false, false},
		{"GLFW3 Dev", "libglfw3-dev", "pkg-config --exists glfw3", true, false},
		{"X11 Dev", "libx11-dev", "pkg-config --exists x11", false, false},
		{"Xrandr Dev", "libxrandr-dev", "pkg-config --exists xrandr", false, false},
		{"Xinerama Dev", "libxinerama-dev", "pkg-config --exists xinerama", false, false},
		{"Xcursor Dev", "libxcursor-dev", "pkg-config --exists xcursor", false, false},
		{"Xi Dev", "libxi-dev", "pkg-config --exists xi", false, false},
	};
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyChecker::CheckAllDependencies()
{
	gDepLog.Info(Tge::Logging::ETarget::File, "DependencyChecker: Starting dependency check for {} dependencies", m_dependencies.size());
	bool allRequired{ true };

	for (auto& dep : m_dependencies)
	{
		dep.available = CheckCommandExists(dep.checkCommand);

		if (dep.required && !dep.available)
		{
			allRequired = false;
			gDepLog.Warning(Tge::Logging::ETarget::File, "DependencyChecker: Missing required dependency: {}", dep.name);
		}
	}

	DetectSystem();

	return allRequired;
}

//////////////////////////////////////////////////////////////////////////
std::vector<SDependencyInfo> CDependencyChecker::GetMissingDependencies() const
{
	std::vector<SDependencyInfo> missing;

	for (auto const& dep : m_dependencies)
	{
		if (!dep.available)
		{
			missing.push_back(dep);
		}
	}

	return missing;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyChecker::CanRunGui() const
{
	bool allRequired{ true };

	for (auto const& dep : m_dependencies)
	{
		if (dep.required && !dep.available)
		{
			allRequired = false;
		}
	}

	return allRequired && m_hasDisplayServer;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyChecker::InstallDependencies()
{
	std::string installCmd{ GetInstallCommand() };

	if (!installCmd.empty())
	{
		gDepLog.Info(Tge::Logging::ETarget::Console, "Installing missing dependencies...");
		gDepLog.Info(Tge::Logging::ETarget::File, "Command: {}", installCmd);

		auto const installResult = CProcessExecutor::Execute(installCmd);

		if (installResult.success)
		{
			gDepLog.Info(Tge::Logging::ETarget::Console, "Dependencies installed successfully!");

			for (auto& dep : m_dependencies)
			{
				if (!dep.available)
				{
					dep.available = CheckCommandExists(dep.checkCommand);
				}
			}
		}
		else
		{
			gDepLog.Warning(Tge::Logging::ETarget::Console, "Failed to install dependencies.");
			gDepLog.Warning(Tge::Logging::ETarget::File, "Dependency install command failed: {}", installCmd);
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyChecker::GetStatusSummary() const
{
	int available{ static_cast<int>(std::count_if(m_dependencies.begin(), m_dependencies.end(),
	                              [](auto const& dep) { return dep.available; })) };
	int total{ static_cast<int>(m_dependencies.size()) };

	return std::format("Dependencies: {}/{} available", available, total);
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyChecker::GetInstallCommand() const
{
	std::vector<std::string> missingPackages;

	for (auto const& dep : m_dependencies)
	{
		if (!dep.available)
		{
			missingPackages.push_back(dep.packageName);
		}
	}

	std::string result;

	if (!missingPackages.empty())
	{
		std::string distro{ GetDistroId() };
		std::string cmd;

		if (distro == "ubuntu" || distro == "debian")
		{
			cmd = "sudo apt update && sudo apt install -y";

			for (auto const& pkg : missingPackages)
			{
				cmd += " " + pkg;
			}

			result = cmd;
		}
		else if (distro == "fedora")
		{
			cmd = "sudo dnf install -y";

			for (auto const& pkg : missingPackages)
			{
				cmd += " " + pkg;
			}

			result = cmd;
		}
		else if (distro == "arch")
		{
			cmd = "sudo pacman -S --noconfirm";

			for (auto const& pkg : missingPackages)
			{
				cmd += " " + pkg;
			}

			result = cmd;
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyChecker::GetLocalDepsPath() const
{
	// Use the unified dependencies folder relative to executable for a self-contained structure
	std::string currentDir{ std::filesystem::current_path().string() };
	return currentDir + "/dependencies";
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyChecker::SetupLocalEnvironment()
{
	std::string depsPath{ GetLocalDepsPath() };

	if (std::filesystem::exists(depsPath))
	{
		std::string binPath{ depsPath + "/bin" };

		if (std::filesystem::exists(binPath))
		{
			std::string currentPath{ std::getenv("PATH") ? std::getenv("PATH") : "" };
			std::string newPath{ binPath + ":" + currentPath };
			setenv("PATH", newPath.c_str(), 1);
		}
	}

	return true;
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyChecker::GetLastInstallError() const
{
	return m_lastInstallError;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyChecker::CheckCommandExists(std::string_view command) const
{
	return CProcessExecutor::Execute(command).success;
}

//////////////////////////////////////////////////////////////////////////
bool CDependencyChecker::CheckPackageInstalled(std::string_view package) const
{
	std::string distro{ GetDistroId() };

	if (distro == "ubuntu" || distro == "debian")
	{
		return CheckCommandExists(std::format("dpkg -l {}", package));
	}
	else if (distro == "fedora")
	{
		return CheckCommandExists(std::format("rpm -q {}", package));
	}
	else if (distro == "arch")
	{
		return CheckCommandExists(std::format("pacman -Q {}", package));
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
void CDependencyChecker::DetectSystem()
{
	if (std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr)
	{
		m_hasDisplayServer = true;
	}
}

//////////////////////////////////////////////////////////////////////////
std::string CDependencyChecker::GetDistroId()
{
	std::ifstream osRelease("/etc/os-release");
	std::string line;
	std::string result{ "unknown" };
	bool found{ false };

	while (std::getline(osRelease, line) && !found)
	{
		if (line.find("ID=") == 0)
		{
			std::string id{ line.substr(3) };

			// Remove surrounding quotes if present
			if (id.front() == '"' && id.back() == '"')
			{
				id = id.substr(1, id.length() - 2);
			}

			result = id;
			found = true;
		}
	}

	return result;
}
} // namespace Ctrn
