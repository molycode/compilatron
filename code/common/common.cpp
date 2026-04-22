#include "common/common.hpp"
#include "gui/preset_manager.hpp"
#include "dependency/dependency_manager.hpp"
#include "dependency/dependency_window.hpp"

#include <cstdlib>
#include <filesystem>

namespace Ctrn
{
// Global singleton instance definitions - static objects with automatic initialization
SCpuInfo g_cpuInfo{};
SBuildSettings g_buildSettings;
CPresetManager g_presetManager;
CDependencyManager g_dependencyManager;
CDependencyWindow g_dependencyWindow;
CCompilerRegistry g_compilerRegistry;

// Root directory — the executable's directory
std::string g_rootDir;

// Data directory — namespaced app data directory (g_rootDir/compilatron)
std::string g_dataDir;

// Active builds tracking - thread-safe list of compiler install paths currently being built
std::vector<std::string> g_activeBuilds;
std::mutex g_activeBuildsMutex;

// Main window state globals - initialized with default values
std::atomic<int> g_mainWindowPosX{100};
std::atomic<int> g_mainWindowPosY{100};
std::atomic<int> g_mainWindowWidth{1600};
std::atomic<int> g_mainWindowHeight{1050};

// Window resize request flag
std::atomic<bool> g_mainWindowNeedsResize{false};

std::string GetEffectiveHostCompiler()
{
	std::string result{ g_buildSettings.globalHostCompiler };

	if (result.empty())
	{
		std::string clangFallback;

		for (auto const& compiler : g_compilerRegistry.GetCompilers())
		{
			if (result.empty() && compiler.kind == ECompilerKind::Gcc)
			{
				result = compiler.path;
			}

			if (clangFallback.empty() && compiler.kind == ECompilerKind::Clang)
			{
				clangFallback = compiler.path;
			}
		}

		if (result.empty())
		{
			result = clangFallback;
		}
	}

	return result;
}

SCompiler CompilerFromPath(std::string_view path)
{
	SCompiler result;
	result.path = std::string{ path };

	std::string const filename{ std::filesystem::path(path).filename().string() };
	result.kind               = filename.find("clang") != std::string::npos
	                            ? ECompilerKind::Clang : ECompilerKind::Gcc;
	result.displayName        = filename;
	result.hasProblematicPath = HasProblematicPathCharacters(path);
	result.isRemovable        = false;

	return result;
}

SCompiler GetEffectiveHostSCompiler()
{
	SCompiler result;

	if (g_buildSettings.globalHostCompiler.empty())
	{
		SCompiler clangFallback;

		for (auto const& compiler : g_compilerRegistry.GetCompilers())
		{
			if (result.path.empty() && compiler.kind == ECompilerKind::Gcc)
			{
				result = compiler;
			}

			if (clangFallback.path.empty() && compiler.kind == ECompilerKind::Clang)
			{
				clangFallback = compiler;
			}
		}

		if (result.path.empty())
		{
			result = clangFallback;
		}
	}
	else
	{
		result = CompilerFromPath(g_buildSettings.globalHostCompiler);
	}

	return result;
}

std::vector<std::string> GetSystemBinPaths()
{
	return { "/usr/bin", "/usr/local/bin", "/bin", "/usr/sbin", "/sbin" };
}

std::vector<std::string> GetUserBinPaths()
{
	std::vector<std::string> paths;
	char const* home{ getenv("HOME") };

	if (home != nullptr)
	{
		paths.push_back(std::string(home) + "/.local/bin");
		paths.push_back(std::string(home) + "/bin");
	}

	return paths;
}

// Check if a path contains characters problematic for build systems (CMake, autoconf, shell)
bool HasProblematicPathCharacters(std::string_view path)
{
	// Check for common shell-problematic characters that cause issues with:
	// - CMake/autoconf configure scripts
	// - Shell command execution
	// - Makefile/Ninja build files
	char const* problematicChars = " ()[]{}$`'\"\\|&;<>*?!~#";

	return path.find_first_of(problematicChars) != std::string_view::npos;
}
} // namespace Ctrn
