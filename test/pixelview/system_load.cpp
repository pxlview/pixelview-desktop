// Whole-computer load for the connection report: interval arithmetic and the real readers.
#include "frontend/utility/PixelviewSystemLoad.hpp"
#include <cassert>
#include <cstdio>
using pixelview::CpuTicks;
using pixelview::SystemLoad;
int main()
{
	SystemLoad load;
	// The first sample only sets the baseline.
	assert(load.cpuPct(CpuTicks{100, 400, true}) == -1);
	// 150 of 200 ticks busy since then.
	assert(load.cpuPct(CpuTicks{250, 600, true}) == 75.0);
	// Counters that went back, did not move, or are unreadable give no value and reset the baseline.
	assert(load.cpuPct(CpuTicks{10, 20, true}) == -1);
	assert(load.cpuPct(CpuTicks{10, 20, true}) == -1);
	assert(load.cpuPct(CpuTicks{}) == -1);
	assert(load.cpuPct(CpuTicks{20, 40, true}) == -1);
	assert(load.cpuPct(CpuTicks{30, 60, true}) == 50.0);
	// More busy than total ticks is not a valid interval.
	assert(load.cpuPct(CpuTicks{100, 70, true}) == -1);
	// This machine: the readers work and give plausible values.
	const CpuTicks a = SystemLoad::readCpu();
	assert(a.valid && a.total >= a.busy);
	SystemLoad live;
	live.cpuPct();
	volatile double spin = 0;
	for (int i = 0; i < 30000000; ++i) spin = spin + i * 0.5;
	const double cpu = live.cpuPct();
	const double memory = SystemLoad::memoryPct();
	std::printf("computer cpu %.1f %%, memory %.1f %%\n", cpu, memory);
	assert(cpu > 0 && cpu <= 100);
	assert(memory > 0 && memory <= 100);
	std::puts("system load ok");
}
