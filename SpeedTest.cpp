#include "SpeedTest.h"
#include <chrono>
#include <iosfwd>

#include "LLog.h"
#define LogInfo LLOG_INFO

SpeedTestRecord g_speedtestrecord;


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

void SpeedTestRecord::Clear(SpeedTestPositionMap& lastdata)
{
	boost::unique_lock<boost::shared_mutex> lock(mutex);
	records.swap(lastdata);
}

std::string SpeedTestRecord::Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags)
{
	SpeedTestPositionMap lastdata;
	{
		boost::unique_lock<boost::shared_mutex> lock(mutex);
		lastdata = records;
	}
	std::ostringstream ss;
	if (type == Json)
	{
		ss << "[";
		int index = 0;
		for (const auto& it : lastdata)
		{
			ss << ((++index) == 1 ? "{" : ",{");
			for (const auto& it : tags)
			{
				ss << "\"" << it.first << "\":\"" << it.second << "\",";
			}
			ss <<  "\"name\":\"" << it.first.name << "\",";
			ss <<  "\"num\":" << it.first.num << ",";
			ss << "\"" << metricsprefix << "_calltimes\":" << it.second.calltimes << ",";
			ss << "\"" << metricsprefix << "_elapse\":" << (it.second.elapsedTSC / TSCPerUS()) << ",";
			ss << "\"" << metricsprefix << "_maxelapse\":" << (it.second.elapsedMaxTSC / TSCPerUS());
			ss << "}";
		}
		ss << "]";
	}
	else if (type == Influx)
	{
		std::string tag;
		for (const auto& it : tags)
		{
			tag += ("," + it.first + "=" + it.second);
		}
		for (const auto& it : lastdata)
		{
			ss << metricsprefix << "_calltimes";
			ss << ",name=" << it.first.name << ",num=" << it.first.num << tag;
			ss << " value=" << it.second.calltimes << "i\n";

			ss << metricsprefix << "_elapse";
			ss << ",name=" << it.first.name << ",num=" << it.first.num << tag;
			ss << " value=" << (it.second.elapsedTSC / TSCPerUS()) << "i\n";

			ss << metricsprefix << "_maxelapse";
			ss << ",name=" << it.first.name << ",num=" << it.first.num << tag;
			ss << " value=" << (it.second.elapsedMaxTSC / TSCPerUS()) << "i\n";
		}
	}
	else if (type == Prometheus)
	{
		std::string tag;
		for (const auto& it : tags)
		{
			tag += ("," + it.first + "=\"" + it.second + "\"");
		}
		for (const auto& it : lastdata)
		{
			ss << metricsprefix << "_calltimes";
			ss << "{name=\"" << it.first.name << "\",num=\"" << it.first.num << "\"" << tag << "}";
			ss << " " << it.second.calltimes << "\n";

			ss << metricsprefix << "_elapse";
			ss << "{name=\"" << it.first.name << "\",num=\"" << it.first.num << "\"" << tag << "}";
			ss << " " << (it.second.elapsedTSC / TSCPerUS()) << "\n";

			ss << metricsprefix << "_maxelapse";
			ss << "{name=\"" << it.first.name << "\",num=\"" << it.first.num << "\"" << tag << "}";
			ss << " " << (it.second.elapsedMaxTSC / TSCPerUS()) << "\n";
		}
	}
	return ss.str();
}

void SpeedTestRecord::Add(const SpeedTestPosition& testpos, int64_t tsc)
{
	// 记录 name值有效才记录
	if (!brecord || !testpos.name)
	{
		return;
	}
	// 先用共享锁 如果存在直接修改
	{
		std::shared_lock<boost::shared_mutex> lock(mutex);
		auto it = records.find(testpos);
		if (it != records.end())
		{
			it->second.calltimes++;
			it->second.elapsedTSC += tsc;
			if (it->second.elapsedMaxTSC < tsc)
			{
				it->second.elapsedMaxTSC = tsc;
			}
			return; // 直接返回
		}
	}
	// 不存在直接用写锁
	{
		boost::unique_lock<boost::shared_mutex> lock(mutex);
		SpeedTestValue& value = records[testpos];
		value.calltimes = 1;
		value.elapsedTSC = tsc;
		value.elapsedMaxTSC = tsc;
	}
}

SpeedTest::SpeedTest(const char* _name_, int _index_)
	: begin_tsc(TSC())
	, testpos(_name_, _index_)
{
}

SpeedTest::~SpeedTest()
{
	int64_t tsc = TSC() - begin_tsc;
	g_speedtestrecord.Add(testpos, tsc);

	if (g_speedtestrecord.greaterUSLog > 0)
	{
		int64_t elapsed = tsc / TSCPerUS();
		if (elapsed > g_speedtestrecord.greaterUSLog)
		{
			LogInfo("SpeedTest %s Speed\t%lldMS\t%lldUS", testpos.name ? testpos.name : "", elapsed / 1000, elapsed % 1000);
		}
	}
}

