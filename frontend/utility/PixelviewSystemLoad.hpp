#pragma once
// Pixelview modification, 2026-10-10: load of the whole computer, not just
// this process, for the connection report. libobs only measures its own
// process; a stream can suffer because something else uses the CPU.
//
// CPU is the busy share of all cores between two samples, from the system's
// cumulative tick counters; memory is the share of physical memory in use.
// The arithmetic is platform independent and tested in
// test/pixelview/system_load.cpp; the readers are per platform.
#include <cstdint>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <sys/sysctl.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <cstdio>
#include <string_view>
#endif
namespace pixelview {
struct CpuTicks {
	uint64_t busy = 0, total = 0;
	bool valid = false;
};
class SystemLoad {
public:
	// Busy share of all cores since the previous call, -1 for the first one
	// or when the counters cannot be read or went back.
	double cpuPct() { return cpuPct(readCpu()); }
	double cpuPct(CpuTicks now)
	{
		double result = -1;
		if (now.valid && last.valid && now.total > last.total && now.busy >= last.busy &&
		    now.busy - last.busy <= now.total - last.total)
			result = 100.0 * double(now.busy - last.busy) / double(now.total - last.total);
		last = now;
		return result;
	}
	// Share of physical memory in use, -1 when it cannot be read.
	static double memoryPct()
	{
#if defined(__APPLE__)
		// Used the way Activity Monitor counts it: app (internal minus purgeable), wired and compressed pages.
		vm_statistics64_data_t vm{};
		mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
		uint64_t physical = 0;
		size_t size = sizeof(physical);
		if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &count) != KERN_SUCCESS ||
		    sysctlbyname("hw.memsize", &physical, &size, nullptr, 0) != 0 || !physical)
			return -1;
		const uint64_t internal = vm.internal_page_count > vm.purgeable_count
						  ? vm.internal_page_count - vm.purgeable_count
						  : 0;
		const uint64_t used = (internal + vm.wire_count + vm.compressor_page_count) * (uint64_t)vm_kernel_page_size;
		return used >= physical ? 100.0 : 100.0 * double(used) / double(physical);
#elif defined(_WIN32)
		MEMORYSTATUSEX status{};
		status.dwLength = sizeof(status);
		if (!GlobalMemoryStatusEx(&status) || !status.ullTotalPhys) return -1;
		return 100.0 * double(status.ullTotalPhys - status.ullAvailPhys) / double(status.ullTotalPhys);
#elif defined(__linux__)
		FILE *file = std::fopen("/proc/meminfo", "r");
		if (!file) return -1;
		char key[64];
		unsigned long long value = 0, total = 0, available = 0;
		while (std::fscanf(file, "%63s %llu kB\n", key, &value) == 2) {
			if (std::string_view(key) == "MemTotal:") total = value;
			else if (std::string_view(key) == "MemAvailable:") available = value;
		}
		std::fclose(file);
		return total && available <= total ? 100.0 * double(total - available) / double(total) : -1;
#else
		return -1;
#endif
	}
	static CpuTicks readCpu()
	{
		CpuTicks ticks;
#if defined(__APPLE__)
		host_cpu_load_info_data_t info{};
		mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
		if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&info, &count) != KERN_SUCCESS)
			return ticks;
		const uint64_t user = info.cpu_ticks[CPU_STATE_USER], system = info.cpu_ticks[CPU_STATE_SYSTEM],
			       nice = info.cpu_ticks[CPU_STATE_NICE], idle = info.cpu_ticks[CPU_STATE_IDLE];
		ticks.busy = user + system + nice;
		ticks.total = ticks.busy + idle;
		ticks.valid = true;
#elif defined(_WIN32)
		FILETIME idle, kernel, user;
		if (!GetSystemTimes(&idle, &kernel, &user)) return ticks;
		auto value = [](const FILETIME &t) { return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
		// Kernel time includes idle time.
		ticks.total = value(kernel) + value(user);
		ticks.busy = ticks.total - value(idle);
		ticks.valid = true;
#elif defined(__linux__)
		FILE *file = std::fopen("/proc/stat", "r");
		if (!file) return ticks;
		unsigned long long user = 0, nice = 0, system = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
		const int read = std::fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle,
					     &iowait, &irq, &softirq, &steal);
		std::fclose(file);
		if (read < 4) return ticks;
		ticks.busy = user + nice + system + irq + softirq + steal;
		ticks.total = ticks.busy + idle + iowait;
		ticks.valid = true;
#endif
		return ticks;
	}

private:
	CpuTicks last;
};
} // namespace pixelview
