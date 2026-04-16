#pragma once

#include "build/compiler_unit.hpp"
#include "build/gcc_settings.hpp"
#include <expected>
#include <functional>
#include <vector>
#include <string>
#include <string_view>

namespace Ctrn
{
class CGccUnit final : public CCompilerUnit
{
public:

	explicit CGccUnit(std::string displayName, SBuildSettings const& globalSettings, SCompilerBuildConfig const& buildConfig);

	void SetGccSettings(SGccSettings const& settings) { m_gccSettings = settings; }

protected:

	std::expected<std::string, std::string> GenerateConfigureCommand() const override;
	std::string              GenerateBuildCommand()   const override;

	std::function<void(std::string_view)> CreateBuildObserver() override;
	std::string              GetSourcePath()          const override;
	std::string              GetBuildPath()           const override;
	std::string              GetInstallPath()         const override;
	std::string              GetDefaultSourceUrl()    const override;
	std::vector<std::string> GetRequiredSourcePaths() const override;
	std::string              GenerateInstallCommand() const override;
	bool                     PostDownloadHook(std::string_view sourcesDir) override;

private:

	SGccSettings const& GetGccConfig() const;
	std::expected<std::string, std::string> GetCompilerConfigureFlags() const;

	// Compiler-specific configuration from GUI
	SGccSettings m_gccSettings;
};
} // namespace Ctrn
