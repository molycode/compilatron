#pragma once

#include <string>

namespace Ctrn
{

enum class EBuildPhase
{
	CleaningPreviousBuild,
	CheckingDependencies,
	InstallingDependencies,
	DownloadingSources,
	ConfiguringCompiler,
	BuildingCompiler,
	InstallingCompiler,
	Completed,
	Failed
};

struct SBuildProgress final
{
	EBuildPhase currentPhase{ EBuildPhase::CleaningPreviousBuild };
	float overallProgress{ 0.0f };  // 0.0 to 1.0
	float phaseProgress{ 0.0f };    // 0.0 to 1.0 within current phase
	std::string statusMessage;
	std::string currentTask;
};

} // namespace Ctrn
