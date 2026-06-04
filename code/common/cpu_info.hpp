#pragma once

namespace Ctrn
{
struct SCpuInfo final
{
	int physicalCores;
	int logicalCores;
	double totalMemoryGiB;     // Total system memory in GiB (OS-visible, excludes hardware reservations)
	double availableMemoryGiB; // Currently available memory in GiB (changes at runtime)

	[[nodiscard]] static SCpuInfo Detect();

	// Get optimal default (physical cores)
	int GetDefaultNumJobs() const;

	// Get default link jobs based on RAM and build type
	int GetDefaultLinkJobs() const;                    // Release builds: 4 GiB per job
	int GetDefaultLinkJobsConservative() const;       // Debug/RelWithDebInfo builds: 9 GiB per job

	// Get maximum link jobs based on RAM and build type
	int GetMaxLinkJobs() const;                        // Release builds: 4 GiB per job
	int GetMaxLinkJobsConservative() const;           // Debug/RelWithDebInfo builds: 9 GiB per job

private:
	static SCpuInfo DetectInternal();
};

} // namespace Ctrn
