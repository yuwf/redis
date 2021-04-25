#include "SpeedTest.h"
#include <chrono>

#include "LLog.h"
#define LogInfo LLOG_INFO

thread_local SpeedTestData g_default_speedtestdata;
bool g_record_speedtestdata = false;
thread_local bool g_record_speedtestdata_thread = false;

int64_t TSCPerUS()
{
	static int64_t CyclesPerMicroSecond;
	if (CyclesPerMicroSecond <= 0)
	{
		using namespace std::chrono_literals;

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
		int64_t tmp = n * 1000 / elapsed;
		if (tmp <= 1000)
		{
			tmp = 1000;
		}

		CyclesPerMicroSecond = tmp;
	}
	return CyclesPerMicroSecond;
}

SpeedTest::SpeedTest(SpeedTestData& _testdata_, const char* _name_, int _index_)
	: begin_tsc(TSC())
	, testdata(_testdata_)
	, testpos(_name_, _index_)
{
}

SpeedTest::~SpeedTest()
{
	int64_t tsc = TSC() - begin_tsc;

	// 记录 name值有效才记录
	if ((g_record_speedtestdata || g_record_speedtestdata_thread) && testpos.name)
	{
		auto it = testdata.position.find(testpos);
		if (it == testdata.position.end())
		{
			SpeedTestValue value;
			value.calltimes = 1;
			value.elapsedTSC = tsc;
			value.elapsedMaxTSC = tsc;
			testdata.position[testpos] = value;
		}
		else
		{
			it->second.calltimes++;
			it->second.elapsedTSC += tsc;
			if (it->second.elapsedMaxTSC < tsc)
			{
				it->second.elapsedMaxTSC = tsc;
			}
		}
	}

	int64_t elapsed = tsc / TSCPerUS();
	if (testdata.greaterUSLog > 0 && elapsed > testdata.greaterUSLog)
	{
		LogInfo("SpeedTest %s Speed %lldUS", testpos.name ? testpos.name : "", elapsed);
	}
}

