#ifndef _REDISSYNCSPINLOCK_H_
#define _REDISSYNCSPINLOCK_H_

// by yuwf qingting.water@gmail.com

#include <unordered_map>
#include <chrono>
#include <thread>
#include "RedisSync.h"
#include "SpeedTest.h"

// 自旋锁，外部需要自旋检测
class RedisSyncSpinLock
{
public:
	RedisSyncSpinLock(RedisSync& redis) : m_redis(redis) {};

	// 范围锁 setnx实现 不支持递归 加锁失败直接返回不会等待
	bool ScopedLock(const std::string& key, unsigned int maxlockmsec = 8000);
	bool ScopedUnLock(const std::string& key);

	// 递归锁 dic实现 支持递归
	bool RecursiveLock(const std::string& key, unsigned int maxlockmsec = 8000);
	bool RecursiveUnLock(const std::string& key);

protected:
	RedisSync& m_redis;

	bool DoScirpt(const std::vector<std::string>& keys, const std::vector<std::string>& args, const std::string& script, std::string& scriptsha1, boost::shared_mutex& m);
};

// 自旋锁
class RedisSyncSpinLocker
{
public:
	// maxlockmsec表示锁的过期时间 最大加锁时间， waitmsec表示自旋等待时间
	RedisSyncSpinLocker(RedisSync& redis, const std::string& key, unsigned int maxlockmsec = 8000, unsigned int waitmsec = 8000);

	~RedisSyncSpinLocker();

	explicit operator bool() const { return m_locking; }

protected:
	RedisSyncSpinLock m_lock;

	std::string m_key;		// 加锁的Key
	bool m_locking = false;	// 是否已加锁
	int64_t m_beginTSC = 0;	// 开始加锁时CPU频率值
	int64_t m_lockTSC = 0;	// 加锁成功时CPU频率值
	int m_spinCount = 0;	// 自旋次数
};


// 用于记录统计锁的情况
struct RedisSpinLockData
{
	int lockcount = 0;		// 加锁次数
	int faillockcount = 0;
	int64_t trylockTSC = 0;// 尝试加锁tsc
	int64_t trylockMaxTSC = 0;
	int64_t lockedTSC = 0;	// 加锁tsc 针对加锁成功的
	int64_t lockedMaxTSC = 0;
	int spincount = 0;		// 自旋锁

	RedisSpinLockData& operator += (const RedisSpinLockData& other)
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

typedef std::unordered_map<std::string, RedisSpinLockData> RedisSpinLockRecord;
extern thread_local RedisSpinLockRecord g_default_redisspinlockdata;
extern bool g_record_redisspinlockdata;						// 是否记录锁统计数据 默认不记录 全部线程
extern thread_local bool g_record_redisspinlockdata_thread;	// 当前线程是否记录 默认不记录


#endif