#pragma once

#include "build/compiler_unit.hpp"
#include "dependency/dependency_unit.hpp"
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#include <imgui.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic pop
#endif
#include <IconsFontAwesome6.h>

namespace Ctrn
{
struct SStatusIcon final
{
	char const* glyph;
	ImVec4      color;
};

SStatusIcon GetStatusIcon(ECompilerStatus status);
SStatusIcon GetDependencyIcon(EDependencyStatus status);
} // namespace Ctrn
