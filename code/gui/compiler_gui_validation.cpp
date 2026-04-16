#include "gui/compiler_gui.hpp"
#include "build/compiler_registry.hpp"
#include "build/compiler_validation.hpp"
#include "dependency/dependency_manager.hpp"
#include "common/common.hpp"
#include <algorithm>
#include <filesystem>

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::AreRequiredDependenciesAvailable(ECompilerKind kind) const
{
	auto allDeps = g_dependencyManager.GetAllDependencies();
	bool allPresent{ true };

	for (auto* dep : allDeps)
	{
		bool const required{ kind == ECompilerKind::Gcc ? dep->requiredForGcc : dep->requiredForClang };

		if (required && dep->status == EDependencyStatus::Missing)
		{
			allPresent = false;
		}
	}

	return allPresent;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::IsCompilerValid(std::string_view compilerPath) const
{
	return !compilerPath.empty() &&
	       std::filesystem::exists(compilerPath) &&
	       !HasProblematicPathCharacters(compilerPath);
}

//////////////////////////////////////////////////////////////////////////
SCompiler CCompilerGUI::GetActualSCompilerForTab(SCompilerTab const& tab) const
{
	SCompiler result;

	if (tab.hostCompiler.empty())
	{
		result = GetEffectiveHostSCompiler();
	}
	else
	{
		result = CompilerFromPath(tab.hostCompiler);
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::IsTabCompilerValid(SCompilerTab const& tab) const
{
	std::string const path{ GetActualSCompilerForTab(tab).path };
	return IsCompilerValid(path) && g_compilerRegistry.Contains(path);
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GetInvalidCompilerMessage(SCompilerTab const& tab) const
{
	std::string const compilerPath{ GetActualSCompilerForTab(tab).path };
	std::string result;

	if (compilerPath.empty())
	{
		result = "No compiler selected — choose one from the dropdown.";
	}
	else if (!g_compilerRegistry.Contains(compilerPath))
	{
		result = "Not in registry: " + compilerPath
			+ "\nSelect a different compiler or re-add this one via the compiler dropdown.";
	}
	else if (!std::filesystem::exists(compilerPath))
	{
		result = "Binary missing: " + compilerPath
			+ "\nThe file no longer exists. Remove this entry and add the correct compiler.";
	}
	else if (HasProblematicPathCharacters(compilerPath))
	{
		result = "Path contains shell-problematic characters: " + compilerPath;
	}
	else
	{
		result = "Invalid compiler: " + compilerPath;
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GetActualCompilerForTab(SCompilerTab const& tab) const
{
	return GetActualSCompilerForTab(tab).path;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::WouldInstallToSystemDirectory(SCompilerTab const& tab) const
{
	std::string folderName{ tab.folderName.empty() ? GetFolderNameFromCompilerName(tab.name) : tab.folderName };
	std::string plannedInstallPath{ GetResolvedInstallPath() + "/" + folderName };

	std::error_code ec;
	std::filesystem::path absolutePath = std::filesystem::absolute(plannedInstallPath, ec);
	bool isSystemDir{ false };

	if (!ec)
	{
		std::filesystem::path checkPath = absolutePath;

		while (!isSystemDir && !checkPath.empty() && checkPath != checkPath.root_path())
		{
			if (IsSystemDirectory(checkPath.string()))
			{
				isSystemDir = true;
			}
			else
			{
				checkPath = checkPath.parent_path();
			}
		}
	}
	else
	{
		isSystemDir = IsSystemDirectory(plannedInstallPath);
	}

	return isSystemDir;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GetMissingDependenciesMessage() const
{
	auto allDeps = g_dependencyManager.GetAllDependencies();
	std::vector<std::string> missingDeps;

	for (auto* dep : allDeps)
	{
		if (dep->IsRequired() && dep->status == EDependencyStatus::Missing)
		{
			missingDeps.push_back(dep->name);
		}
	}

	std::string message;

	if (!missingDeps.empty())
	{
		message = "Missing required dependencies:\n";

		for (auto const& depName : missingDeps)
		{
			message += "- " + depName + "\n";
		}

		message += "\nPlease install them via the Dependencies tab.";
	}

	return message;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GetFolderNameFromCompilerName(std::string_view compilerName) const
{
	std::string result{compilerName};
	std::transform(result.begin(), result.end(), result.begin(), ::tolower);
	std::replace(result.begin(), result.end(), ' ', '_');
	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::GetResolvedInstallPath() const
{
	std::filesystem::path installPath(g_buildSettings.installDirectory);
	std::string result;

	if (installPath.is_absolute())
	{
		result = installPath.string();
	}
	else
	{
		std::filesystem::path resolvedPath = std::filesystem::path(g_dataDir) / installPath;
		result = resolvedPath.string();
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
std::string CCompilerGUI::FindLldForTab(SCompilerTab const& tab) const
{
	std::string result;

	if (tab.kind == ECompilerKind::Clang && tab.clangSettings.linker.value == "lld")
	{
		if (!tab.clangSettings.lldOverridePath.value.empty())
		{
			if (std::filesystem::exists(tab.clangSettings.lldOverridePath.value))
			{
				result = tab.clangSettings.lldOverridePath.value;
			}
		}
		else
		{
			std::string const actualCompiler{ GetActualCompilerForTab(tab) };
			std::error_code ec;
			std::filesystem::path const resolved{ std::filesystem::canonical(actualCompiler, ec) };
			std::filesystem::path const compilerDir{ ec
				? std::filesystem::path{ actualCompiler }.parent_path()
				: resolved.parent_path() };

			std::filesystem::path const lldInCompilerDir{ compilerDir / "ld.lld" };

			if (std::filesystem::exists(lldInCompilerDir))
			{
				result = lldInCompilerDir.string();
			}
			else
			{
				char const* pathEnv{ std::getenv("PATH") };

				if (pathEnv != nullptr)
				{
					std::string_view pathStr{ pathEnv };
					size_t start{ 0 };

					while (result.empty())
					{
						size_t const colon{ pathStr.find(':', start) };
						std::string_view const dir{ colon == std::string_view::npos
							? pathStr.substr(start)
							: pathStr.substr(start, colon - start) };

						if (!dir.empty())
						{
							std::filesystem::path const candidate{ std::filesystem::path{ dir } / "ld.lld" };

							if (std::filesystem::exists(candidate))
							{
								result = candidate.string();
							}
						}

						if (colon == std::string_view::npos)
						{
							break;
						}

						start = colon + 1;
					}
				}
			}
		}
	}

	return result;
}

//////////////////////////////////////////////////////////////////////////
bool CCompilerGUI::IsLldAvailableForTab(SCompilerTab const& tab) const
{
	return tab.kind != ECompilerKind::Clang
	    || tab.clangSettings.linker.value != "lld"
	    || !FindLldForTab(tab).empty();
}

//////////////////////////////////////////////////////////////////////////
ECompilerValidationResult CCompilerGUI::ValidateCompilerForBuild(SCompilerTab const& tab) const
{
	std::string actualCompiler{ GetActualCompilerForTab(tab) };
	ECompilerValidationResult result{ ECompilerValidationResult::CompilerPathEmpty };

	if (!actualCompiler.empty())
	{
		std::string folderName{ tab.folderName.empty() ? GetFolderNameFromCompilerName(tab.name) : tab.folderName };
		std::string plannedInstallPath{ GetResolvedInstallPath() + "/" + folderName };
		result = Ctrn::ValidateCompilerForBuild(actualCompiler, plannedInstallPath);
	}

	return result;
}
} // namespace Ctrn
