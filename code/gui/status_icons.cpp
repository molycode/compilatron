#include "gui/status_icons.hpp"

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
SStatusIcon GetStatusIcon(ECompilerStatus status)
{
	switch (status)
	{
		case ECompilerStatus::NotStarted: return { ICON_FA_CIRCLE_QUESTION, ImVec4(0.5f, 0.5f, 0.5f, 1.0f) };
		case ECompilerStatus::Cloning:    return { ICON_FA_CIRCLE_ARROW_DOWN, ImVec4(0.4f, 0.7f, 1.0f, 1.0f) };
		case ECompilerStatus::Waiting:    return { ICON_FA_CLOCK, ImVec4(1.0f, 0.6f, 0.0f, 1.0f) };
		case ECompilerStatus::Building:   return { ICON_FA_GEAR, ImVec4(1.0f, 0.85f, 0.0f, 1.0f) };
		case ECompilerStatus::Success:    return { ICON_FA_CIRCLE_CHECK, ImVec4(0.2f, 0.8f, 0.2f, 1.0f) };
		case ECompilerStatus::Failed:
		case ECompilerStatus::Aborted:    return { ICON_FA_CIRCLE_XMARK, ImVec4(1.0f, 0.3f, 0.3f, 1.0f) };
		default:                          return { ICON_FA_CIRCLE_QUESTION, ImVec4(0.5f, 0.5f, 0.5f, 1.0f) };
	}
}

//////////////////////////////////////////////////////////////////////////
SStatusIcon GetDependencyIcon(EDependencyStatus status)
{
	switch (status)
	{
		case EDependencyStatus::Available:     return { ICON_FA_CIRCLE_CHECK, ImVec4(0.2f, 0.8f, 0.2f, 1.0f) };
		case EDependencyStatus::Missing:       return { ICON_FA_CIRCLE_XMARK, ImVec4(1.0f, 0.3f, 0.3f, 1.0f) };
		case EDependencyStatus::MultipleFound: return { ICON_FA_TRIANGLE_EXCLAMATION, ImVec4(1.0f, 0.6f, 0.0f, 1.0f) };
		case EDependencyStatus::Outdated:      return { ICON_FA_TRIANGLE_EXCLAMATION, ImVec4(1.0f, 0.5f, 0.0f, 1.0f) };
		case EDependencyStatus::Broken:        return { ICON_FA_CIRCLE_XMARK, ImVec4(1.0f, 0.3f, 0.0f, 1.0f) };
		default:                               return { ICON_FA_CIRCLE_QUESTION, ImVec4(0.5f, 0.5f, 0.5f, 1.0f) };
	}
}
} // namespace Ctrn
