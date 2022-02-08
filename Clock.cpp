#include "Clock.h"
#include <chrono>

static int64_t s_CyclesPerSecond = 0;
static int64_t s_CyclesPreMilliSecond = 0;
static int64_t s_CyclesPerMicroSecond = 0;

static void CalcCycles()
{
	using namespace std::chrono_literals;	// 支持下面1ms的写法

	// 代码预热
	for (int i = 0; i < 100; ++i)
	{
		(void)std::chrono::high_resolution_clock::now();
		TSC();
	}

	// 计算 rdtscp 指令使用的时钟频率
	auto start = std::chrono::high_resolution_clock::now();
	int64_t c1 = TSC();
	std::this_thread::sleep_for(1ms);
	auto end = std::chrono::high_resolution_clock::now();
	int64_t c2 = TSC();
	int64_t elapsed = std::chrono::duration<int64_t, std::nano>(end - start).count();
	if (elapsed <= 0)
	{
		elapsed = 2500000;
	}
	int64_t n = c2 - c1;

	s_CyclesPerSecond = n * 1000000000 / elapsed;
	if (s_CyclesPerSecond <= 1000000000) {
		s_CyclesPerSecond = 1000000000;
	}

	s_CyclesPreMilliSecond = n * 1000000 / elapsed;
	if (s_CyclesPreMilliSecond <= 1000000) {
		s_CyclesPreMilliSecond = 1000000;
	}

	s_CyclesPerMicroSecond = n * 1000 / elapsed;
	if (s_CyclesPerMicroSecond <= 1000)
	{
		s_CyclesPerMicroSecond = 1000;
	}
}

int64_t TSCPerS()
{
	if (s_CyclesPerSecond < 0)
	{
		CalcCycles();
	}
	return s_CyclesPerSecond;
}

int64_t TSCPerMS()
{
	if (s_CyclesPreMilliSecond < 0)
	{
		CalcCycles();
	}
	return s_CyclesPreMilliSecond;
}

int64_t TSCPerUS()
{
	if (s_CyclesPerMicroSecond < 0)
	{
		CalcCycles();
	}
	return s_CyclesPerMicroSecond;
}


