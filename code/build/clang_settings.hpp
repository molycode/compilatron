#pragma once

#include "build/property.hpp"
#include "build/build_type.hpp"
#include "build/cpp_standard.hpp"
#include "build/cmake_generator.hpp"

#include <string>
#include <tuple>

namespace Ctrn
{
struct SClangSettings final
{
	SProperty<EBuildType>    buildType{              "clang_buildType",              "Build type",           EBuildType::Release         };
	SProperty<ECppStandard>  cppStandard{            "clang_cppStandard",            "C++ standard",         ECppStandard::Cpp17         };
	SProperty<bool>          cxxStandardRequired{    "clang_cxxStandardRequired",    "Std required",         true                        };
	SProperty<bool>          cxxExtensions{          "clang_cxxExtensions",          "CXX extensions",       false                       };

	SProperty<bool>          targetX86{              "clang_targetX86",              "Target X86",           true                        };
	SProperty<bool>          targetAArch64{          "clang_targetAArch64",          "Target AArch64",       true                        };
	SProperty<bool>          targetARM{              "clang_targetARM",              "Target ARM",           true                        };
	SProperty<bool>          targetRISCV{            "clang_targetRISCV",            "Target RISC-V",        true                        };
	SProperty<std::string>   customTargets{          "clang_customTargets",          "Custom targets",       ""                          };

	SProperty<bool>          projectClang{           "clang_projectClang",           "Clang",            true                    };
	SProperty<bool>          projectClangToolsExtra{ "clang_projectClangToolsExtra", "clang-tools-extra", true                  };
	SProperty<bool>          projectLld{             "clang_projectLld",             "LLD",              true                    };
	SProperty<bool>          projectCompilerRt{      "clang_projectCompilerRt",      "CompilerRT",       false                   };
	SProperty<bool>          projectLibcxx{          "clang_projectLibcxx",          "libc++",           false                   };
	SProperty<bool>          projectLibcxxabi{       "clang_projectLibcxxabi",       "libc++abi",        false                   };
	SProperty<bool>          projectLibunwind{       "clang_projectLibunwind",       "libunwind",        false                   };
	SProperty<bool>          projectLldb{            "clang_projectLldb",            "LLDB",             false                   };
	SProperty<bool>          projectOpenmp{          "clang_projectOpenmp",          "OpenMP",           false                   };
	SProperty<bool>          projectPolly{           "clang_projectPolly",           "Polly",            false                   };
	SProperty<bool>          projectPstl{            "clang_projectPstl",            "PSTL",             false                   };
	SProperty<bool>          projectMlir{            "clang_projectMlir",            "MLIR",             false                   };
	SProperty<bool>          projectFlang{           "clang_projectFlang",           "Flang",            false                   };
	SProperty<bool>          projectBolt{            "clang_projectBolt",            "BOLT",             false                   };
	SProperty<std::string>   customProjects{         "clang_customProjects",         "Custom projects",      ""                          };

	SProperty<std::string>   customCFlags{           "clang_customCFlags",           "CFLAGS",               ""                          };
	SProperty<std::string>   customCxxFlags{         "clang_customCxxFlags",         "CXXFLAGS",             ""                          };
	SProperty<bool>          enableRtti{             "clang_enableRtti",             "RTTI",                 true                        };
	SProperty<bool>          enableEh{               "clang_enableEh",               "Exception handling",   true                        };
	SProperty<bool>          enableZlib{             "clang_enableZlib",             "zlib compression",     true                        };
	SProperty<bool>          enableLibffi{           "clang_enableLibffi",           "libffi",               false                       };
	SProperty<bool>          enableTerminfo{         "clang_enableTerminfo",         "Terminfo",             true                        };
	SProperty<bool>          enableLibxml2{          "clang_enableLibxml2",          "libxml2",              false                       };
	SProperty<bool>          enableAssertions{       "clang_enableAssertions",       "Assertions",           false                       };
	SProperty<bool>          buildLlvmDylib{         "clang_buildLlvmDylib",         "Build LLVM dylib",     false                       };
	SProperty<bool>          linkLlvmDylib{          "clang_linkLlvmDylib",          "Link LLVM dylib",      false                       };
	SProperty<bool>          installUtils{           "clang_installUtils",           "Install utils",        true                        };
	SProperty<std::string>   ltoMode{               "clang_ltoMode",                "LTO mode",             "Off"                       };
	SProperty<std::string>   linker{                 "clang_linker",                 "Linker",               "bfd"                       };
	SProperty<bool>          optimizedTablegen{      "clang_optimizedTablegen",      "Optimized TableGen",   true                        };

	SProperty<ECMakeGenerator> generator{            "clang_generator",              "Generator",            ECMakeGenerator::Ninja      };
	SProperty<int>           numNinjaLinkJobs{       "clang_ninjaLinkJobs",          "Ninja link jobs",      0                           };
	SProperty<bool>          buildWithInstallRpath{  "clang_buildWithInstallRpath",  "Build with install RPATH", false                   };
	SProperty<std::string>   additionalConfigureFlags{ "clang_additionalConfigureFlags", "Extra CMake flags", ""                         };

	auto Properties()
	{
		return std::tie(buildType, cppStandard, cxxStandardRequired, cxxExtensions,
		                targetX86, targetAArch64, targetARM, targetRISCV, customTargets,
		                projectClang, projectClangToolsExtra, projectLld, projectCompilerRt,
		                projectLibcxx, projectLibcxxabi, projectLibunwind, projectLldb,
		                projectOpenmp, projectPolly, projectPstl, projectMlir, projectFlang,
		                projectBolt, customProjects,
		                customCFlags, customCxxFlags, enableRtti, enableEh, enableZlib, enableLibffi,
		                enableTerminfo, enableLibxml2, enableAssertions, buildLlvmDylib, linkLlvmDylib,
		                installUtils, ltoMode, linker, optimizedTablegen,
		                generator, numNinjaLinkJobs, buildWithInstallRpath, additionalConfigureFlags);
	}

	auto Properties() const
	{
		return std::tie(buildType, cppStandard, cxxStandardRequired, cxxExtensions,
		                targetX86, targetAArch64, targetARM, targetRISCV, customTargets,
		                projectClang, projectClangToolsExtra, projectLld, projectCompilerRt,
		                projectLibcxx, projectLibcxxabi, projectLibunwind, projectLldb,
		                projectOpenmp, projectPolly, projectPstl, projectMlir, projectFlang,
		                projectBolt, customProjects,
		                customCFlags, customCxxFlags, enableRtti, enableEh, enableZlib, enableLibffi,
		                enableTerminfo, enableLibxml2, enableAssertions, buildLlvmDylib, linkLlvmDylib,
		                installUtils, ltoMode, linker, optimizedTablegen,
		                generator, numNinjaLinkJobs, buildWithInstallRpath, additionalConfigureFlags);
	}

	void ResetToDefaults()
	{
		std::apply([](auto&... props) { (props.Reset(), ...); }, Properties());
	}

	bool operator==(SClangSettings const&) const = default;
};
} // namespace Ctrn
