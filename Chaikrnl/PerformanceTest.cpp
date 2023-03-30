#include <arch/cpu.h>
#include "PerformanceTest.h"

PerformanceTest PerformanceTest::GlobalPerformance;

PerformanceTest::PerformanceTest()
{
    StartTime = 0;
    pActive = false;
}


PerformanceTest& PerformanceTest::GetPerformance()
{
    return GlobalPerformance;
}

void PerformanceTest::StartTimer(PerformanceElement& perf)
{
    if (!pActive) 
        perf.CounterUse = 0;
    else
        perf.CounterUse = arch_get_cpu_ticks();
}

void PerformanceTest::StopTimer(PerformanceElement& perf)
{
    if (perf.CounterUse == 0) return;
    perf.Counter += arch_get_cpu_ticks() - perf.CounterUse;
}

void PerformanceTest::StartTiming()
{
    StartTime = arch_get_cpu_ticks();
    pActive = true;
}

uint64_t PerformanceTest::StopTiming()
{
    return arch_get_cpu_ticks() - StartTime;
}
