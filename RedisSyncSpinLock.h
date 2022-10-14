#ifndef _REDISSYNCSPINLOCK_H_
#define _REDISSYNCSPINLOCK_H_

// by git@github.com:yuwf/redis.git

#include <unordered_map>
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
	std::atomic<int64_t> lockcount = { 0 };		// 加锁次数
	std::atomic<int64_t> faillockcount = { 0 };
	std::atomic<int64_t> trylockTSC = { 0 };	// 尝试加锁tsc
	std::atomic<int64_t> trylockMaxTSC = { 0 };
	std::atomic<int64_t> lockedTSC = { 0 };		// 加锁tsc 针对加锁成功的
	std::atomic<int64_t> lockedMaxTSC = { 0 };
	std::atomic<int64_t> spincount = { 0 };		// 自旋锁
};

class RedisSpinLockRecord
{
public:
	RedisSpinLockData* Reg(const std::string& key);

	// 快照数据
	// 【参数metricsprefix和tags 不要有相关格式禁止的特殊字符 内部不对这两个参数做任何格式转化】
	// metricsprefix指标名前缀 内部产生指标如下
	// [metricsprefix]redissyncspinlock_lockcount 加锁次数
	// [metricsprefix]redissyncspinlock_faillockcount 失败次数
	// [metricsprefix]redissyncspinlock_trylock 尝试加锁的时间 微秒
	// [metricsprefix]redissyncspinlock_maxtrylock 尝试加锁的最大时间 微秒
	// [metricsprefix]redissyncspinlock_locked 加锁的时间 微秒
	// [metricsprefix]redissyncspinlock_maxlocked 加锁的最大时间 微秒
	// [metricsprefix]redissyncspinlock_spincount 加锁时自旋的次数
	// tags额外添加的标签，内部产生标签 key:加锁的key名
	enum SnapshotType { Json, Influx, Prometheus };
	std::string Snapshot(SnapshotType type, const std::string& metricsprefix = "", const std::map<std::string, std::string>& tags = std::map<std::string, std::string>());

	void SetRecord(bool b) { brecord = b; }

protected:
	// 记录的测试数据
	typedef std::unordered_map<std::string, RedisSpinLockData*> RedisSpinLockDataMap;
	shared_mutex mutex;
	RedisSpinLockDataMap records;

	// 是否记录测试数据
	bool brecord = true;

};

extern RedisSpinLockRecord g_redisspinlockrecord;


#endif