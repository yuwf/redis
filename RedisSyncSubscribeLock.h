﻿#ifndef _REDISSYNCSUBSCIRBELOCK_H_
#define _REDISSYNCSUBSCIRBELOCK_H_

// by git@github.com:yuwf/redis.git

#include <unordered_map>
#include <functional>
#include <thread>
#include "RedisSync.h"

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




#endif