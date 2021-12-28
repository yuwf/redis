
#include "RedisAsyncThread.h"

RedisAsyncThread::RedisAsyncThread()
{
}

RedisAsyncThread::~RedisAsyncThread()
{

}

bool RedisAsyncThread::Init(const std::string& host, unsigned short port, const std::string& auth, int index, std::function< void(std::function< void()>)> dispath)
{
	if (!m_redis.InitRedis(host, port, auth, index))
	{
		return false;
	}

	m_dispatch = dispath;

	// 开启线程
	m_thread.reset(new std::thread([this]() { this->Run(); }));

	m_redis.SetGlobalCallBack([this](bool ok, const RedisAsync::QueueCmdPtr& cmdptr)
	{
		if (cmdptr && m_dispatch)
		{
			m_dispatch([ok, cmdptr]()->void
			{
				if (cmdptr->type == 0)
				{
					if (cmdptr->callback)
					{
						static RedisResult s_result;
						cmdptr->callback(ok, cmdptr->rst.empty() ? s_result : cmdptr->rst[0]);
					}
				}
				else if (cmdptr->type == 1 && cmdptr->cmd.size() == cmdptr->rst.size())
				{
					if (cmdptr->multicallback)
					{
						cmdptr->multicallback(ok, cmdptr->rst);
					}
				}
			});
		}
	});
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

void RedisAsyncThread::Command(const std::string& str, const RedisAsync::CallBack& callback)
{
	if (str.empty())
	{
		//这里改一下 保证即使有故障 回调也能调用
		LogError("RedisAsyncThread command is empty");
		if (callback && m_dispatch)
		{
			m_dispatch([callback]()->void
			{
				static RedisResult s_result;
				callback(false, s_result);
			});
		}
	}

	auto task = [this, str, callback]() {
		if (!m_redis.Command(str, callback))
		{
			if (callback && m_dispatch)
			{
				m_dispatch([callback]()->void
				{
					static RedisResult s_result;
					callback(false, s_result);
				});
			}
		}
	};

	std::lock_guard<std::mutex> lock(m_mutex);
	m_tasks.push_back(task);
	m_condition.notify_one();
}

void RedisAsyncThread::Command(const std::vector<std::string>& strs, const RedisAsync::MultiCallBack& callback)
{
	if (strs.empty())
	{
		LogError("RedisAsyncThread commands is empty");
		if (callback && m_dispatch)
		{
			m_dispatch([callback]()->void
			{
				static std::vector<RedisResult> s_result;
				callback(false, s_result);
			});
		}
	}
	auto task = [this, strs, callback]() {
		if (!m_redis.Command(strs, callback))
		{
			if (callback && m_dispatch)
			{
				m_dispatch([callback]()->void
				{
					static std::vector<RedisResult> s_result;
					callback(false, s_result);
				});
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
