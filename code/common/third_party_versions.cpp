#include "common/third_party_versions.hpp"

#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wnontrivial-memcall"
#endif
#include <imgui.h>
#if defined(__clang__) && __clang_major__ >= 10
#pragma clang diagnostic pop
#endif
#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

// Stringize a library's integer version macros into a compile-time "MAJOR.MINOR.PATCH" literal.
#define CTRN_STRINGIZE_IMPL(x) #x
#define CTRN_STRINGIZE(x) CTRN_STRINGIZE_IMPL(x)

namespace Ctrn
{
std::string_view GetImGuiVersion()
{
	return IMGUI_VERSION;
}

//////////////////////////////////////////////////////////////////////////
std::string_view GetGlfwVersion()
{
	return CTRN_STRINGIZE(GLFW_VERSION_MAJOR) "." CTRN_STRINGIZE(GLFW_VERSION_MINOR) "." CTRN_STRINGIZE(GLFW_VERSION_REVISION);
}

//////////////////////////////////////////////////////////////////////////
std::string_view GetJsonVersion()
{
	return CTRN_STRINGIZE(NLOHMANN_JSON_VERSION_MAJOR) "." CTRN_STRINGIZE(NLOHMANN_JSON_VERSION_MINOR) "." CTRN_STRINGIZE(NLOHMANN_JSON_VERSION_PATCH);
}
} // namespace Ctrn

#undef CTRN_STRINGIZE
#undef CTRN_STRINGIZE_IMPL
