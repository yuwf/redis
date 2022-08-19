#include "Clock.h"
#include <time.h>
#include <chrono>

// CPU每秒的频率值
static int64_t s_cyclespersecond = 0;
// CPU每毫秒的频率值
static int64_t s_cyclespremillisecond = 0;
// CPU每微秒的频率值
static int64_t s_cyclespermicrosecond = 0;

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
	std::this_thread::sleep_for(100ms);
	auto end = std::chrono::high_resolution_clock::now();
	int64_t c2 = TSC();
	int64_t elapsed = std::chrono::duration<int64_t, std::nano>(end - start).count();
	if (elapsed <= 0)
	{
		elapsed = 100000000; // 100ms
	}
	int64_t n = c2 - c1;

	s_cyclespersecond = n * 1000000000 / elapsed;
	if (s_cyclespersecond <= 1000000000)
	{
		s_cyclespersecond = 1000000000;
	}

	s_cyclespremillisecond = n * 1000000 / elapsed;
	if (s_cyclespremillisecond <= 1000000)
	{
		s_cyclespremillisecond = 1000000;
	}

	s_cyclespermicrosecond = n * 1000 / elapsed;
	if (s_cyclespermicrosecond <= 1000)
	{
		s_cyclespermicrosecond = 1000;
	}
}

int64_t TSCPerS()
{
	if (s_cyclespersecond <= 0)
	{
		CalcCycles();
	}
	return s_cyclespersecond;
}

int64_t TSCPerMS()
{
	if (s_cyclespremillisecond <= 0)
	{
		CalcCycles();
	}
	return s_cyclespremillisecond;
}

int64_t TSCPerUS()
{
	if (s_cyclespermicrosecond <= 0)
	{
		CalcCycles();
	}
	return s_cyclespermicrosecond;
}

PTime::PTime()
{
	SetNow();
}

PTime::PTime(int secs)
{
	SetSecs(secs);
}

void PTime::SetSecs(int secs)
{
	SetMSecs((int64_t)secs * 1000);
}

int PTime::Now()
{
	return int(chrono::system_clock::now().time_since_epoch().count() / 10000000);
}

void PTime::SetMSecs(int64_t msecs)
{
	ts = msecs;
	time_t t = ts / 1000;
#if _LINUX
	localtime_r(&t, &_tm);
#else
	localtime_s(&_tm, &t);
#endif
}

void PTime::SetNow()
{
	SetMSecs(chrono::system_clock::now().time_since_epoch().count() / 10000);
}

std::string PTime::ToString() const
{
	char tmp[32];
	sprintf(tmp, "%04d-%02d-%02d %02d:%02d:%02d", (_tm.tm_year + 1900), _tm.tm_mon + 1, _tm.tm_mday, _tm.tm_hour, _tm.tm_min, _tm.tm_sec);
	return std::move(std::string(tmp));
}

bool PTime::FromString(const std::string& str)
{
	tm tmp = { 0 };
	int param = sscanf(str.c_str(), "%d-%d-%d %d:%d:%d",
		&(tmp.tm_year),
		&(tmp.tm_mon),
		&(tmp.tm_mday),
		&(tmp.tm_hour),
		&(tmp.tm_min),
		&(tmp.tm_sec));
	if (param != 6)
	{
		return false;
	}
	tmp.tm_year -= 1900;
	tmp.tm_mon--;
	tmp.tm_isdst = -1;

	SetMSecs(mktime(&tmp) * (int64_t)1000); // 能算出星期
	return true;
}

