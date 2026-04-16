#pragma once

#include "build/compiler_registry.hpp"
#include "common/cpu_info.hpp"
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include <mutex>

namespace Ctrn
{
struct SBuildSettings;
class CPresetManager;
class CDependencyManager;
class CDependencyWindow;

extern SCpuInfo g_cpuInfo;
extern SBuildSettings g_buildSettings;
extern CPresetManager g_presetManager;
extern CDependencyManager g_dependencyManager;
extern CDependencyWindow g_dependencyWindow;

// Root directory — the executable's directory
extern std::string g_rootDir;

// Data directory — namespaced app data directory (g_rootDir/compilatron)
// All app-generated data (logs, config, registry, sources, builds, dependencies) lives here
extern std::string g_dataDir;

// Active builds tracking - thread-safe access to currently building compiler install paths
// Used to prevent deletion of directories that are actively being written to
extern std::vector<std::string> g_activeBuilds;
extern std::mutex g_activeBuildsMutex;

// Main window state globals - thread-safe atomic access
// Updated immediately when user changes window position/size
extern std::atomic<int> g_mainWindowPosX;
extern std::atomic<int> g_mainWindowPosY;
extern std::atomic<int> g_mainWindowWidth;
extern std::atomic<int> g_mainWindowHeight;

// Window resize request flag - set when code needs to resize the main window
extern std::atomic<bool> g_mainWindowNeedsResize;

// Returns the effective global host C++ compiler path.
// Prefers g_buildSettings.globalHostCompiler; falls back to first GCC compiler in the registry.
[[nodiscard]] std::string GetEffectiveHostCompiler();

// Constructs an SCompiler from a bare path string.
// Kind is derived from the filename; version is unknown (empty).
[[nodiscard]] SCompiler CompilerFromPath(std::string_view path);

// Returns the effective global host compiler as SCompiler.
// Prefers g_buildSettings.globalHostCompiler; falls back to first GCC compiler in the registry.
[[nodiscard]] SCompiler GetEffectiveHostSCompiler();

// Utility function: Check if a path contains characters problematic for build systems
// Returns true if path contains spaces, parentheses, or other shell-problematic characters
[[nodiscard]] bool HasProblematicPathCharacters(std::string_view path);
} // namespace Ctrn
