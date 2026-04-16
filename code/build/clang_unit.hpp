#pragma once

#include "build/compiler_unit.hpp"
#include "build/clang_settings.hpp"
#include <expected>
#include <vector>
#include <string>

namespace Ctrn
{
class CClangUnit final : public CCompilerUnit
{
public:

	explicit CClangUnit(std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig);

	void SetClangSettings(SClangSettings const& settings) { m_clangSettings = settings; }

protected:

	std::expected<std::string, std::string> GenerateConfigureCommand() const override;
	std::string              GenerateBuildCommand()   const override;
	std::string              GetSourcePath()          const override;
	std::string              GetBuildPath()           const override;
	std::string              GetInstallPath()         const override;
	std::string              GetDefaultSourceUrl()    const override;
	std::vector<std::string> GetRequiredSourcePaths() const override;
	std::string              GenerateInstallCommand() const override;

private:

	SClangSettings const& GetClangConfig() const;
	std::expected<std::string, std::string> GetCompilerCMakeFlags() const;

	// Compiler-specific configuration from GUI
	SClangSettings m_clangSettings;
};
} // namespace Ctrn
