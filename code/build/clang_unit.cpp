#include "build/clang_unit.hpp"
#include "build/compiler_registry.hpp"
#include "common/common.hpp"
#include "common/loggers.hpp"
#include "dependency/dependency_manager.hpp"
#include <format>
#include <sstream>
#include <filesystem>
#include <fstream>

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
			targets.emplace_back("X86");
		}

		if (config.targetAArch64)
		{
			targets.emplace_back("AArch64");
		}

		if (config.targetARM)
		{
			targets.emplace_back("ARM");
		}

		if (config.targetRISCV)
		{
			targets.emplace_back("RISCV");
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
			projects.emplace_back("clang");
		}

		if (config.projectClangToolsExtra)
		{
			projects.emplace_back("clang-tools-extra");
		}

		if (config.projectLld)
		{
			projects.emplace_back("lld");
		}

		if (config.projectLldb)
		{
			projects.emplace_back("lldb");
		}

		if (config.projectPolly)
		{
			projects.emplace_back("polly");
		}

		if (config.projectMlir)
		{
			projects.emplace_back("mlir");
		}

		if (config.projectFlang)
		{
			projects.emplace_back("flang");
		}

		if (config.projectBolt)
		{
			projects.emplace_back("bolt");
		}

		// Runtimes (go into LLVM_ENABLE_RUNTIMES)
		if (config.projectLibcxx)
		{
			runtimes.emplace_back("libcxx");
		}

		if (config.projectLibcxxabi)
		{
			runtimes.emplace_back("libcxxabi");
		}

		if (config.projectLibunwind)
		{
			runtimes.emplace_back("libunwind");
		}

		if (config.projectCompilerRt)
		{
			runtimes.emplace_back("compiler-rt");
		}

		if (config.projectOpenmp)
		{
			runtimes.emplace_back("openmp");
		}

		if (config.projectPstl)
		{
			runtimes.emplace_back("pstl");
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
		cmd << " -DLLVM_ENABLE_ZLIB=" << (config.enableZlib ? "FORCE_ON" : "OFF");
		cmd << " -DLLVM_ENABLE_ZSTD=" << (config.enableZstd ? "FORCE_ON" : "OFF");
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

		if (config.linker.value == "lld")
		{
			cmd << " -DLLVM_USE_LINKER=lld";
			cmd << " -DCLANG_DEFAULT_LINKER=lld";
		}
		else if (config.linker.value == "bfd")
		{
			cmd << " -DLLVM_USE_LINKER=bfd";
		}

		// Default toolchain libraries the built clang links unless overridden per-compile
		// (e.g. -stdlib=libc++). The first value of each is the platform default on Linux.
		cmd << " -DCLANG_DEFAULT_CXX_STDLIB=" << (config.defaultCxxStdlib == ECxxStdlib::LibCxx ? "libc++" : "libstdc++");
		cmd << " -DCLANG_DEFAULT_RTLIB=" << (config.defaultRtlib == ERtlib::CompilerRt ? "compiler-rt" : "libgcc");
		cmd << " -DCLANG_DEFAULT_UNWINDLIB=" << (config.defaultUnwindlib == EUnwindlib::LibUnwind ? "libunwind" : "libgcc");

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

//////////////////////////////////////////////////////////////////////////
void CClangUnit::OnPostInstall(std::filesystem::path const& installPath)
{
	namespace fs = std::filesystem;

	// Opt-in: baking a runtime rpath into every program this toolchain builds is a local
	// convenience, not wanted for depot/farm toolchains whose artifacts ship elsewhere.
	if (GetClangConfig().writeRpathConfig)
	{
		std::error_code ec;

		// Locate the per-target runtime directory (e.g. lib/x86_64-unknown-linux-gnu) holding
		// the shared runtimes — libomp.so, libc++.so, libunwind.so. It only exists when those
		// runtimes were built, so this self-gates: a plain build finds nothing.
		fs::path const libRoot{ installPath / "lib" };
		fs::path runtimeDir;

		if (fs::is_directory(libRoot, ec))
		{
			for (auto const& entry : fs::directory_iterator(libRoot, ec))
			{
				if (runtimeDir.empty() && entry.is_directory()
				    && entry.path().filename().string().find("linux") != std::string::npos)
				{
					for (auto const& lib : fs::directory_iterator(entry.path(), ec))
					{
						if (lib.path().filename().string().find(".so") != std::string::npos)
						{
							runtimeDir = entry.path();
						}
					}
				}
			}
		}

		if (!runtimeDir.empty())
		{
			fs::path const binDir{ installPath / "bin" };

			if (fs::is_directory(binDir, ec))
			{
				// Clang auto-loads <bindir>/clang.cfg and <bindir>/clang++.cfg (they do not
				// share a file), so write both. The path uses clang's <CFGDIR> token (= the bin
				// directory), so it resolves wherever the toolchain is moved or bootstrapped
				// rather than hardcoding this machine's install path.
				std::string const content{ std::format(
					"# Generated by Compilatron.\n"
					"# Adds this toolchain's runtime library directory to the rpath of programs you build,\n"
					"# so they find the bundled libomp / libc++ / libunwind .so at runtime without setting\n"
					"# LD_LIBRARY_PATH. <CFGDIR> resolves to this bin directory. Delete this file to opt out.\n"
					"-Wl,-rpath,<CFGDIR>/../lib/{}\n", runtimeDir.filename().string()) };

				bool wroteAny{ false };

				for (char const* name : { "clang.cfg", "clang++.cfg" })
				{
					fs::path const cfgPath{ binDir / name };
					std::ofstream file{ cfgPath };

					if (file.is_open())
					{
						file << content;
						wroteAny = true;
					}
					else
					{
						gLog.Warning(Tge::Logging::ETarget::File, "OnPostInstall: failed to write {}", cfgPath.string());
					}
				}

				if (wroteAny)
				{
					m_unitLog.Info(Tge::Logging::ETarget::Listeners, std::format(
						"Wrote rpath config (clang.cfg, clang++.cfg) -> programs built with this clang find the "
						"bundled runtimes (lib/{}) without LD_LIBRARY_PATH", runtimeDir.filename().string()));
				}
			}
			else
			{
				gLog.Warning(Tge::Logging::ETarget::File,
					"OnPostInstall: bin directory missing, cannot write rpath config: {}", binDir.string());
			}
		}
		else
		{
			m_unitLog.Info(Tge::Logging::ETarget::Listeners,
				"No shared runtime libraries installed; skipping rpath config");
		}
	}
}

} // namespace Ctrn
