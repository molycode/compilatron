#pragma once

#include <string>
#include <string_view>

namespace Ctrn
{
enum class ECompilerValidationResult
{
	Valid,                  // Compiler is valid and can be used
	CompilerNotPresent,     // Compiler path doesn't exist (may be first-time build)
	CompilerNotDirectory,   // Unused — kept for completeness
	CompilerSelfOverwrite,  // Would overwrite the compiler being used (self-compilation)
	CompilerPathEmpty,      // Empty compiler path provided
	TargetPathEmpty         // Empty target installation path provided
};

// Validates whether the given C++ compiler executable can be used to build to targetInstallPath.
// cppCompilerPath must be a full path to the C++ compiler executable (e.g. /usr/bin/g++-13).
[[nodiscard]] ECompilerValidationResult ValidateCompilerForBuild(std::string_view cppCompilerPath, std::string_view targetInstallPath);

std::string GetValidationErrorMessage(ECompilerValidationResult result, std::string_view compilerPath = "", std::string_view targetPath = "");

// Returns true if the directory is a system-wide path (e.g. /usr/bin, /opt, /snap).
[[nodiscard]] bool IsSystemDirectory(std::string_view directory);
} // namespace Ctrn
