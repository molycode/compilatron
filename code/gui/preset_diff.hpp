#pragma once

#include "build/build_settings.hpp"
#include <string>
#include <vector>

namespace Ctrn
{
std::vector<std::string> GeneratePresetDiff(SBuildSettings const& current, SBuildSettings const& saved);
} // namespace Ctrn
