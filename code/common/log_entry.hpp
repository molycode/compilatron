#pragma once

#include <tge/logging/log_level.hpp>
#include <string>

namespace Ctrn
{

struct SLogEntry final
{
	Tge::Logging::ELogLevel level{ Tge::Logging::ELogLevel::Info };
	std::string text;
};

} // namespace Ctrn
