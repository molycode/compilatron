#include "build/gcc_unit.hpp"
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
std::string BuildGccPath(std::string_view dependenciesDir)
{
	std::string result{ dependenciesDir };

	// Honour user-selected paths for tools GCC's build system invokes directly.
	// cmake and ninja are Clang-specific and must not appear here.
	static constexpr std::array<std::string_view, 4> gccTools{
		"bison", "flex", "perl", "make"
	};

	for (std::string_view const tool : gccTools)
	{
		std::string const path{ g_dependencyManager.GetSelectedPath(tool) };

		if (!path.empty())
		{
			result += ':';
			result += std::filesystem::path(path).parent_path().string();
		}
	}

	return result;
}
}

//////////////////////////////////////////////////////////////////////////
CGccUnit::CGccUnit(std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig)
	: CCompilerUnit(ECompilerKind::Gcc, std::move(displayName), globalSettings, buildConfig)
{
	m_unitLog.Info(Tge::Logging::ETarget::File, std::format("Created GCC unit: version={}, folder={}", GetName(), GetFolderName()));
}

//////////////////////////////////////////////////////////////////////////
SGccSettings const& CGccUnit::GetGccConfig() const
{
	return m_gccSettings;
}

//////////////////////////////////////////////////////////////////////////
std::string CGccUnit::GetSourcePath() const
{
	return GetBuildConfig().sourcesDir;
}

//////////////////////////////////////////////////////////////////////////
std::string CGccUnit::GetBuildPath() const
{
	return GetBuildConfig().buildDir;
}

//////////////////////////////////////////////////////////////////////////
std::string CGccUnit::GetInstallPath() const
{
	namespace fs = std::filesystem;
	fs::path installPath = fs::path(g_dataDir) / GetGlobalSettings().installDirectory / GetFolderName();
	return installPath.string();
}

//////////////////////////////////////////////////////////////////////////
std::string CGccUnit::GetDefaultSourceUrl() const
{
	return std::string{ GccSourceUrl } + ".git";
}

//////////////////////////////////////////////////////////////////////////
std::vector<std::string> CGccUnit::GetRequiredSourcePaths() const
{
	return {
		"configure",
		"Makefile.in",
		"gcc/configure.ac",
		"gcc/Makefile.in",
		"libgcc",
		"include"
	};
}

//////////////////////////////////////////////////////////////////////////
std::string CGccUnit::GenerateInstallCommand() const
{
	std::ostringstream cmd;
	cmd << "PATH=" << BuildGccPath(GetBuildConfig().dependenciesDir) << ":$PATH && cd " << GetBuildPath() << " && make install";
	return cmd.str();
}

//////////////////////////////////////////////////////////////////////////
bool CGccUnit::PostDownloadHook(std::string_view sourcesDir)
{
	std::string const prereqCmd{ "cd \"" + std::string{sourcesDir} + "\" && contrib/download_prerequisites" };
	m_unitLog.Info(Tge::Logging::ETarget::Console, "Downloading GCC prerequisites: " + prereqCmd);

	bool const success{ ExecuteCommand(prereqCmd) };

	if (!success)
	{
		m_unitLog.Error(Tge::Logging::ETarget::Console, "Failed to download GCC prerequisites");
		ReportProgress(ECompilerStatus::Failed, 0.2f, "Prerequisites download failed");
	}
	else
	{
		m_unitLog.Info(Tge::Logging::ETarget::Console, "GCC prerequisites downloaded successfully");
	}

	return success;
}

