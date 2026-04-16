#pragma once

#include "build/compiler_kind.hpp"

#include <string>

namespace Ctrn
{
struct SCompiler final
{
	std::string path;
	ECompilerKind kind{ ECompilerKind::Gcc };
	std::string version;
	std::string displayName;
	bool hasProblematicPath{ false };
	bool isRemovable{ false };   // False for system compilers — remove button is hidden
};
} // namespace Ctrn
