#ifndef _SPEEDTEST_H_
#define _SPEEDTEST_H_

#include <unordered_map>
#include <boost/thread/mutex.hpp>
#include <atomic>

#if defined(_MSC_VER)
#include <intrin.h>  
#else
#include <x86intrin.h>
#endif

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define	FORCE_INLINE inline __attribute__((always_inline))
#endif

// 获取CPU当前频率值
FORCE_INLINE int64_t TSC()
{
	// rdtsc
	return (int64_t)__rdtsc();

	// rdtscp
	//uint32_t aux;
	//return (int64_t)__rdtscp(&aux);
}

// 获取CPU微妙的频率
extern int64_t TSCPerUS();

struct SpeedTestPosition
{
	const char* name = NULL;	// 测试位置名 要求是字面变量
	int num = 0;				// 测试位置号

	std::size_t hash = 0;		// 构造时直接计算好hash

	SpeedTestPosition(const char* _name_, int _num_) : name(_name_), num(_num_)
	{
		if (_name_)
			hash = std::hash<std::size_t>()((std::size_t)_name_) + std::hash<int>()(num);
		else
			hash = std::hash<int>()(num);
	}

	bool operator == (const SpeedTestPosition& other) const
	{
		return name == other.name && num == other.num;
	}
};

struct SpeedTestValue
{
	SpeedTestValue() {}
	SpeedTestValue(int times, int64_t tsc, int64_t maxtsc)
		: calltimes(times), elapsedTSC(tsc), elapsedMaxTSC(maxtsc)
	{
	}
	SpeedTestValue(const SpeedTestValue& other)
		: calltimes(other.calltimes.load()), elapsedTSC(other.elapsedTSC.load()), elapsedMaxTSC(other.elapsedMaxTSC.load())
	{
	}

	std::atomic<int64_t> calltimes = { 0 };		// 调用次数
	std::atomic<int64_t> elapsedTSC = { 0 }; // 消耗CPU帧率
	std::atomic<int64_t> elapsedMaxTSC = { 0 }; // 消耗CPU最大帧率

	SpeedTestValue& operator += (const SpeedTestValue& other)
	{
		calltimes += other.calltimes;
		elapsedTSC += other.elapsedTSC;
		int64_t tsc = other.elapsedMaxTSC.load();
		if (elapsedMaxTSC < tsc)
		{
			elapsedMaxTSC = tsc;
		}
		return *this;
	}
	SpeedTestValue& operator = (const SpeedTestValue& other)
	{
		calltimes = other.calltimes.load();
		elapsedTSC = other.elapsedTSC.load();
		elapsedMaxTSC = other.elapsedMaxTSC.load();
		return *this;
	}
};

struct SpeedTestPositionHash
{
	std::size_t operator()(const SpeedTestPosition& obj) const
	{
		return obj.hash;
	}
};

typedef std::unordered_map<SpeedTestPosition, SpeedTestValue, SpeedTestPositionHash> SpeedTestPositionMap;

class SpeedTestRecord
{
public:
	void Clear(SpeedTestPositionMap& lastdata);

	// 快照数据
	// 【参数metricsprefix和tags 不要有相关格式禁止的特殊字符 内部不对这两个参数做任何格式转化】
	// metricsprefix指标名前缀 内部产生指标如下
	// metricsprefix_calltimes 调用次数
	// metricsprefix_elapse 耗时 微秒
	// metricsprefix_maxelapse 最大耗时 微秒
	// tags额外添加的标签， 内部产生标签 name:测试名称 num;测试号
	enum SnapshotType { Json, Influx, Prometheus };
	std::string Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags = std::map<std::string, std::string>());

	void Add(const SpeedTestPosition& testpos, int64_t tsc);

	void SetRecord(bool b) { brecord = b; }
	void SetLog(int64_t v) { greaterUSLog = v; }

protected:
	// 记录的测试数据
	boost::shared_mutex mutex;
	SpeedTestPositionMap records;

	// 是否记录测试数据
	bool brecord = true;

	friend class SpeedTest;
	// 单测试值大于改值时输出日志 微妙 <=0表示不使用这个值 
	int64_t greaterUSLog = 0;
};

class SpeedTest
{
public:
	// _name_ 参数必须是一个字面量
	SpeedTest(const char* _name_, int _index_);
	~SpeedTest();

protected:
	int64_t begin_tsc; // CPU频率值
	SpeedTestPosition testpos;
};

extern SpeedTestRecord g_speedtestrecord;

#define __SpeedTestObjName(line)  speedtestobj_##line
#define _SpeedTestObjName(line)  __SpeedTestObjName(line)
#define SpeedTestObjName() _SpeedTestObjName(__LINE__)

#define SeedTestObject() \
	SpeedTest SpeedTestObjName()(__FUNCTION__, __LINE__);

#endif