//////////////////////////////////////////////////////////////////////////
std::expected<std::string, std::string> CGccUnit::GetCompilerConfigureFlags() const
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
			gLog.Info(Tge::Logging::ETarget::File, "CGccUnit: Using compilers: CC='{}', CXX='{}'", cCompiler, cxxCompiler);
			result = std::string{"CC=\""} + cCompiler + "\" CXX=\"" + cxxCompiler + "\"";
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::expected<std::string, std::string> CGccUnit::GenerateConfigureCommand() const
{
	auto const& config = GetGccConfig();
	auto const& buildConfig = GetBuildConfig();

	auto const compilerFlags = GetCompilerConfigureFlags();
	std::expected<std::string, std::string> result;

	if (compilerFlags)
	{
		std::ostringstream cmd;
		cmd << "cd \"" << GetBuildPath() << "\" && ";

		if (!buildConfig.dependenciesDir.empty())
		{
			cmd << "PATH=\"" << BuildGccPath(buildConfig.dependenciesDir) << ":$PATH\" ";
		}

		if (!compilerFlags.value().empty())
		{
			cmd << compilerFlags.value() << " ";
		}

		cmd << "\"" << GetSourcePath() << "/configure\"";

		cmd << " --prefix=\"" << GetInstallPath() << "\"";

		// Prevent sub-configure cache races under parallel make
		cmd << " --cache-file=/dev/null";

		if (!config.enabledLanguages.value.empty())
		{
			cmd << " --enable-languages=" << config.enabledLanguages;
		}

		if (config.enableLto)
		{
			cmd << " --enable-lto";
		}

		if (config.enableBootstrap)
		{
			cmd << " --enable-bootstrap";
		}
		else
		{
			cmd << " --disable-bootstrap";
		}

		if (config.enableShared)
		{
			cmd << " --enable-shared";
		}
		else
		{
			cmd << " --disable-shared";
		}

		if (config.useSystemZlib)
		{
			cmd << " --with-system-zlib";
		}

		if (config.disableWerror)
		{
			cmd << " --disable-werror";
		}

		if (config.enableChecking)
		{
			cmd << " --enable-checking=" << config.checkingLevel;
		}
		else
		{
			cmd << " --disable-checking";
		}

		if (config.disableMultilib)
		{
			cmd << " --disable-multilib";
		}
		else
		{
			cmd << " --enable-multilib";
		}

		if (config.modernCppAbi)
		{
			cmd << " --with-default-libstdcxx-abi=new";
		}
		else
		{
			cmd << " --with-default-libstdcxx-abi=gcc4-compatible";
		}

		if (config.posixThreads)
		{
			cmd << " --enable-threads=posix";
		}

		if (config.enablePlugin)
		{
			cmd << " --enable-plugin";
		}
		else
		{
			cmd << " --disable-plugin";
		}

		if (config.enablePie)
		{
			cmd << " --enable-default-pie";
		}
		else
		{
			cmd << " --disable-default-pie";
		}

		if (config.enableBuildId)
		{
			cmd << " --enable-linker-build-id";
		}
		else
		{
			cmd << " --disable-linker-build-id";
		}

		if (config.enableGold)
		{
			cmd << " --enable-gold";
		}

		if (!config.withArch.value.empty())
		{
			cmd << " --with-arch=" << config.withArch;
		}

		if (!config.withTune.value.empty())
		{
			cmd << " --with-tune=" << config.withTune;
		}

		if (!config.withSysroot.value.empty())
		{
			cmd << " --with-sysroot=" << config.withSysroot;
		}

		std::string optLevel{};

		switch (config.optimizationLevel)
		{
			case EOptimizationLevel::O0:    optLevel = "-O0"; break;
			case EOptimizationLevel::O1:    optLevel = "-O1"; break;
			case EOptimizationLevel::O2:    optLevel = "-O2"; break;
			case EOptimizationLevel::O3:    optLevel = "-O3"; break;
			case EOptimizationLevel::Os:    optLevel = "-Os"; break;
			case EOptimizationLevel::Ofast: optLevel = "-Ofast"; break;
		}

		if (config.generateDebugSymbols)
		{
			cmd << " --enable-debug";
		}

		std::string cflags{ optLevel };

		if (!config.customCFlags.value.empty())
		{
			if (!cflags.empty())
			{
				cflags += " ";
			}

			cflags += config.customCFlags.value;
		}

		if (!cflags.empty())
		{
			cmd << " CFLAGS=\"" << cflags << "\"";
		}

		std::string cxxflags{ optLevel };

		if (!config.customCxxFlags.value.empty())
		{
			if (!cxxflags.empty())
			{
				cxxflags += " ";
			}

			cxxflags += config.customCxxFlags.value;
		}

		if (!cxxflags.empty())
		{
			cmd << " CXXFLAGS=\"" << cxxflags << "\"";
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
std::function<void(std::string_view)> CGccUnit::CreateBuildObserver()
{
	// GCC builds with make, which doesn't emit Ninja-style [X/Y] lines.
	// Instead, parse well-known phase markers from make's recursive output.
	// Progress only advances — never goes backward — to handle parallel make
	// interleaving the same phase name multiple times.
	struct SGccPhase
	{
		std::string_view pattern;
		float            progress;
		std::string_view label;
	};

	// Bootstrap build phases — GCC prints "Configuring stage N in <dir>" at each stage boundary.
	static constexpr std::array<SGccPhase, 4> bootstrapPhases{ {
		{ "Configuring stage 1 in", 0.15f, "Bootstrap stage 1" },
		{ "Configuring stage 2 in", 0.45f, "Bootstrap stage 2" },
		{ "Configuring stage 3 in", 0.72f, "Bootstrap stage 3" },
		{ "Comparing stages 2 and 3", 0.90f, "Verifying bootstrap" },
	} };

	// Non-bootstrap build phases — GCC's recursive make prints "Making all in <subdir>".
	// Target libraries appear as "Making all in <triple>/libXXX" so we match the suffix.
	static constexpr std::array<SGccPhase, 7> buildPhases{ {
		{ "Making all in libiberty",   0.15f, "Building libiberty" },
		{ "Making all in fixincludes", 0.30f, "Building fixincludes" },
		{ "Making all in gcc",         0.45f, "Building GCC" },
		{ "/libgcc",                   0.65f, "Building libgcc" },
		{ "/libstdc++",                0.78f, "Building libstdc++" },
		{ "/libsanitizer",             0.85f, "Building libsanitizer" },
		{ "/libatomic",                0.90f, "Building libatomic" },
	} };

	return [this, lastProgress = 0.0f](std::string_view line) mutable
	{
		bool found{ false };

		for (auto const& phase : bootstrapPhases)
		{
			if (!found && phase.progress > lastProgress && line.find(phase.pattern) != std::string_view::npos)
			{
				lastProgress = phase.progress;
				ReportProgress(ECompilerStatus::Building, phase.progress, std::string{ phase.label });
				found = true;
			}
		}

		for (auto const& phase : buildPhases)
		{
			if (!found && phase.progress > lastProgress && line.find(phase.pattern) != std::string_view::npos)
			{
				lastProgress = phase.progress;
				ReportProgress(ECompilerStatus::Building, phase.progress, std::string{ phase.label });
				found = true;
			}
		}
	};
}

//////////////////////////////////////////////////////////////////////////
std::string CGccUnit::GenerateBuildCommand() const
{
	auto const& buildConfig = GetBuildConfig();

	std::ostringstream cmd;
	cmd << "PATH=" << BuildGccPath(buildConfig.dependenciesDir) << ":$PATH && cd \"" << GetBuildPath() << "\" && make -j" << buildConfig.numJobs;

	return cmd.str();
}

} // namespace Ctrn
