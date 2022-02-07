#ifndef _REDISASYNCTHREAD_H_
#define _REDISASYNCTHREAD_H_

// by yuwf qingting.water@gmail.com

#include "RedisAsync.h"
#include<condition_variable>
#include<thread>
#include<mutex>

// 异步调用，内部另起一个线程
// 基于RedisAsync实现
class RedisAsyncThread
{
public:
	RedisAsyncThread();
	virtual ~RedisAsyncThread();

	//初始化Redis
	bool Init(const std::string& host, unsigned short port, const std::string& auth, int index, std::function<void(std::function< void()>)> dispath, bool bssl = false);
	void Stop();
	void Join();

	// 投递命令
	// 命令样式 "set key 123"
	void Command(const std::string& str, const RedisAsync::CallBack& callback = nullptr);
	void Command(const std::vector<std::string>& strs, const RedisAsync::MultiCallBack& callback = nullptr);

private:
	void Run();

	std::function<void(std::function<void()>)>	m_dispatch;
	RedisAsync									m_redis;
	std::shared_ptr<std::thread>				m_thread;
	bool										m_stop = false;

	typedef std::function<void()> Task;
	std::mutex									m_mutex;
	std::condition_variable_any					m_condition;
	std::list<Task>								m_tasks;

private:
	// 禁止拷贝
	RedisAsyncThread(const RedisAsyncThread&) = delete;
	RedisAsyncThread& operator=(const RedisAsyncThread&) = delete;
};

#endif