#ifndef CHAIOS_PERFORMANCE_TEST_H
#define CHAIOS_PERFORMANCE_TEST_H

#include <stdheaders.h>
#include <chaikrnl.h>

struct PerformanceElement {
	char16_t* Name;
	uint64_t Counter;
	uint64_t CounterUse;
};


class PerformanceTest
{
public:
	static PerformanceTest& GetPerformance();
	void StartTimer(PerformanceElement& perf);
	void StopTimer(PerformanceElement& perf);
	void StartTiming();
	uint64_t StopTiming();
private:
	PerformanceTest();
	uint64_t StartTime;
	bool pActive;
	static PerformanceTest GlobalPerformance;
};

#endif
