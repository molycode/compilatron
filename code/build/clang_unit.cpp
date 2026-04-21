#include "build/clang_unit.hpp"
#include "build/compiler_registry.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "dependency/dependency_manager.hpp"
#include <format>
#include <sstream>
#include <filesystem>

namespace Ctrn
{
namespace
{
std::string BuildClangPath(std::string_view dependenciesDir)
{
	std::string result{ dependenciesDir };

	std::string const python3{ g_dependencyManager.GetSelectedPath("python3") };

	if (!python3.empty())
	{
		result += ':';
		result += std::filesystem::path(python3).parent_path().string();
	}

	return result;
}
} // namespace

//////////////////////////////////////////////////////////////////////////
CClangUnit::CClangUnit(std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig)
	: CCompilerUnit(ECompilerKind::Clang, std::move(displayName), globalSettings, buildConfig)
{
	m_unitLog.Info(Tge::Logging::ETarget::File, std::format("Created Clang unit: version={}, folder={}", GetName(), GetFolderName()));
}

//////////////////////////////////////////////////////////////////////////
SClangSettings const& CClangUnit::GetClangConfig() const
{
	return m_clangSettings;
}

//////////////////////////////////////////////////////////////////////////
std::string CClangUnit::GetSourcePath() const
{
	return GetBuildConfig().sourcesDir;
}

//////////////////////////////////////////////////////////////////////////
std::string CClangUnit::GetBuildPath() const
{
	return GetBuildConfig().buildDir;
}

//////////////////////////////////////////////////////////////////////////
std::string CClangUnit::GetInstallPath() const
{
	namespace fs = std::filesystem;
	fs::path installPath = fs::path(g_dataDir) / GetGlobalSettings().installDirectory / GetFolderName();
	return installPath.string();
}

//////////////////////////////////////////////////////////////////////////
std::string CClangUnit::GetDefaultSourceUrl() const
{
	return std::string{ ClangSourceUrl } + ".git";
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CClangUnit::GetRequiredSourcePaths() const
{
	return {
		"llvm/CMakeLists.txt",
		"llvm/lib",
		"llvm/include",
		"clang/CMakeLists.txt",
		"clang/lib",
		"clang/include"
	};
}

//////////////////////////////////////////////////////////////////////////
std::string CClangUnit::GenerateInstallCommand() const
{
	std::ostringstream cmd;
	auto const& buildConfig = GetBuildConfig();

	std::string const cmakeSelected{ g_dependencyManager.GetSelectedPath("cmake") };
	std::string const cmake{ cmakeSelected.empty() ? "cmake" : cmakeSelected };

	std::string const clangPath{ BuildClangPath(buildConfig.dependenciesDir) };

	if (GetClangConfig().generator == ECMakeGenerator::UnixMakefiles)
	{
		cmd << "PATH=\"" << clangPath << ":$PATH\" && cd " << GetBuildPath()
		    << " && make install";
	}
	else
	{
		cmd << "PATH=\"" << clangPath << ":$PATH\" " << cmake << " --install " << GetBuildPath();
	}

	return cmd.str();
}

//////////////////////////////////////////////////////////////////////////
std::expected<std::string, std::string> CClangUnit::GetCompilerCMakeFlags() const
{
	std::string const cxxCompiler{ GetResolvedCompiler() };

	std::expected<std::string, std::string> result;

	if (!cxxCompiler.empty())
	{
		std::string const cCompiler{ GetHostCompilerCPath(cxxCompiler) };

		if (cCompiler.empty())
		{
			result = std::unexpected(std::string{"Could not derive C compiler path from: "} + cxxCompiler);
		}
		else if (HasProblematicPathCharacters(cCompiler) || HasProblematicPathCharacters(cxxCompiler))
		{
			std::string errorMsg{ "Compiler path contains shell-problematic characters. Please use a clean path without special characters." };

			if (HasProblematicPathCharacters(cCompiler))
			{
				errorMsg += "\nC compiler: " + cCompiler;
			}

			if (HasProblematicPathCharacters(cxxCompiler))
			{
				errorMsg += "\nC++ compiler: " + cxxCompiler;
			}

			result = std::unexpected(errorMsg);
		}
		else
		{
			gLog.Info(Tge::Logging::ETarget::File, "CClangUnit: Using compilers: CC='{}', CXX='{}'", cCompiler, cxxCompiler);
			result = std::string{"-DCMAKE_C_COMPILER=\""} + cCompiler + "\" -DCMAKE_CXX_COMPILER=\"" + cxxCompiler + "\"";
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::expected<std::string, std::string> CClangUnit::GenerateConfigureCommand() const
{
	auto const& config = GetClangConfig();
	auto const& buildConfig = GetBuildConfig();

	auto const compilerFlags = GetCompilerCMakeFlags();
	std::expected<std::string, std::string> result;

	if (compilerFlags)
	{
		std::ostringstream cmd;
		cmd << "cd \"" << GetBuildPath() << "\" && ";

		std::string const cmakeSelected{ g_dependencyManager.GetSelectedPath("cmake") };
	std::string const cmake{ cmakeSelected.empty() ? "cmake" : cmakeSelected };

		if (!buildConfig.dependenciesDir.empty())
		{
			cmd << "PATH=\"" << BuildClangPath(buildConfig.dependenciesDir) << ":$PATH\" ";
		}

		cmd << cmake << " -S \"" << GetSourcePath() << "/llvm\"";

		if (!compilerFlags.value().empty())
		{
			cmd << " " << compilerFlags.value();
		}

		std::string buildType{};

		switch (config.buildType)
		{
			case EBuildType::Debug:          buildType = "Debug"; break;
			case EBuildType::Release:        buildType = "Release"; break;
			case EBuildType::RelWithDebInfo: buildType = "RelWithDebInfo"; break;
			case EBuildType::MinSizeRel:     buildType = "MinSizeRel"; break;
		}
		cmd << " -DCMAKE_BUILD_TYPE=" << buildType;

		int cppStd{ 20 };

		switch (config.cppStandard)
		{
			case ECppStandard::Cpp11: cppStd = 11; break;
			case ECppStandard::Cpp14: cppStd = 14; break;
			case ECppStandard::Cpp17: cppStd = 17; break;
			case ECppStandard::Cpp20: cppStd = 20; break;
			case ECppStandard::Cpp23: cppStd = 23; break;
			case ECppStandard::Cpp26: cppStd = 26; break;
			case ECppStandard::Cpp29: cppStd = 29; break;
		}
		cmd << " -DCMAKE_CXX_STANDARD=" << cppStd;
		cmd << " -DCMAKE_CXX_STANDARD_REQUIRED=" << (config.cxxStandardRequired ? "True" : "False");
		cmd << " -DCMAKE_CXX_EXTENSIONS=" << (config.cxxExtensions ? "True" : "False");

		cmd << " -DCMAKE_INSTALL_PREFIX=\"" << GetInstallPath() << "\"";

		if (config.buildWithInstallRpath)
		{
			cmd << " -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON";
		}

		std::vector<std::string> targets;

		if (config.targetX86)
		{
			targets.push_back("X86");
		}

		if (config.targetAArch64)
		{
			targets.push_back("AArch64");
		}

		if (config.targetARM)
		{
			targets.push_back("ARM");
		}

		if (config.targetRISCV)
		{
			targets.push_back("RISCV");
		}

		if (!config.customTargets.value.empty())
		{
			std::string customList{ config.customTargets.value };
			size_t pos{ 0 };

			while ((pos = customList.find(';')) != std::string::npos)
			{
				std::string target{ customList.substr(0, pos) };

				if (!target.empty())
				{
					targets.push_back(target);
				}

				customList.erase(0, pos + 1);
			}

			if (!customList.empty())
			{
				targets.push_back(customList);
			}
		}

		if (!targets.empty())
		{
			std::string targetList{};

			for (size_t i = 0; i < targets.size(); ++i)
			{
				if (i > 0)
				{
					targetList += ";";
				}

				targetList += targets[i];
			}
			cmd << " -DLLVM_TARGETS_TO_BUILD=\"" << targetList << "\"";
		}

		// LLVM projects and runtimes (separated according to LLVM build system requirements)
		std::vector<std::string> projects;
		std::vector<std::string> runtimes;

		// Projects (go into LLVM_ENABLE_PROJECTS)
		if (config.projectClang)
		{
			projects.push_back("clang");
		}

		if (config.projectClangToolsExtra)
		{
			projects.push_back("clang-tools-extra");
		}

		if (config.projectLld)
		{
			projects.push_back("lld");
		}

		if (config.projectCompilerRt)
		{
			projects.push_back("compiler-rt");
		}

		if (config.projectLldb)
		{
			projects.push_back("lldb");
		}

		if (config.projectOpenmp)
		{
			projects.push_back("openmp");
		}

		if (config.projectPolly)
		{
			projects.push_back("polly");
		}

		if (config.projectPstl)
		{
			projects.push_back("pstl");
		}

		if (config.projectMlir)
		{
			projects.push_back("mlir");
		}

		if (config.projectFlang)
		{
			projects.push_back("flang");
		}

		if (config.projectBolt)
		{
			projects.push_back("bolt");
		}

		// Runtimes (go into LLVM_ENABLE_RUNTIMES)
		if (config.projectLibcxx)
		{
			runtimes.push_back("libcxx");
		}

		if (config.projectLibcxxabi)
		{
			runtimes.push_back("libcxxabi");
		}

		if (config.projectLibunwind)
		{
			runtimes.push_back("libunwind");
		}

		if (!config.customProjects.value.empty())
		{
			std::string customList{ config.customProjects.value };
			size_t pos{ 0 };

			while ((pos = customList.find(';')) != std::string::npos)
			{
				std::string project{ customList.substr(0, pos) };

				if (!project.empty())
				{
					projects.push_back(project);
				}

				customList.erase(0, pos + 1);
			}

			if (!customList.empty())
			{
				projects.push_back(customList);
			}
		}

		if (!projects.empty())
		{
			std::string projectList{};

			for (size_t i = 0; i < projects.size(); ++i)
			{
				if (i > 0)
				{
					projectList += ";";
				}

				projectList += projects[i];
			}
			cmd << " -DLLVM_ENABLE_PROJECTS=\"" << projectList << "\"";
		}

		if (!runtimes.empty())
		{
			std::string runtimeList{};

			for (size_t i = 0; i < runtimes.size(); ++i)
			{
				if (i > 0)
				{
					runtimeList += ";";
				}

				runtimeList += runtimes[i];
			}
			cmd << " -DLLVM_ENABLE_RUNTIMES=\"" << runtimeList << "\"";
		}

		if (config.generator == ECMakeGenerator::Ninja)
		{
			cmd << " -G Ninja";

			{
				bool const isRelease{ config.buildType == EBuildType::Release || config.buildType == EBuildType::MinSizeRel };
				int const linkJobs{ config.numNinjaLinkJobs > 0
					? config.numNinjaLinkJobs
					: (isRelease ? g_cpuInfo.GetDefaultLinkJobs() : g_cpuInfo.GetDefaultLinkJobsConservative()) };
				cmd << " -DLLVM_PARALLEL_LINK_JOBS=" << linkJobs;
			}
		}
		else if (config.generator == ECMakeGenerator::UnixMakefiles)
		{
			cmd << " -G \"Unix Makefiles\"";
		}

		if (!config.customCFlags.value.empty())
		{
			cmd << " -DCMAKE_C_FLAGS=\"" << config.customCFlags << "\"";
		}

		if (!config.customCxxFlags.value.empty())
		{
			cmd << " -DCMAKE_CXX_FLAGS=\"" << config.customCxxFlags << "\"";
		}

		cmd << " -DLLVM_ENABLE_RTTI=" << (config.enableRtti ? "ON" : "OFF");
		cmd << " -DLLVM_ENABLE_EH=" << (config.enableEh ? "ON" : "OFF");
		cmd << " -DLLVM_ENABLE_ZLIB=" << (config.enableZlib ? "ON" : "OFF");
		cmd << " -DLLVM_ENABLE_FFI=" << (config.enableLibffi ? "ON" : "OFF");
		cmd << " -DLLVM_ENABLE_TERMINFO=" << (config.enableTerminfo ? "ON" : "OFF");
		cmd << " -DLLVM_ENABLE_LIBXML2=" << (config.enableLibxml2 ? "ON" : "OFF");
		cmd << " -DLLVM_ENABLE_ASSERTIONS=" << (config.enableAssertions ? "ON" : "OFF");
		cmd << " -DLLVM_BUILD_LLVM_DYLIB=" << (config.buildLlvmDylib ? "ON" : "OFF");
		cmd << " -DLLVM_LINK_LLVM_DYLIB=" << (config.linkLlvmDylib ? "ON" : "OFF");
		cmd << " -DLLVM_INSTALL_UTILS=" << (config.installUtils ? "ON" : "OFF");

		if (config.ltoMode.value != "Off")
		{
			cmd << " -DLLVM_ENABLE_LTO=" << config.ltoMode;
		}

		if (config.optimizedTablegen)
		{
			cmd << " -DLLVM_OPTIMIZED_TABLEGEN=ON";
		}

		if (config.linker.value != "default")
		{
			if (config.linker.value == "lld")
			{
				cmd << " -DLLVM_USE_LINKER=lld";
				cmd << " -DCLANG_DEFAULT_LINKER=lld";

				// Resolve and log which ld.lld will be used
				std::filesystem::path lldPath;

				if (!config.lldOverridePath.value.empty())
				{
					lldPath = config.lldOverridePath.value;
				}
				else
				{
					std::string const compiler{ GetResolvedCompiler() };
					std::error_code ec;
					std::filesystem::path const resolved{ std::filesystem::canonical(compiler, ec) };
					std::filesystem::path const compilerDir{ ec
						? std::filesystem::path{ compiler }.parent_path()
						: resolved.parent_path() };

					std::filesystem::path const colocated{ compilerDir / "ld.lld" };

					if (std::filesystem::exists(colocated))
					{
						lldPath = colocated;
					}
					else
					{
						char const* pathEnv{ std::getenv("PATH") };

						if (pathEnv != nullptr)
						{
							std::string_view pathStr{ pathEnv };
							size_t start{ 0 };

							while (lldPath.empty())
							{
								size_t const colon{ pathStr.find(':', start) };
								std::string_view const dir{ colon == std::string_view::npos
									? pathStr.substr(start)
									: pathStr.substr(start, colon - start) };

								if (!dir.empty())
								{
									std::filesystem::path const candidate{ std::filesystem::path{ dir } / "ld.lld" };

									if (std::filesystem::exists(candidate))
									{
										lldPath = candidate;
									}
								}

								if (colon == std::string_view::npos)
								{
									break;
								}

								start = colon + 1;
							}
						}
					}
				}

				if (!lldPath.empty())
				{
					m_unitLog.Info(Tge::Logging::ETarget::Console, "Linker: ld.lld resolved to: {}", lldPath.string());
				}
				else
				{
					m_unitLog.Warning(Tge::Logging::ETarget::Console, "Linker: lld selected but ld.lld not found — build will likely fail");
				}
			}
			else if (config.linker.value == "gold")
			{
				cmd << " -DLLVM_USE_LINKER=gold";
			}
			else if (config.linker.value == "bfd")
			{
				cmd << " -DLLVM_USE_LINKER=bfd";
			}
		}

		if (config.enableGoldPlugin)
		{
			std::string const binutilsIncDir{ g_dependencyManager.GetSelectedPath("binutils") };

			if (!binutilsIncDir.empty())
			{
				cmd << " -DLLVM_BINUTILS_INCDIR=\"" << binutilsIncDir << "\"";
			}
		}

		// Additional configure flags (applied last so they can override anything)
		if (!config.additionalConfigureFlags.value.empty())
		{
			cmd << " " << config.additionalConfigureFlags;
		}

		result = cmd.str();
	}
	else
	{
		result = std::unexpected(compilerFlags.error());
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CClangUnit::GenerateBuildCommand() const
{
	auto const& config = GetClangConfig();
	auto const& buildConfig = GetBuildConfig();

	std::ostringstream cmd;
	cmd << "PATH=\"" << BuildClangPath(buildConfig.dependenciesDir) << ":$PATH\" && cd " << GetBuildPath();

	if (config.generator == ECMakeGenerator::UnixMakefiles)
	{
		cmd << " && make -j" << buildConfig.numJobs;
	}
	else
	{
		// Resolve ninja: honour user selection first, then check local deps dir, then fall back to PATH
		std::string ninjaCmd{ g_dependencyManager.GetSelectedPath("ninja") };

		if (ninjaCmd.empty())
		{
			std::string const localNinja{ buildConfig.dependenciesDir + "/ninja" };
			ninjaCmd = std::filesystem::exists(localNinja) ? localNinja : "ninja";
		}

		cmd << " && " << ninjaCmd << " -j" << buildConfig.numJobs;
	}

	return cmd.str();
}

} // namespace Ctrn
