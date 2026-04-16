#pragma once

#include "build/compiler.hpp"

#include <string>
#include <vector>

namespace Ctrn
{
struct SCompilerScanResult final
{
	std::string directory;
	std::vector<SCompiler> compilers;
	std::vector<SCompiler> alreadyRegistered;
};
} // namespace Ctrn
