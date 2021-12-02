#ifndef __REDISASYNCFAKE__
#define __REDISASYNCFAKE__
#include<condition_variable>
#include<thread>
#include<mutex>
#include "RedisSync.h"
#include<functional>

// 伪异步调用，另起一个线程同步调用
//基于同步实现

class RedisAsyncFake
{
public:
	using CommandCallBack = std::function<void(bool bSuccess, std::shared_ptr<RedisResult>)>;
	using CommandsCallBack = std::function<void(bool bSuccess, std::shared_ptr< std::vector<RedisResult> >)>;

	struct CommandAndCallBackBase
	{
		virtual ~CommandAndCallBackBase() {};
		virtual int Type() = 0;
	};

	struct CommandAndCallBack : public CommandAndCallBackBase
	{
		virtual int Type() { return 1; }

		std::string m_cmd;
		CommandCallBack m_callback = nullptr;

		CommandAndCallBack() {}
		CommandAndCallBack(const std::string& cmd, CommandCallBack callback) :m_cmd(cmd), m_callback(callback) {}
	};

	struct CommandsAndCallBack : public CommandAndCallBackBase
	{
		virtual int Type() { return 2; }

		std::vector<std::string> m_cmd;
		CommandsCallBack m_callback = nullptr;

		CommandsAndCallBack() {}
		CommandsAndCallBack(const std::vector<std::string>& cmd, CommandsCallBack callback) :m_cmd(cmd), m_callback(callback) {}
	};

public:
	RedisAsyncFake();
	virtual ~RedisAsyncFake();

	//初始化Redis
	bool Init(const std::string& host, unsigned short port, const std::string& auth, int index, std::function<void(std::function< void()>)> dispath);

	//投递命令
	void Command(const std::string& command, const CommandCallBack& callback = nullptr);
	void Commands(const std::vector<std::string>& command, const CommandsCallBack& callback = nullptr);

	void Stop();

	void Join();

	bool IsNormal();//设置打开 且连接正常

private:
	void RunLoop();

	CommandAndCallBackBase* Pop();

	void Execute(CommandAndCallBack* task);
	void Execute(CommandsAndCallBack* task);

	bool										 m_init = false;
	std::function< void(std::function< void()>)> m_dispatch;
	RedisSync							         m_redisClient;
	std::shared_ptr<std::thread>		         m_thread;
	bool                                         m_stop = false;
	std::mutex                                   m_mutex;
	std::condition_variable_any			         m_condition;
	std::list<CommandAndCallBackBase*>		     m_commondsWithCallBack;

private:
	// 禁止拷贝
	RedisAsyncFake(const RedisAsyncFake&) = delete;
	RedisAsyncFake& operator=(const RedisAsyncFake&) = delete;
};

#endif