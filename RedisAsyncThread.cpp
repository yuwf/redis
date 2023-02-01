
#include "RedisAsyncThread.h"

RedisAsyncThread::RedisAsyncThread()
{
}

RedisAsyncThread::~RedisAsyncThread()
{

}

bool RedisAsyncThread::Init(const std::string& host, unsigned short port, const std::string& auth, int index, std::function< void(std::function< void()>)> dispath, bool bssl)
{
	if (!m_redis.InitRedis(host, port, auth, index, bssl))
	{
		return false;
	}

	m_dispatch = dispath;

	// 开启线程
	m_thread.reset(new std::thread([this]() { this->Run(); }));
	return true;
}

void RedisAsyncThread::Stop()
{
	m_stop = true;
	m_condition.notify_one();
}

void RedisAsyncThread::Join()
{
	if (m_thread && m_thread->joinable())
		m_thread->join();
}

void RedisAsyncThread::SendCommand(const RedisCommand& cmd, const RedisAsync::CallBack& callback)
{
	RedisAsync::CallBack dispatchcallback; // RedisAsyncThread线程调用，把callback放到m_dispatch的线程中调用
	if (callback)
	{
		dispatchcallback = [this, callback](bool ok, const RedisResult& rst)
		{
			if (m_dispatch)
				m_dispatch([callback, ok, rst]()->void { callback(ok, rst); });
		};
	}
	auto task = [this, cmd, dispatchcallback]()
	{
		if (!m_redis.SendCommand(cmd, dispatchcallback))
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

void RedisAsyncThread::SendCommand(RedisCommand&& cmd, const RedisAsync::CallBack& callback)
{
	RedisAsync::CallBack dispatchcallback; // RedisAsyncThread线程调用，把callback放到m_dispatch的线程中调用
	if (callback)
	{
		dispatchcallback = [this, callback](bool ok, const RedisResult& rst)
		{
			if (m_dispatch)
				m_dispatch([callback, ok, rst]()->void { callback(ok, rst); });
		};
	}
	auto task = [this, c = std::forward<RedisCommand>(cmd), dispatchcallback]() mutable
	{
		if (!m_redis.SendCommand(std::forward<RedisCommand>(c), dispatchcallback))
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

void RedisAsyncThread::SendCommand(const std::vector<RedisCommand>& cmds, const RedisAsync::MultiCallBack& callback)
{
	RedisAsync::MultiCallBack dispatchcallback; // RedisAsyncThread线程调用，把callback放到m_dispatch的线程中调用
	if (callback)
	{
		dispatchcallback = [this, callback](bool ok, const std::vector<RedisResult>& rst)
		{
			if (m_dispatch)
				m_dispatch([callback, ok, rst]()->void { callback(ok, rst); });
		};
	}
	if (cmds.empty())
	{
		RedisLogError("RedisAsyncThread commands is empty");
		if (dispatchcallback)
		{
			static std::vector<RedisResult> s_result;
			dispatchcallback(false, s_result);
		}
	}
	auto task = [this, cmds, dispatchcallback]()
	{
		if (!m_redis.SendCommand(cmds, dispatchcallback))
		{
			if (dispatchcallback)
			{
				static std::vector<RedisResult> s_result;
				dispatchcallback(false, s_result);
			}
		}
	};

	std::lock_guard<std::mutex> lock(m_mutex);
	m_tasks.push_back(task);
	m_condition.notify_one();
}

void RedisAsyncThread::SendCommand(std::vector<RedisCommand>&& cmds, const RedisAsync::MultiCallBack& callback)
{
	RedisAsync::MultiCallBack dispatchcallback; // RedisAsyncThread线程调用，把callback放到m_dispatch的线程中调用
	if (callback)
	{
		dispatchcallback = [this, callback](bool ok, const std::vector<RedisResult>& rst)
		{
			if (m_dispatch)
				m_dispatch([callback, ok, rst]()->void { callback(ok, rst); });
		};
	}
	if (cmds.empty())
	{
		RedisLogError("RedisAsyncThread commands is empty");
		if (dispatchcallback)
		{
			static std::vector<RedisResult> s_result;
			dispatchcallback(false, s_result);
		}
	}
	auto task = [this, c = std::forward<std::vector<RedisCommand>>(cmds), dispatchcallback]() mutable
	{
		if (!m_redis.SendCommand(std::forward<std::vector<RedisCommand>>(c), dispatchcallback))
		{
			if (dispatchcallback)
			{
				static std::vector<RedisResult> s_result;
				dispatchcallback(false, s_result);
			}
		}
	};

	std::lock_guard<std::mutex> lock(m_mutex);
	m_tasks.push_back(task);
	m_condition.notify_one();
}


void RedisAsyncThread::Run()
{
	while (!m_stop)
	{
		Task task;
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_condition.wait(lock, [this] { return this->m_stop || !this->m_tasks.empty() || !m_redis.Empty(); });

			if (m_stop)
			{
				m_redis.Close();
				return;
			}
			if (!m_tasks.empty())
			{
				task = m_tasks.front();
				m_tasks.pop_front();
			}
		}
		if (task)
		{
			task();
		}

		m_redis.UpdateReply();
	}
}
