#ifndef _REDISSYNCHLOCK_H_
#define _REDISSYNCHLOCK_H_

#include <unordered_map>
#include <chrono>
#include <thread>
#include "RedisSync.h"
#include "SpeedTest.h"

class RedisSynchLock
{
public:
	RedisSynchLock(RedisSync& redis) : m_redis(redis) {};

	// 范围锁 setnx实现 不支持递归 加锁失败直接返回不会等待
	bool ScopedLock(const std::string& key, unsigned int millisec = 8000);
	bool ScopedUnLock(const std::string& key);

	// 递归锁 dic实现 支持递归
	bool RecursiveLock(const std::string& key);
	bool RecursiveUnLock(const std::string& key);

protected:
	RedisSync& m_redis;
};

// 用于记录统计锁的情况
struct RedisSynchLockTrace
{
	std::string key;
	bool locking = false;
	int64_t beginTSC = 0;	// 进入时CPU频率值
	int64_t lockTSC = 0;	// 加锁后的CPU频率值
	int spincount = 0;		// 自旋锁 自旋次数
};

struct RedisSynchLockData
{
	int lockcount = 0;		// 加锁次数
	int faillockcount = 0;
	int64_t trylockTSC = 0;// 尝试加锁tsc
	int64_t trylockMaxTSC = 0;
	int64_t lockedTSC = 0;	// 加锁tsc 针对加锁成功的
	int64_t lockedMaxTSC = 0;
	int spincount = 0;		// 自旋锁

	RedisSynchLockData& operator += (const RedisSynchLockData& other)
	{
		lockcount += other.lockcount;
		faillockcount += other.faillockcount;
		trylockTSC += other.trylockTSC;
		if (trylockMaxTSC < other.trylockMaxTSC)
		{
			trylockMaxTSC = other.trylockMaxTSC;
		}
		lockedTSC += other.lockedTSC;
		if (lockedMaxTSC < other.lockedMaxTSC)
		{
			lockedMaxTSC = other.lockedMaxTSC;
		}
		spincount += other.spincount;
		return *this;
	}
};

typedef std::unordered_map<std::string, RedisSynchLockData> RedisSynchLockRecord;
extern thread_local RedisSynchLockRecord g_default_redissynchlockdata;
extern bool g_record_redissynchlockdata;						// 是否记录锁统计数据 默认不记录 全部线程
extern thread_local bool g_record_redissynchlockdata_thread;	// 当前线程是否记录 默认不记录

// 自旋锁
class RedisSpinLocker
{
public:
	// millisec表示锁的过期时间 也是尝试加锁时间
	RedisSpinLocker(RedisSync& redis, const std::string& key, unsigned int millisec = 8000);

	~RedisSpinLocker();

	explicit operator bool() const { return trace.locking; }

protected:
	RedisSynchLock lock;
	RedisSynchLockTrace trace;
};


#endif