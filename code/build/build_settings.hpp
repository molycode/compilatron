#pragma once

#include "build/clang_settings.hpp"
#include "build/gcc_settings.hpp"
#include "build/property.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace Ctrn
{
constexpr std::string_view GccSourceUrl{ "https://github.com/gcc-mirror/gcc" };
constexpr std::string_view ClangSourceUrl{ "https://github.com/llvm/llvm-project" };

struct SCompilerEntry final
{
	uint16_t id{ 0 }; // Runtime tab identifier — not serialised

	SProperty<std::string> name{             "name",             "Version",            ""      };
	SProperty<std::string> folderName{       "folderName",       "Folder Name",        ""      };
	SProperty<int>         numJobs{          "jobCount",         "Compilation Jobs",   0       };
	SProperty<bool>        keepDependencies{ "keepDependencies", "Keep dependencies",  true    };
	SProperty<bool>        keepSources{      "keepSources",      "Keep sources",       true    };
	SProperty<bool>        updateSources{    "updateSources",    "Update sources",     true    };
	SProperty<std::string> hostCompiler{     "compilerOverride", "Host Compiler",      ""      };
	SProperty<std::string> compilerType{     "compilerType",     "Compiler type",      ""      };

	// Sub-settings — not part of Properties(); serialised separately via their own Properties()
	SClangSettings clangSettings;
	SGccSettings gccSettings;

	auto Properties()
	{
		return std::tie(name, folderName, numJobs, keepDependencies,
		                keepSources, updateSources, hostCompiler, compilerType);
	}

	auto Properties() const
	{
		return std::tie(name, folderName, numJobs, keepDependencies,
		                keepSources, updateSources, hostCompiler, compilerType);
	}

	bool operator==(SCompilerEntry const&) const = default;
};

struct SBuildSettings final
{
	std::vector<SCompilerEntry> compilerEntries;
	std::string installDirectory{ "compilers" };
	std::string globalHostCompiler;
	std::map<std::string, std::string> dependencyLocationSelections;

	bool operator==(SBuildSettings const&) const = default;
};
} // namespace Ctrn
