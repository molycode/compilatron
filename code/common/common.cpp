#include "common/common.hpp"
#include "gui/preset_manager.hpp"
#include "dependency/dependency_manager.hpp"
#include "dependency/dependency_window.hpp"

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
		for (auto const& compiler : g_compilerRegistry.GetCompilers())
		{
			if (result.empty() && compiler.kind == ECompilerKind::Gcc)
			{
				result = compiler.path;
			}
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
		for (auto const& compiler : g_compilerRegistry.GetCompilers())
		{
			if (result.path.empty() && compiler.kind == ECompilerKind::Gcc)
			{
				result = compiler;
			}
		}
	}
	else
	{
		result = CompilerFromPath(g_buildSettings.globalHostCompiler);
	}

	return result;
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
