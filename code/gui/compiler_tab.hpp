#pragma once

#include "build/clang_settings.hpp"
#include "build/compiler_kind.hpp"
#include "build/gcc_settings.hpp"
#include <cstdint>
#include <string>

namespace Ctrn
{
struct SCompilerTab final
{
	std::string name;              // Selected version/branch (e.g., "main", "llvmorg-21.1.1"); empty until user picks one
	ECompilerKind kind{ ECompilerKind::Gcc };
	uint16_t id{ 0 };             // Unique numeric ID — assigned once at tab creation, never 0
	bool isOpen{ true };          // Whether tab should be shown
	bool selectOnOpen{ false };   // Auto-focus this tab on its first rendered frame

	std::string folderName;
	bool keepDependencies{ true };
	bool keepSources{ true };
	int numJobs{ 0 };         // 0 = use default based on CPU
	std::string hostCompiler;  // Override compiler for this tab (empty = use global)

	SClangSettings clangSettings;  // Used when kind == ECompilerKind::Clang
	SGccSettings gccSettings;      // Used when kind == ECompilerKind::Gcc

	// ImGui input buffers are per-tab to prevent focus loss on re-render
	char folderNameBuffer[256]{};

	// Only updated when not actively typing, to avoid focus loss during input
	std::string tabDisplayName;
	std::string tabLabel;        // Cached: tabDisplayName + "###" + id — rebuilt when either changes

	// Cached ImGui ID strings — stable after tab creation, never rebuilt per frame
	std::string idTabCompiler;   // "##TabCompiler" + id
	std::string idDeleteSources; // "Delete Sources##" + id
	std::string idSourcesPopup;  // "Confirm Delete Sources##" + id
	std::string idDeleteBuild;   // "Delete Build##" + id
	std::string idBuildPopup;    // "Confirm Delete Build##" + id
	std::string idShowCommand;   // "Show Command##" + id
	std::string idAdvanced;      // "Advanced Configuration##" + id
	std::string idCopyLog;       // "Copy Log##" + id
	std::string idSaveLog;       // "Save Log##" + id
	std::string idCompilerLog;   // "CompilerLog##" + id
};
} // namespace Ctrn
