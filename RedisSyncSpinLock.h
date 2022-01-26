#ifndef _REDISSYNCSPINLOCK_H_
#define _REDISSYNCSPINLOCK_H_

// by yuwf qingting.water@gmail.com

#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include "RedisSync.h"

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
	int64_t m_failTSC = 0;
	int64_t m_lockTSC = 0;	// 加锁成功时CPU频率值
	int m_spinCount = 0;	// 自旋次数
};


// 用于记录统计锁的情况
struct RedisSpinLockData
{
	RedisSpinLockData() {}
	RedisSpinLockData(const RedisSpinLockData& other)
		: lockcount(other.lockcount.load())
		, faillockcount(other.faillockcount.load())
		, trylockTSC(other.trylockTSC.load())
		, trylockMaxTSC(other.trylockMaxTSC.load())
		, lockedTSC(other.lockedTSC.load())
		, lockedMaxTSC(other.lockedMaxTSC.load())
		, spincount(other.spincount.load())
	{
	}

	std::atomic<int64_t> lockcount = { 0 };		// 加锁次数
	std::atomic<int64_t> faillockcount = { 0 };
	std::atomic<int64_t> trylockTSC = { 0 };	// 尝试加锁tsc
	std::atomic<int64_t> trylockMaxTSC = { 0 };
	std::atomic<int64_t> lockedTSC = { 0 };		// 加锁tsc 针对加锁成功的
	std::atomic<int64_t> lockedMaxTSC = { 0 };
	std::atomic<int64_t> spincount = { 0 };		// 自旋锁

	RedisSpinLockData& operator += (const RedisSpinLockData& other)
	{
		lockcount += other.lockcount;
		faillockcount += other.faillockcount;
		trylockTSC += other.trylockTSC;
		int64_t maxtsc = other.trylockMaxTSC.load();
		if (trylockMaxTSC < maxtsc)
		{
			trylockMaxTSC = maxtsc;
		}
		lockedTSC += other.lockedTSC;
		maxtsc = other.lockedMaxTSC.load();
		if (lockedMaxTSC < maxtsc)
		{
			lockedMaxTSC = maxtsc;
		}
		spincount += other.spincount;
		return *this;
	}

	RedisSpinLockData& operator = (const RedisSpinLockData& other)
	{
		lockcount = other.lockcount.load();
		faillockcount = other.faillockcount.load();
		trylockTSC = other.trylockTSC.load();
		trylockMaxTSC = other.trylockMaxTSC.load();
		lockedTSC = other.lockedTSC.load();
		lockedMaxTSC = other.lockedMaxTSC.load();
		spincount = other.spincount.load();
		return *this;
	}
};

typedef std::unordered_map<std::string, RedisSpinLockData*> RedisSpinLockDataMap;

class RedisSpinLockRecord
{
public:
	RedisSpinLockData* Reg(const std::string& key);

	// 快照数据
	// 【参数metricsprefix和tags 不要有相关格式禁止的特殊字符 内部不对这两个参数做任何格式转化】
	// metricsprefix指标名前缀 内部产生指标如下
	// metricsprefix_lockcount 加锁次数
	// metricsprefix_faillockcount 失败次数
	// metricsprefix_trylock 尝试加锁的时间 微秒
	// metricsprefix_maxtrylock 尝试加锁的最大时间 微秒
	// metricsprefix_locked 加锁的时间 微秒
	// metricsprefix_maxlocked 加锁的最大时间 微秒
	// metricsprefix_spincount 加锁时自旋的次数
	// tags额外添加的标签，内部产生标签 key:加锁的key名
	enum SnapshotType { Json, Influx, Prometheus };
	std::string Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags = std::map<std::string, std::string>());

	void SetRecord(bool b) { brecord = b; }

protected:
	friend class RedisSyncSpinLocker;
	// 记录的测试数据
	boost::shared_mutex mutex;
	RedisSpinLockDataMap records;

	// 是否记录测试数据
	bool brecord = true;

};

extern RedisSpinLockRecord g_redisspinlockrecord;


#endif