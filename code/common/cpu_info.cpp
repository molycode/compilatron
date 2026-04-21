#include "common/cpu_info.hpp"
#include "common/loggers.hpp"
#include <charconv>
#include <thread>
#include <fstream>
#include <string>
#include <set>
#include <algorithm>
#include <mutex>

#ifdef CTRN_PLATFORM_WINDOWS
#include <windows.h>
#endif // CTRN_PLATFORM_WINDOWS

namespace Ctrn
{
//////////////////////////////////////////////////////////////////////////
SCpuInfo SCpuInfo::Detect()
{
	// Use static instance to avoid repeated detection and logging
	static std::once_flag s_detected;
	static SCpuInfo s_cachedInfo;

	std::call_once(s_detected, []()
	{
		s_cachedInfo = DetectInternal();
	});

	return s_cachedInfo;
}

//////////////////////////////////////////////////////////////////////////
SCpuInfo SCpuInfo::DetectInternal()
{
	SCpuInfo info;
	info.logicalCores = std::thread::hardware_concurrency();

	if (info.logicalCores == 0)
	{
		info.logicalCores = 4; // Fallback
	}

	// Detect system memory
#ifdef CTRN_PLATFORM_WINDOWS
	// Windows: Get total physical memory
	MEMORYSTATUSEX memInfo;
	memInfo.dwLength = sizeof(MEMORYSTATUSEX);

	if (GlobalMemoryStatusEx(&memInfo))
	{
		info.totalMemoryGB     = static_cast<double>(memInfo.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
		info.availableMemoryGB = static_cast<double>(memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
	}
	else
	{
		info.totalMemoryGB     = 8;
		info.availableMemoryGB = 8;
		gLog.Warning(Tge::Logging::ETarget::File, "SCpuInfo: Windows: Failed to detect memory, using 8 GiB fallback");
	}
#else
	// Linux: Parse /proc/meminfo
	info.totalMemoryGB     = 8; // Default fallback
	info.availableMemoryGB = 8;
	std::ifstream meminfo("/proc/meminfo");

	if (meminfo.is_open())
	{
		std::string line;
		bool foundTotal{ false };
		bool foundAvailable{ false };

		while (std::getline(meminfo, line) && !(foundTotal && foundAvailable))
		{
			auto parseKiB = [&](std::string_view prefix) -> long
			{
				if (line.find(prefix) != 0)
				{
					return -1;
				}

				size_t colonPos{ line.find(':') };

				if (colonPos == std::string::npos)
				{
					return -1;
				}

				std::string_view valStr{ line.data() + colonPos + 1, line.size() - colonPos - 1 };
				size_t kbPos{ valStr.find("kB") };

				if (kbPos != std::string_view::npos)
				{
					valStr = valStr.substr(0, kbPos);
				}

				size_t trimStart{ valStr.find_first_not_of(' ') };

				if (trimStart == std::string_view::npos)
				{
					return -1;
				}

				long value{ 0 };
				std::from_chars(valStr.data() + trimStart, valStr.data() + valStr.size(), value);
				return value;
			};

			if (!foundTotal)
			{
				long kib{ parseKiB("MemTotal:") };

				if (kib >= 0)
				{
					info.totalMemoryGB = static_cast<double>(kib) / (1024.0 * 1024.0);
					foundTotal = true;
				}
			}

			if (!foundAvailable)
			{
				long kib{ parseKiB("MemAvailable:") };

				if (kib >= 0)
				{
					info.availableMemoryGB = static_cast<double>(kib) / (1024.0 * 1024.0);
					foundAvailable = true;
				}
			}
		}

	}
#endif // CTRN_PLATFORM_WINDOWS

#ifdef CTRN_PLATFORM_WINDOWS
	// Windows: Use GetLogicalProcessorInformation
	DWORD bufferSize{ 0 };
	GetLogicalProcessorInformation(nullptr, &bufferSize);

	if (bufferSize > 0)
	{
		std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(bufferSize / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));

		if (GetLogicalProcessorInformation(buffer.data(), &bufferSize))
		{
			info.physicalCores = 0;

			for (auto const& proc : buffer)
			{
				if (proc.Relationship == RelationProcessorCore)
				{
					info.physicalCores++;
				}
			}

			if (info.physicalCores == 0)
			{
				info.physicalCores = info.logicalCores / 2; // Fallback estimate
			}
		}
		else
		{
			info.physicalCores = info.logicalCores / 2; // Fallback estimate
		}
	}
	else
	{
		info.physicalCores = info.logicalCores / 2; // Fallback estimate
	}
#else
	// Linux/Unix: Parse /proc/cpuinfo
	info.physicalCores = 0;
	std::ifstream cpuinfo("/proc/cpuinfo");

	if (cpuinfo.is_open())
	{
		std::set<int> physicalIds;
		std::string line;
		int currentPhysicalId{ -1 };

		while (std::getline(cpuinfo, line))
		{
			if (line.find("physical id") == 0)
			{
				size_t colonPos{ line.find(':') };

				if (colonPos != std::string::npos)
				{
					std::string idStr{ line.substr(colonPos + 1) };
					auto trimStart = idStr.find_first_not_of(' ');

					if (trimStart != std::string::npos)
					{
						std::from_chars(idStr.data() + trimStart, idStr.data() + idStr.size(), currentPhysicalId);
					}

					physicalIds.insert(currentPhysicalId);
				}
			}
			else if (line.find("cpu cores") == 0)
			{
				size_t colonPos{ line.find(':') };

				if (colonPos != std::string::npos)
				{
					int coresPerCpu{ 0 };
					std::string coresStr{ line.substr(colonPos + 1) };
					auto trimStart = coresStr.find_first_not_of(' ');

					if (trimStart != std::string::npos)
					{
						std::from_chars(coresStr.data() + trimStart, coresStr.data() + coresStr.size(), coresPerCpu);
					}

					info.physicalCores = static_cast<int>(physicalIds.size()) * coresPerCpu;
				}
			}
		}

		if (info.physicalCores == 0)
		{
			info.physicalCores = info.logicalCores / 2; // Assume hyperthreading
		}
	}
	else
	{
		info.physicalCores = info.logicalCores / 2; // Fallback: assume hyperthreading
	}

	if (info.physicalCores == 0)
	{
		info.physicalCores = 1;
	}
#endif // CTRN_PLATFORM_WINDOWS

	gLog.Info(Tge::Logging::ETarget::File, "SCpuInfo: {}/{} cores, {:.2f} GiB RAM",
		info.logicalCores, info.physicalCores, info.totalMemoryGB);
	return info;
}

//////////////////////////////////////////////////////////////////////////
int SCpuInfo::GetMaxParallelism() const
{
	// Total CPU parallelism capability = total available threads
	// For hyperthreaded systems: logicalCores = total threads available
	return logicalCores;
}

//////////////////////////////////////////////////////////////////////////
int SCpuInfo::GetDefaultNumJobs() const
{
	return std::max(1, physicalCores);
}

//////////////////////////////////////////////////////////////////////////
int SCpuInfo::GetDefaultLinkJobs() const
{
	// Default for Release builds: Use ~62% of RAM for linking (4GB per job)
	// This leaves ~38% free for system and other processes
	constexpr long GB_PER_LINK_JOB = 4;
	long defaultLinkMemoryGB{ static_cast<long>(totalMemoryGB * 0.625) }; // 62.5%
	int defaultLinkJobs{ static_cast<int>(std::max(1L, defaultLinkMemoryGB / GB_PER_LINK_JOB)) };

	// Cap at a reasonable maximum to prevent runaway values on very large systems
	return std::min(defaultLinkJobs, 20);
}

//////////////////////////////////////////////////////////////////////////
int SCpuInfo::GetDefaultLinkJobsConservative() const
{
	// Default for Debug/RelWithDebInfo builds: Use ~62% of RAM for linking (9GB per job)
	// Conservative estimate for builds with debug info and less optimization
	constexpr long GB_PER_LINK_JOB = 9;
	long defaultLinkMemoryGB{ static_cast<long>(totalMemoryGB * 0.625) }; // 62.5%
	int defaultLinkJobs{ static_cast<int>(std::max(1L, defaultLinkMemoryGB / GB_PER_LINK_JOB)) };

	// Cap at a reasonable maximum to prevent runaway values on very large systems
	return std::min(defaultLinkJobs, 10);
}

//////////////////////////////////////////////////////////////////////////
int SCpuInfo::GetMaxLinkJobs() const
{
	// Maximum for Release builds: Use ~94% of RAM for linking (4GB per job)
	// This leaves ~6% free as minimum headroom
	constexpr long GB_PER_LINK_JOB = 4;
	long maxLinkMemoryGB{ static_cast<long>(totalMemoryGB * 0.9375) }; // 93.75%
	int maxLinkJobs{ static_cast<int>(std::max(1L, maxLinkMemoryGB / GB_PER_LINK_JOB)) };

	// Cap at a reasonable maximum to prevent runaway values on very large systems
	return std::min(maxLinkJobs, 30);
}

//////////////////////////////////////////////////////////////////////////
int SCpuInfo::GetMaxLinkJobsConservative() const
{
	// Maximum for Debug/RelWithDebInfo builds: Use ~94% of RAM for linking (9GB per job)
	// Conservative estimate for builds with debug info and less optimization
	constexpr long GB_PER_LINK_JOB = 9;
	long maxLinkMemoryGB{ static_cast<long>(totalMemoryGB * 0.9375) }; // 93.75%
	int maxLinkJobs{ static_cast<int>(std::max(1L, maxLinkMemoryGB / GB_PER_LINK_JOB)) };

	// Cap at a reasonable maximum to prevent runaway values on very large systems
	return std::min(maxLinkJobs, 15);
}

} // namespace Ctrn
