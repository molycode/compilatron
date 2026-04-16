#pragma once

#include "build/property.hpp"
#include "build/optimization_level.hpp"

#include <string>
#include <tuple>

namespace Ctrn
{
struct SGccSettings final
{
	SProperty<std::string>       enabledLanguages{       "gcc_enabledLanguages",       "Languages",            "c,c++"                     };
	SProperty<bool>              enableLto{              "gcc_enableLto",              "LTO",                  true                        };
	SProperty<EOptimizationLevel> optimizationLevel{    "gcc_optimizationLevel",      "Optimization",         EOptimizationLevel::O2      };
	SProperty<bool>              generateDebugSymbols{  "gcc_generateDebugSymbols",   "Debug symbols",        false                       };
	SProperty<bool>              enableBootstrap{        "gcc_enableBootstrap",        "Bootstrap",            true                        };
	SProperty<bool>              enableShared{           "gcc_enableShared",           "Shared libraries",     true                        };
	SProperty<bool>              useSystemZlib{          "gcc_useSystemZlib",          "System zlib",          false                       };
	SProperty<bool>              disableWerror{          "gcc_disableWerror",          "Disable Werror",       true                        };
	SProperty<bool>              enableChecking{         "gcc_enableChecking",         "Checking",             true                        };
	SProperty<std::string>       checkingLevel{          "gcc_checkingLevel",          "Checking level",       "release"                   };
	SProperty<bool>              disableMultilib{        "gcc_disableMultilib",        "Disable multilib",     true                        };
	SProperty<bool>              modernCppAbi{           "gcc_modernCppAbi",           "Modern C++ ABI",       true                        };
	SProperty<bool>              posixThreads{           "gcc_posixThreads",           "POSIX threads",        true                        };
	SProperty<bool>              enablePlugin{           "gcc_enablePlugin",           "Plugin support",       true                        };
	SProperty<bool>              enablePie{              "gcc_enablePie",              "PIE",                  true                        };
	SProperty<bool>              enableBuildId{          "gcc_enableBuildId",          "Build ID",             true                        };
	SProperty<bool>              enableGold{             "gcc_enableGold",             "Gold linker",          false                       };
	SProperty<std::string>       withArch{               "gcc_withArch",               "Target arch",          ""                          };
	SProperty<std::string>       withTune{               "gcc_withTune",               "Target tune",          ""                          };
	SProperty<std::string>       withSysroot{            "gcc_withSysroot",            "Sysroot",              ""                          };
	SProperty<std::string>       customCFlags{           "gcc_customCFlags",           "CFLAGS",               ""                          };
	SProperty<std::string>       customCxxFlags{         "gcc_customCxxFlags",         "CXXFLAGS",             ""                          };
	SProperty<std::string>       additionalConfigureFlags{ "gcc_additionalConfigureFlags", "Extra configure flags", ""                      };

	auto Properties()
	{
		return std::tie(enabledLanguages, enableLto, optimizationLevel, generateDebugSymbols,
		                enableBootstrap, enableShared, useSystemZlib, disableWerror,
		                enableChecking, checkingLevel, disableMultilib, modernCppAbi,
		                posixThreads, enablePlugin, enablePie, enableBuildId, enableGold,
		                withArch, withTune, withSysroot,
		                customCFlags, customCxxFlags, additionalConfigureFlags);
	}

	auto Properties() const
	{
		return std::tie(enabledLanguages, enableLto, optimizationLevel, generateDebugSymbols,
		                enableBootstrap, enableShared, useSystemZlib, disableWerror,
		                enableChecking, checkingLevel, disableMultilib, modernCppAbi,
		                posixThreads, enablePlugin, enablePie, enableBuildId, enableGold,
		                withArch, withTune, withSysroot,
		                customCFlags, customCxxFlags, additionalConfigureFlags);
	}

	void ResetToDefaults()
	{
		std::apply([](auto&... props) { (props.Reset(), ...); }, Properties());
	}

	bool operator==(SGccSettings const&) const = default;
};
} // namespace Ctrn
