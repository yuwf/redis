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

	// 发送命令
	void SendCommand(const RedisCommand& cmd, const RedisAsync::CallBack& callback = nullptr);
	void SendCommand(RedisCommand&& cmd, const RedisAsync::CallBack& callback = nullptr);
	void SendCommand(const std::vector<RedisCommand>& cmds, const RedisAsync::MultiCallBack& callback = nullptr);
	void SendCommand(std::vector<RedisCommand>&& cmds, const RedisAsync::MultiCallBack& callback = nullptr);

	template<class KeyList, class ArgList>
	void Script(const RedisScript& script, const KeyList& keys, const ArgList& args, const RedisAsync::CallBack& callback = nullptr)
	{
		RedisAsync::CallBack dispatchcallback; // RedisAsyncThread线程调用，把callback放到m_dispatch的线程中调用
		if (callback)
		{
			dispatchcallback = [this, callback](bool ok, const RedisResult& rst)
			{
				if (m_dispatch)
					m_dispatch([callback, ok, rst](){ callback(ok, rst); });
			};
		}
		auto task = [this, script, keys, args, dispatchcallback]()
		{
			if (!m_redis.Script(script, keys, args, dispatchcallback))
			{
				if (dispatchcallback)
				{
					static RedisResult s_result;
					dispatchcallback(false, s_result);
				}
			}
		};
		std::lock_guard<std::mutex> lock(m_mutex);
		m_tasks.push_back(task);
		m_condition.notify_one();
	}

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