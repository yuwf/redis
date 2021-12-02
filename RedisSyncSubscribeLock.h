#ifndef _REDISSYNCSUBSCIRBELOCK_H_
#define _REDISSYNCSUBSCIRBELOCK_H_

// by yuwf qingting.water@gmail.com

#include <unordered_map>
#include <functional>
#include <thread>
#include "RedisSync.h"
#include "SpeedTest.h"

class RedisSyncSubscribeLock
{
public:
	RedisSyncSubscribeLock(RedisSync& redis);

	// 根据构造函数中redis中的地址初始化一个注册订阅的Redis
	bool Init();

	void Update();

	// dic实现
	void ScopedLock(const std::string& key, std::function<void()> callback, std::function<void()> failcallback, unsigned int maxlockmsec = 8000, unsigned int waitmsec = 8000);

protected:
	bool ScopedLock(const std::string& key, const std::string& lockid, unsigned int maxlockmsec = 8000);
	bool ScopedCheckLock(const std::string& key);
	bool ScopedUnLock(const std::string& key, const std::string& lockid);

	bool DoScirpt(const std::vector<std::string>& keys, const std::vector<std::string>& args, const std::string& script, std::string& scriptsha1, boost::shared_mutex& m);

	RedisSync& m_redis; // 注册订阅事件
	RedisSync m_subscriberedis; // 注册订阅事件

	const std::string m_channel;
	struct WaitLock
	{
		std::string key;
		std::string lockid;
		unsigned int maxlockmsec = 0;
		unsigned int waitmsec = 0;
		int64_t beginTSC = 0;			// 开始加锁时CPU频率值
		std::function<void()> callback;	// 加锁后的回调
		std::function<void()> failcallback;
	};
	std::unordered_map<std::string, WaitLock> m_waitlocks; // lockid:Lock

	int64_t m_increment = 1;// 自增变量 用户生成锁id
	int64_t m_lasttsc = 0;	// 用户检查超时
};

class RedisSyncSubscirbeLocker
{
public:
	RedisSyncSubscirbeLocker(RedisSyncSubscribeLock& subscribe) : m_subscribe(subscribe) {};

protected:
	RedisSyncSubscribeLock& m_subscribe;
};


#endif