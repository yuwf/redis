
#include "RedisAsyncFake.h"

#define LogError

RedisAsyncFake::RedisAsyncFake():m_isOpen(true),m_connectSuccess(false)
{
	
}

RedisAsyncFake::~RedisAsyncFake()
{

}

bool RedisAsyncFake::InitAsyncRedisAndStart(const std::string& host, unsigned short port, const std::string& auth, std::function< void(std::function< void()>)> dispath)
{
	if (!m_redisClient.InitRedis(host, port, auth))
	{
		LLOG_ERROR("AsyncRedis init Error Ip %s port %d  auth:%s ", host.c_str(), port, auth.c_str() );
		return false;
	}

	m_dispatch = dispath;
	m_connectSuccess = true;

	// 开启线程
	m_thread.reset(new std::thread([this]() { this->RunLoop(); }));
	return true;
}

void RedisAsyncFake::PushCommandWithCallBack(const std::string& command, const CommandCallBack& callback  )
{
	if (!m_isOpen || command.empty() || !m_connectSuccess)
	{
		//这里改一下 保证即使有故障 回调也能调用
		LLOG_ERROR("RedisAsyncFake Push Error ");
		static std::shared_ptr<RedisResult> s_result = std::make_shared<RedisResult>();
		if (callback && m_dispatch)
		{
			m_dispatch([cb = callback]()->void {
				cb(false, s_result);
			});
		}
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	CommandAndCallBack* p = new CommandAndCallBack(command, callback);
	m_commondsWithCallBack.emplace_back(p);
	m_condition.notify_one();
}

void RedisAsyncFake::PushCommandsWithCallBack(const std::vector<std::string>& command, const CommandsCallBack& callback)
{
	if (!m_isOpen || command.empty() || !m_connectSuccess)
	{
		//这里改一下 保证即使有故障 回调也能调用
		LLOG_ERROR("RedisAsyncFake Push Error ");
		static std::shared_ptr< std::vector<RedisResult> > s_result = std::make_shared< std::vector<RedisResult> >();
		if (callback && m_dispatch)
		{
			m_dispatch([cb = callback]()->void {
				cb(false, s_result);
			});
		}
	}
	std::lock_guard<std::mutex> lock(m_mutex);
	CommandsAndCallBack* p = new CommandsAndCallBack(command, callback);
	m_commondsWithCallBack.emplace_back(p);
	m_condition.notify_one();
}

void RedisAsyncFake::Stop()
{
	m_stop = true;
	m_condition.notify_one();
}

void RedisAsyncFake::Join()
{
	if (m_thread && m_thread->joinable())
		m_thread->join();
}

void RedisAsyncFake::RunLoop()
{
	while (!m_stop)
	{
		CommandAndCallBackBase* task = Pop();
		if (task)
		{
			if (task->Type() == 1)
			{
				Execute((CommandAndCallBack*)task);
			}
			else if (task->Type() == 2)
			{
				Execute((CommandsAndCallBack*)task);
			}
			delete task;
		}
	}
}

RedisAsyncFake::CommandAndCallBackBase* RedisAsyncFake::Pop()
{
	//这里必须是unique_lock 因为只有unique_lock会在wait的时候释放掉锁，满足条件的时候再加锁
	std::unique_lock<std::mutex> lock(m_mutex);
	m_condition.wait(lock, [this] {  return this->m_stop || !this->m_commondsWithCallBack.empty(); });
	if (m_stop )
		return NULL;// stop就退出 剩余的就不处理了
	if (m_commondsWithCallBack.empty())
		return NULL;
	CommandAndCallBackBase* task = m_commondsWithCallBack.front();
	m_commondsWithCallBack.pop_front();
	return task;
}

void RedisAsyncFake::Execute(CommandAndCallBack* task)
{
	std::shared_ptr<RedisResult> result = std::make_shared<RedisResult>();
	bool bSuccess = m_redisClient.Command(task->m_cmd, *result);
	if (task->m_callback && m_dispatch)
	{
		m_dispatch([bSuccess, result, cb = task->m_callback]()->void {
		cb(bSuccess, result);
		});
	}
}

void RedisAsyncFake::Execute(CommandsAndCallBack* task)
{
	std::shared_ptr< std::vector<RedisResult> > result = std::make_shared< std::vector<RedisResult> >();

	bool bSuccess = false;
	bSuccess = m_redisClient.PipelineBegin();
	if (bSuccess)
	{
		for (const auto& it : task->m_cmd)
		{
			RedisResult empty;
			m_redisClient.Command(it, empty);
		}
		bSuccess = m_redisClient.PipelineCommit(*result);
	}
	
	if (task->m_callback && m_dispatch)
	{
		m_dispatch([bSuccess, result, cb = task->m_callback]()->void {
			cb(bSuccess, result);
		});
	}
}

void RedisAsyncFake::SetIsOpen(bool bOpen)
{
	m_isOpen = bOpen;
}

bool RedisAsyncFake::IsNormal()
{
	return m_isOpen && m_connectSuccess;
}
