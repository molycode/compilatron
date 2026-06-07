#pragma once

#include "build/compiler_kind.hpp"
#include <cstdint>
#include <string>

namespace Ctrn
{
// Transient per-tab view state. All persistable build data lives on the matching
// SCompilerEntry in g_buildSettings.compilerEntries (the single source of truth),
// resolved by id via CCompilerGUI::EntryFor(tab). This struct holds only UI state.
struct SCompilerTab final
{
	// Immutable mirror of the entry's compilerType, set once at tab creation and never
	// rewritten, so it cannot drift. Kept here to avoid string compares in per-frame UI.
	ECompilerKind kind{ ECompilerKind::Gcc };
	uint16_t id{ 0 };             // Links tab to its SCompilerEntry and CCompilerUnit — assigned once, never 0
	bool isOpen{ true };          // Whether tab should be shown
	bool selectOnOpen{ false };   // Auto-focus this tab on its first rendered frame

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
