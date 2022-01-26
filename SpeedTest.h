#ifndef _SPEEDTEST_H_
#define _SPEEDTEST_H_

// by yuwf qingting.water@gmail.com

// 区域块性能监控，支持监控数据快照

/* 使用案例

函数名和行号作为测试参数
SeedTestObject();
自定义测试参数，第一个参数要求是字面变量或者常量静态区字符串，第二个参数为一个数字
SpeedTestObject2("HandleMsgFun", msgid);

直接使用宏，宏会生成一个静态测试数据对象指针
测试对象会直接使用改指针进行原子操作，效率非常高

*/

#include <unordered_map>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>
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


struct SpeedTestData
{
	SpeedTestData() {}
	SpeedTestData(int times, int64_t tsc, int64_t maxtsc)
		: calltimes(times), elapsedTSC(tsc), elapsedMaxTSC(maxtsc)
	{
	}
	SpeedTestData(const SpeedTestData& other)
		: calltimes(other.calltimes.load()), elapsedTSC(other.elapsedTSC.load()), elapsedMaxTSC(other.elapsedMaxTSC.load())
	{
	}

	std::atomic<int64_t> calltimes = { 0 };		// 调用次数
	std::atomic<int64_t> elapsedTSC = { 0 };	// 消耗CPU帧率
	std::atomic<int64_t> elapsedMaxTSC = { 0 }; // 消耗CPU最大帧率

	SpeedTestData& operator += (const SpeedTestData& other)
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
	SpeedTestData& operator = (const SpeedTestData& other)
	{
		calltimes = other.calltimes.load();
		elapsedTSC = other.elapsedTSC.load();
		elapsedMaxTSC = other.elapsedMaxTSC.load();
		return *this;
	}
};

struct SpeedTestPosition
{
	const char* name = NULL;	// 测试位置名 要求是字面变量或者常量静态区数据
	int num = 0;				// 测试位置号

	SpeedTestPosition(const char* _name_, int _num_) : name(_name_), num(_num_)
	{
	}

	bool operator == (const SpeedTestPosition& other) const
	{
		return name == other.name && num == other.num;
	}
};
struct SpeedTestPositionHash
{
	uintptr_t operator()(const SpeedTestPosition& obj) const
	{
		return (uintptr_t)obj.name;
	}
};

typedef std::unordered_map<SpeedTestPosition, SpeedTestData*, SpeedTestPositionHash> SpeedTestPositionMap;

class SpeedTestRecord
{
public:
	// 注册测试点 返回该位置的测试数据
	SpeedTestData* Reg(const SpeedTestPosition& testpos);

	// 快照数据
	// 【参数metricsprefix和tags 不要有相关格式禁止的特殊字符 内部不对这两个参数做任何格式转化】
	// metricsprefix指标名前缀 内部产生指标如下
	// metricsprefix_calltimes 调用次数
	// metricsprefix_elapse 耗时 微秒
	// metricsprefix_maxelapse 最大耗时 微秒
	// tags额外添加的标签， 内部产生标签 name:测试名称 num;测试号
	enum SnapshotType { Json, Influx, Prometheus };
	std::string Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags = std::map<std::string, std::string>());

	void SetRecord(bool b) { brecord = b; }

protected:
	// 记录的测试数据
	boost::shared_mutex mutex;
	SpeedTestPositionMap records;

	// 是否记录测试数据
	bool brecord = true;
};

extern SpeedTestRecord g_speedtestrecord;

class SpeedTest
{
public:
	// _name_ 参数必须是一个字面量
	SpeedTest(const char* _name_, int _index_);	// 这种方式会查找SpeedTestData 比较慢
	SpeedTest(SpeedTestData* p);

	~SpeedTest();

protected:
	SpeedTestData* pspeedtestdata = NULL;
	int64_t begin_tsc = 0; // CPU频率值 定义放到pspeedtestdata后面 防止构造时g_speedtestrecord.Reg影响真正测试的区域
};

#define ___SpeedTestDataName(line)  speedtestdata_##line
#define __SpeedTestDataName(line)  ___SpeedTestDataName(line)
#define _SpeedTestDataName() __SpeedTestDataName(__LINE__)

#define ___SpeedTestObjName(line)  speedtestobj_##line
#define __SpeedTestObjName(line)  ___SpeedTestObjName(line)
#define _SpeedTestObjName() __SpeedTestObjName(__LINE__)

#define SpeedTestObject() \
	static SpeedTestData* _SpeedTestDataName() = g_speedtestrecord.Reg(SpeedTestPosition(__FUNCTION__, __LINE__)); \
	SpeedTest _SpeedTestObjName()(_SpeedTestDataName());
	
#define SpeedTestObject2(_name_, _index_) \
	static SpeedTestData* _SpeedTestDataName() = g_speedtestrecord.Reg(SpeedTestPosition(_name_, _index_)); \
	SpeedTest _SpeedTestObjName()(_SpeedTestDataName());

// 写错了 要去掉
#define SeedTestObject() SpeedTestObject()

#endif