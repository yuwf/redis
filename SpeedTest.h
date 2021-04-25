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

// ��ȡCPU��ǰƵ��ֵ
FORCE_INLINE int64_t TSC()
{
	// rdtsc
	return (int64_t)__rdtsc();

	// rdtscp
	//uint32_t aux;
	//return (int64_t)__rdtscp(&aux);
}

// ��ȡCPU΢���Ƶ��
extern int64_t TSCPerUS();

struct SpeedTestPosition
{
	const char* name = NULL;	// ����λ���� Ҫ�����������
	int index = 0;				// ����λ�ú�

	std::size_t hash = 0;		// ����ʱֱ�Ӽ����hash

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
	int calltimes = 0;		// ���ô���
	int64_t elapsedTSC = 0; // ����CPU֡��
	int64_t elapsedMaxTSC = 0; // ����CPU���֡��

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
	// �ܵĺ�ʱ ��Ҫ�ⲿ�Լ�����
	SpeedTestPositionMap position;

	// ������ֵ���ڸ�ֵʱ�����־ ΢�� <=0��ʾ��ʹ�����ֵ 
	int64_t greaterUSLog = 0;
};

class SpeedTest
{
public:
	// _name_ ����������һ��������
	SpeedTest(SpeedTestData& _testdata_, const char* _name_, int _index_);
	~SpeedTest();

protected:
	int64_t begin_tsc; // CPUƵ��ֵ

	SpeedTestData& testdata;
	SpeedTestPosition testpos;
};

extern thread_local SpeedTestData g_default_speedtestdata;
extern bool g_record_speedtestdata;						// �Ƿ��¼�������� Ĭ�ϲ���¼ ��������߳�
extern thread_local bool g_record_speedtestdata_thread;	// ��Ե�ǰ�߳� Ĭ�ϲ���¼

#define __SpeedTestObjName(line)  speedtestobj_##line
#define _SpeedTestObjName(line)  __SpeedTestObjName(line)
#define SpeedTestObjName() _SpeedTestObjName(__LINE__)

#define SeedTestObject() \
	SpeedTest SpeedTestObjName()(g_default_speedtestdata, __FUNCTION__, __LINE__);

#endif