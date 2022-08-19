#ifndef _CLOCK_H_
#define _CLOCK_H_

// by git@github.com:yuwf/clock.git

// 时钟
#include <stdlib.h>
#if defined(_MSC_VER)
#include <intrin.h>  
#else
#include <x86intrin.h>
#endif
#include <string>

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define	FORCE_INLINE inline __attribute__((always_inline))
#endif

// https://github.com/gcc-mirror/gcc/blob/master/gcc/config/i386/ia32intrin.h
// https://msdn.microsoft.com/en-us/library/twchhe95%28v=vs.100%29.aspx
// https://gcc.gnu.org/onlinedocs/gcc/Machine-Constraints.html
// https://en.wikipedia.org/wiki/Time_Stamp_Counter
// https://msdn.microsoft.com/en-us/library/bb385235.aspx

// 获取CPU当前频率值
FORCE_INLINE int64_t TSC()
{
	// rdtsc
	return (int64_t)__rdtsc();

	// rdtscp
	// uint32_t aux;
	//return (int64_t)__rdtscp(&aux);
}

// 获取CPU妙的频率
extern int64_t TSCPerS();
// 获取CPU毫妙的频率
extern int64_t TSCPerMS();
// 获取CPU微妙的频率
extern int64_t TSCPerUS();

// PointTime时间点，依赖的是chrono::system_clock
struct PTime
{
public:
	// 默认使用当前时间构造
	PTime();
	// 用以秒为单位的时间戳构造
	PTime(int secs);

	// 静态成员，获取秒数时间戳
	static int Now();

	// 秒数时间戳
	int Secs() const { return (int)(ts / 1000); }
	// 秒数时间戳
	int64_t MSecs() const { return ts; }

	// 设置时间戳 单位秒
	void SetSecs(int secs);
	// 设置时间戳 单位毫秒
	void SetMSecs(int64_t msecs);
	// 设置为当前系统时间
	void SetNow();

	// tm结构
	const tm& TM() const { return _tm; }
	// 获取年份部分 2022
	int Year() const { return _tm.tm_year+1900; }
	// 获取月份部分 [0, 11]
	int Month() const { return _tm.tm_mon; }
	// 这一年中的第几天 [0-365]
	int YDay() const { return _tm.tm_yday; }
	// 获取几号 [1-31]
	int MDay() const { return _tm.tm_mday; }
	// 获取星期 [0-6]
	int WDay() const { return _tm.tm_wday; }
	// 获取小时部分 [0-23]
	int Hour() const { return _tm.tm_hour; }
	// 获取分钟部分 [0-59]
	int Min() const { return _tm.tm_min; }
	// 获取秒数部分 [0-59]
	int Sec() const { return _tm.tm_sec; }
	// 获取毫秒部分 [0-999]
	int MSec() const { return (int)(ts % 1000); }

	// 比较操作
	bool operator > (const PTime& rt) const { return ts > rt.ts; };
	bool operator >= (const PTime& rt) const { return ts >= rt.ts; };
	bool operator < (const PTime& rt) const { return ts < rt.ts; };
	bool operator <= (const PTime& rt) const { return ts <= rt.ts; };

	// 获取年月日格式为  20160220
	int GetDate() const { return (_tm.tm_year + 1900) * 10000 + (_tm.tm_mon + 1) * 100 + _tm.tm_mday; }

	// 获取时间描述格式2022-02-22 10:07:37
	std::string ToString() const;
	// 格式 2022-02-22 10:07:37
	bool FromString(const std::string& str);

protected:
	// 时间戳 精确到毫秒
	int64_t ts = 0;
	// 时间结构体 和时区有关
	tm		_tm;
};

struct Ticker
{
	// 间隔tsc
	int64_t interval = 0;
	// 开始tsc
	int64_t last = 0;

	Ticker(int64_t milliseconds)
	{	
		interval = TSCPerMS() * milliseconds;
		last = TSC() - rand()%interval; // 防止太固定了
	}

	bool Hit()
	{
		int64_t now = TSC();
		if (now - last >= interval)
		{
			last = now;
			return true;
		}
		return false;
	}

	// 固定间隔 如果间隔时间长 可Hit多次
	bool HitFix()
	{
		int64_t now = TSC();
		if (now - last >= interval)
		{
			last += interval;
			return true;
		}
		return false;
	}

	explicit operator bool()
	{
		return Hit();
	}
};


#endif