#include "SpeedTest.h"
#include <chrono>
#include <iosfwd>

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

SpeedTestRecord g_speedtestrecord;

SpeedTestData* SpeedTestRecord::Reg(const SpeedTestPosition& testpos)
{
	// 记录 name值有效才记录
	if (!brecord || !testpos.name)
	{
		return NULL;
	}
	// 先用共享锁 如果存在直接修改
	{
		std::shared_lock<boost::shared_mutex> lock(mutex);
		auto it = ((const SpeedTestPositionMap&)records).find(testpos); // 显示的调用const的find
		if (it != records.end())
		{
			return it->second;
		}
	}

	// 不存在构造一个
	SpeedTestData* p = new SpeedTestData;
	// 使用写锁
	{
		boost::unique_lock<boost::shared_mutex> lock(mutex);
		records.insert(std::make_pair(testpos, p));
	}
	return p;
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
			for (const auto& t : tags)
			{
				ss << "\"" << t.first << "\":\"" << t.second << "\",";
			}
			ss <<  "\"name\":\"" << it.first.name << "\",";
			ss <<  "\"num\":" << it.first.num << ",";
			ss << "\"" << metricsprefix << "_calltimes\":" << it.second->calltimes << ",";
			ss << "\"" << metricsprefix << "_elapse\":" << (it.second->elapsedTSC / TSCPerUS()) << ",";
			ss << "\"" << metricsprefix << "_maxelapse\":" << (it.second->elapsedMaxTSC / TSCPerUS());
			ss << "}";
		}
		ss << "]";
	}
	else if (type == Influx)
	{
		std::string tag;
		for (const auto& t : tags)
		{
			tag += ("," + t.first + "=" + t.second);
		}
		for (const auto& it : lastdata)
		{
			ss << metricsprefix << "_calltimes";
			ss << ",name=" << it.first.name << ",num=" << it.first.num << tag;
			ss << " value=" << it.second->calltimes << "i\n";

			ss << metricsprefix << "_elapse";
			ss << ",name=" << it.first.name << ",num=" << it.first.num << tag;
			ss << " value=" << (it.second->elapsedTSC / TSCPerUS()) << "i\n";

			ss << metricsprefix << "_maxelapse";
			ss << ",name=" << it.first.name << ",num=" << it.first.num << tag;
			ss << " value=" << (it.second->elapsedMaxTSC / TSCPerUS()) << "i\n";
		}
	}
	else if (type == Prometheus)
	{
		std::string tag;
		for (const auto& t : tags)
		{
			tag += ("," + t.first + "=\"" + t.second + "\"");
		}
		for (const auto& it : lastdata)
		{
			ss << metricsprefix << "_calltimes";
			ss << "{name=\"" << it.first.name << "\",num=\"" << it.first.num << "\"" << tag << "}";
			ss << " " << it.second->calltimes << "\n";

			ss << metricsprefix << "_elapse";
			ss << "{name=\"" << it.first.name << "\",num=\"" << it.first.num << "\"" << tag << "}";
			ss << " " << (it.second->elapsedTSC / TSCPerUS()) << "\n";

			ss << metricsprefix << "_maxelapse";
			ss << "{name=\"" << it.first.name << "\",num=\"" << it.first.num << "\"" << tag << "}";
			ss << " " << (it.second->elapsedMaxTSC / TSCPerUS()) << "\n";
		}
	}
	return ss.str();
}

SpeedTest::SpeedTest(const char* _name_, int _index_)
	: pspeedtestdata(g_speedtestrecord.Reg(SpeedTestPosition(_name_, _index_)))
	, begin_tsc(TSC())
{
}

SpeedTest::SpeedTest(SpeedTestData* p)
	: pspeedtestdata(p)
	, begin_tsc(TSC())
{
}

SpeedTest::~SpeedTest()
{
	if (pspeedtestdata)
	{
		int64_t tsc = TSC() - begin_tsc;
		pspeedtestdata->calltimes++;
		pspeedtestdata->elapsedTSC += tsc;
		if (pspeedtestdata->elapsedMaxTSC < tsc)
		{
			pspeedtestdata->elapsedMaxTSC = tsc;
		}
	}
}

