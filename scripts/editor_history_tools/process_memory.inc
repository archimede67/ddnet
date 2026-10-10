// Prepared benchmark executable only; production sources remain untouched.
#include <base/detect.h>

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>

// Process memory declarations require the Windows types above.
#include <psapi.h>
#endif

namespace benchmark_allocations
{
	std::atomic<double> g_LoadMilliseconds{0};
	void PrintMemory(const char *pPhase)
	{
		std::size_t Resident = 0, Private = 0, PeakResident = 0, PeakPrivate = 0;
#if defined(CONF_FAMILY_WINDOWS)
		PROCESS_MEMORY_COUNTERS_EX Memory{};
		Memory.cb = sizeof(Memory);
		if(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&Memory), sizeof(Memory)))
		{
			Resident = Memory.WorkingSetSize;
			Private = Memory.PrivateUsage;
			PeakResident = Memory.PeakWorkingSetSize;
			PeakPrivate = Memory.PeakPagefileUsage;
		}
#elif defined(CONF_PLATFORM_LINUX)
		if(auto *pFile = std::fopen("/proc/self/status", "r"))
		{
			char aLine[256];
			while(std::fgets(aLine, sizeof(aLine), pFile))
			{
				std::size_t Kilobytes;
				if(std::sscanf(aLine, "VmRSS: %zu kB", &Kilobytes) == 1)
					Resident = Kilobytes * 1024;
				if(std::sscanf(aLine, "VmHWM: %zu kB", &Kilobytes) == 1)
					PeakResident = Kilobytes * 1024;
			}
			std::fclose(pFile);
		}
#endif
#if defined(DDNET_BENCH_NO_ALLOCATION_HOOKS)
		std::printf("MEMORY phase=%s process_resident=%zu process_private=%zu process_peak_resident=%zu process_peak_private=%zu\n", pPhase, Resident, Private, PeakResident, PeakPrivate);
#else
		std::printf("MEMORY phase=%s process_resident=%zu process_private=%zu process_peak_resident=%zu process_peak_private=%zu cpp_live=%zu\n", pPhase, Resident, Private, PeakResident, PeakPrivate, g_Live.load());
#endif
	}
}
