#ifndef _REDISASYNCTHREAD_H_
#define _REDISASYNCTHREAD_H_

// by git@github.com:yuwf/redis.git

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
	void Command(const std::string& cmdname, const RedisAsync::CallBack& callback = nullptr)
	{
		if (cmdname.empty()) return;
		SendCommand(RedisCommand(cmdname), callback);
	}

	template<class T1>
	void Command(const std::string& cmdname, const T1& t1, const RedisAsync::CallBack& callback = nullptr)
	{
		if (cmdname.empty()) return;
		SendCommand(RedisCommand(cmdname, t1), callback);
	}

	template<class T1, class T2>
	void Command(const std::string& cmdname, const T1& t1, const T2& t2, const RedisAsync::CallBack& callback = nullptr)
	{
		if (cmdname.empty()) return;
		SendCommand(RedisCommand(cmdname, t1, t2), callback);
	};

	template<class T1, class T2, class T3>
	void Command(const std::string& cmdname, const T1& t1, const T2& t2, const T3& t3, const RedisAsync::CallBack& callback = nullptr)
	{
		if (cmdname.empty()) return;
		SendCommand(RedisCommand(cmdname, t1, t2, t3), callback);
	};

	template<class T1, class T2, class T3, class T4>
	void Command(const std::string& cmdname, const T1& t1, const T2& t2, const T3& t3, const T4& t4, const RedisAsync::CallBack& callback = nullptr)
	{
		if (cmdname.empty()) return;
		SendCommand(RedisCommand(cmdname, t1, t2, t3, t4), callback);
	};

	template<class T1, class T2, class T3, class T4, class T5>
	void Command(const std::string& cmdname, const T1& t1, const T1& t2, const T1& t3, const T1& t4, const T1& t5, const RedisAsync::CallBack& callback = nullptr)
	{
		if (cmdname.empty()) return;
		SendCommand(RedisCommand(cmdname, t1, t2, t3, t4, t5), callback);
	};

	template<class T1, class T2, class T3, class T4, class T5, class T6>
	void Command(const std::string& cmdname, const T1& t1, const T2& t2, const T3& t3, const T4& t4, const T5& t5, const T6& t6, const RedisAsync::CallBack& callback = nullptr)
	{
		if (cmdname.empty()) return;
		SendCommand(RedisCommand(cmdname, t1, t2, t3, t4, t5, t6), callback);
	}

	void SendCommand(const RedisCommand& cmd, const RedisAsync::CallBack& callback = nullptr);
	void SendCommand(const std::vector<RedisCommand>& cmds, const RedisAsync::MultiCallBack& callback = nullptr);

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