#ifndef _SPEEDTEST_H_
#define _SPEEDTEST_H_

#include <unordered_map>

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
	int index = 0;				// 测试位置号

	std::size_t hash = 0;		// 构造时直接计算好hash

	SpeedTestPosition(const char* _name_, int _index_) : name(_name_), index(_index_)
	{
		if (_name_)
			hash = std::hash<std::size_t>()((std::size_t)_name_) + std::hash<int>()(index);
		else
			hash = std::hash<int>()(index);
	}

	bool operator == (const SpeedTestPosition& other) const
	{
		return name == other.name && index == other.index;
	}
};

struct SpeedTestPositionHash
{
	std::size_t operator()(const SpeedTestPosition& obj) const
	{
		return obj.hash;
	}
};

struct SpeedTestValue
{
	int calltimes = 0;		// 调用次数
	int64_t elapsedTSC = 0; // 消耗CPU帧率
	int64_t elapsedMaxTSC = 0; // 消耗CPU最大帧率

	SpeedTestValue& operator += (const SpeedTestValue& other)
	{
		calltimes += other.calltimes;
		elapsedTSC += other.elapsedTSC;
		if (elapsedMaxTSC < other.elapsedMaxTSC)
		{
			elapsedMaxTSC = other.elapsedMaxTSC;
		}
		return *this;
	}
};

typedef std::unordered_map<SpeedTestPosition, SpeedTestValue, SpeedTestPositionHash> SpeedTestPositionMap;

struct SpeedTestData
{
	// 总的耗时 需要外部自己清理
	SpeedTestPositionMap position;

	// 单测试值大于改值时输出日志 微妙 <=0表示不使用这个值 
	int64_t greaterUSLog = 0;
};

class SpeedTest
{
public:
	// _name_ 参数必须是一个字面量
	SpeedTest(SpeedTestData& _testdata_, const char* _name_, int _index_);
	~SpeedTest();

protected:
	int64_t begin_tsc; // CPU频率值

	SpeedTestData& testdata;
	SpeedTestPosition testpos;
};

extern thread_local SpeedTestData g_default_speedtestdata;
extern bool g_record_speedtestdata;						// 是否记录测试数据 默认不记录 针对所有线程
extern thread_local bool g_record_speedtestdata_thread;	// 针对当前线程 默认不记录

#define __SpeedTestObjName(line)  speedtestobj_##line
#define _SpeedTestObjName(line)  __SpeedTestObjName(line)
#define SpeedTestObjName() _SpeedTestObjName(__LINE__)

#define SeedTestObject() \
	SpeedTest SpeedTestObjName()(g_default_speedtestdata, __FUNCTION__, __LINE__);

#endif