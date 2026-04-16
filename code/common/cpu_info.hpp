#pragma once

namespace Ctrn
{
struct SCpuInfo final
{
	int physicalCores;
	int logicalCores;
	double totalMemoryGB;      // Total system memory in GiB (OS-visible, excludes hardware reservations)
	double availableMemoryGB;  // Currently available memory in GiB (changes at runtime)

	[[nodiscard]] static SCpuInfo Detect();

	// Get total parallelism capability (total CPU threads)
	int GetMaxParallelism() const;

	// Get optimal default (physical cores)
	int GetDefaultNumJobs() const;

	// Get default link jobs based on RAM and build type
	int GetDefaultLinkJobs() const;                    // Release builds: 4GB per job
	int GetDefaultLinkJobsConservative() const;       // Debug/RelWithDebInfo builds: 9GB per job

	// Get maximum link jobs based on RAM and build type
	int GetMaxLinkJobs() const;                        // Release builds: 4GB per job
	int GetMaxLinkJobsConservative() const;           // Debug/RelWithDebInfo builds: 9GB per job

private:
	static SCpuInfo DetectInternal();
};

} // namespace Ctrn
