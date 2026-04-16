#pragma once

#include <string>

namespace Ctrn
{

enum class EDependencyType
{
	None,
	BuildTool,      // cmake, ninja, git
	Compiler,       // gcc, clang for bootstrap
	Library,        // development libraries
	SystemPackage   // system packages via apt/dnf
};

struct SDependency final
{
	std::string name;
	std::string packageName;    // Package manager name
	std::string checkCommand;   // Command to verify availability
	EDependencyType type{ EDependencyType::None };
	bool required{ false };
	bool available{ false };
};

} // namespace Ctrn
