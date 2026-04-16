#include "build/compiler_validation.hpp"
#include "common/loggers.hpp"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <sstream>

namespace fs = std::filesystem;

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
ECompilerValidationResult ValidateCompilerForBuild(std::string_view cppCompilerPath, std::string_view targetInstallPath)
{
	ECompilerValidationResult result{ ECompilerValidationResult::Valid };

	if (cppCompilerPath.empty())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "ValidateCompilerForBuild: cppCompilerPath is empty");
		result = ECompilerValidationResult::CompilerPathEmpty;
	}
	else if (targetInstallPath.empty())
	{
		gLog.Warning(Tge::Logging::ETarget::File, "ValidateCompilerForBuild: targetInstallPath is empty");
		result = ECompilerValidationResult::TargetPathEmpty;
	}
	else if (!fs::exists(cppCompilerPath))
	{
		// Valid: the compiler may not exist yet (first-time build)
		gLog.Info(Tge::Logging::ETarget::File, "ValidateCompilerForBuild: compiler path does not exist yet: {} (OK for first-time builds)", cppCompilerPath);
		result = ECompilerValidationResult::CompilerNotPresent;
	}
	else
	{
		// exe → binDir → installDir (e.g. /usr/bin/g++-13 → /usr/bin → /usr)
		fs::path const compilerInstallDir{ fs::path(cppCompilerPath).parent_path().parent_path() };
		fs::path const targetPath{ fs::path(targetInstallPath).lexically_normal() };

		if (targetPath == compilerInstallDir)
		{
			gLog.Warning(Tge::Logging::ETarget::File,
				"ValidateCompilerForBuild: self-compilation detected: {} would overwrite {}",
				targetPath.string(), compilerInstallDir.string());
			result = ECompilerValidationResult::CompilerSelfOverwrite;
		}
		else
		{
			// Also check if target is a parent of the compiler install dir
			std::string const targetStr{ targetPath.string() };
			std::string const compilerStr{ compilerInstallDir.string() };

			if (compilerStr.find(targetStr) == 0 &&
			    compilerStr.length() > targetStr.length() &&
			    compilerStr[targetStr.length()] == '/')
			{
				gLog.Warning(Tge::Logging::ETarget::File,
					"ValidateCompilerForBuild: self-compilation detected (parent path): {} would contain and overwrite {}",
					targetStr, compilerStr);
				result = ECompilerValidationResult::CompilerSelfOverwrite;
			}
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string GetValidationErrorMessage(ECompilerValidationResult result, std::string_view compilerPath, std::string_view targetPath)
{
	switch (result)
	{
		case ECompilerValidationResult::Valid:
			return "Compiler is valid";

		case ECompilerValidationResult::CompilerNotPresent:
			return std::format("Compiler does not exist yet: {} (this is OK for first-time builds)", compilerPath);

		case ECompilerValidationResult::CompilerNotDirectory:
			return std::format("Compiler path is not a directory: {}", compilerPath);

		case ECompilerValidationResult::CompilerSelfOverwrite:
			return std::format("Self-compilation detected: building to {} would overwrite the compiler being used at {}", targetPath, compilerPath);

		case ECompilerValidationResult::CompilerPathEmpty:
			return "Compiler path is empty";

		case ECompilerValidationResult::TargetPathEmpty:
			return "Target installation path is empty";
	}

	return "Unknown validation error";
}

//////////////////////////////////////////////////////////////////////////
bool IsSystemDirectory(std::string_view directory)
{
	if (directory == "/usr" || directory.find("/usr/") == 0 ||
	    directory == "/bin" || directory.find("/bin/") == 0 ||
	    directory == "/sbin" || directory.find("/sbin/") == 0 ||
	    directory == "/opt" || directory.find("/opt/") == 0 ||
	    directory == "/snap" || directory.find("/snap/") == 0)
	{
		return true;
	}

	char const* pathEnv{ getenv("PATH") };
	bool found{ false };

	if (pathEnv != nullptr)
	{
		std::istringstream stream{ std::string{pathEnv} };
		std::string pathDir;

		while (std::getline(stream, pathDir, ':') && !found)
		{
			if (pathDir == directory)
			{
				found = true;
			}
		}
	}

	return found;
}
} // namespace Ctrn
