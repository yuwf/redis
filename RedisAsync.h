﻿#ifndef _REDISASYNC_H_
#define _REDISASYNC_H_

// by git@github.com:yuwf/redis.git

#include <memory>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <functional>
#include "Redis.h"

// 基于非阻塞的异步
// 异步调用，不支持多线程，IO默认是非阻塞的
class RedisAsync : public Redis
{
public:
	// 回调函数 ok只表示命令有没有执行，错误与否需要使用rst来判断
	typedef std::function<void(bool ok, const RedisResult& rst)> CallBack;
	typedef std::function<void(bool ok, const std::vector<RedisResult>& rst)> MultiCallBack;
	// 队列命令
	struct QueueCmd
	{
		// 单条命令放第一个位置
		std::vector<RedisCommand> cmd;
		std::vector<RedisResult> rst;
		CallBack callback = nullptr;
		MultiCallBack multicallback = nullptr;
	};
	typedef std::shared_ptr<QueueCmd> QueueCmdPtr;

public:
	RedisAsync(bool subscribe = false);
	virtual ~RedisAsync();

	bool InitRedis(const std::string& host, unsigned short port, const std::string& auth = "", int index = 0, bool bssl = false);
	void Close();

	// 发送命令 不等待结果
	// 返回结果为true 才会产生回调，否则不会回调
	// 返回结果只表示是否写入命令列表中
	// 外层调用UpdateReply来读取结果并回调
	bool SendCommand(const RedisCommand& cmd, const CallBack& callback = nullptr);
	bool SendCommand(RedisCommand&& cmd, const CallBack& callback = nullptr);
	bool SendCommand(const std::vector<RedisCommand>& cmds, const MultiCallBack& callback = nullptr);
	bool SendCommand(std::vector<RedisCommand>&& cmds, const MultiCallBack& callback = nullptr);

	// 执行脚本
	template<class KeyList, class ArgList>
	bool Script(const RedisScript& script, const KeyList& keys, const ArgList& args, const CallBack& callback = nullptr)
	{
		return SendCommand(RedisCommand("EVALSHA", script.scriptsha1, keys.size(), keys, args), [&, script, keys, args, callback](bool ok, const RedisResult& rst)
		{
			if (rst.IsError() && rst.ToString().find("NOSCRIPT ") != std::string::npos) // 如果是脚本不存在的错误
			{
				if (!SendCommand(RedisCommand("EVAL", script.script, keys.size(), keys, args), callback)) // 直接执行Eval 他也会加载脚本
				{
					if (callback) // 因为是在回调中 需要保证callback的调用
					{
						static RedisResult s_result;
						callback(false, s_result);
					}
				}
			}
			else
			{
				if (callback)
					callback(ok, rst);
			}
		});
	}

	// 接受命令 命令结果回调
	void UpdateReply();

	// 命令队列是否为空
	bool Empty() const { return m_queuecommands.empty(); }

	// 订阅相关，Redis发生了重连会自动重新订阅之前订阅的频道
	// SUBSCRIBE 命令
	// 返回false表示解析失败或者网络读取失败 返回true表示开启订阅
	bool SubScribe(const std::string& channel);
	// UNSUBSCRIBE 命令
	// channel 为空表示取消所有订阅
	bool UnSubScribe(const std::string& channel);
	// PSUBSCRIBE 命令
	// 返回false表示解析失败或者网络读取失败 返回true表示开启订阅
	bool PSubScribe(const std::string& pattern);
	// PUNSUBSCRIBE 命令
	// channel 为空表示取消所有订阅
	bool PUnSubScribe(const std::string& pattern);
	// 获取订阅的消息 返回值-1:网络错误或者其他错误, 0表示没有消息, 1表示收到消息
	// 参数block表示是否阻塞直到收到消息 但不一定是订阅消息 可能是注册或者取消订阅消息
	int Message(std::string& channel, std::string& msg, bool block = false);

	// 连接信息
	const std::string& Host() const { return m_host; }
	unsigned short Port() const { return m_port; }
	const std::string& Auth() const { return m_auth; }
	int Index() const { return m_index; }
	bool SSL() const { return m_bssl; }

	// 统计使用
	int64_t Ops() const { return m_ops; }
	int64_t SendBytes() const { return m_sendbytes; }
	int64_t RecvBytes() const { return m_recvbytes; }
	int64_t SendCost() const { return m_sendcost; }
	int64_t RecvCost() const { return m_recvcost; }
	int64_t NetIOCost() const { return m_sendcost + m_recvcost; }
	void ResetOps() { m_ops.store(0); m_sendbytes.store(0); m_recvbytes.store(0); m_sendcost.store(0); m_recvcost.store(0); }

	// 快照数据
	// 【参数metricsprefix和tags 不要有相关格式禁止的特殊字符 内部不对这两个参数做任何格式转化】
	// metricsprefix指标名前缀 内部产生指标如下
	// [metricsprefix]redisasync_ops 调用次数
	// [metricsprefix]redisasync_sendbytes 发送字节数
	// [metricsprefix]redisasync_recvbytes 接受字节数
	// [metricsprefix]redisasync_sendcost 发送时间 微秒
	// [metricsprefix]redisasync_recvcost 接受时间 微秒
	// tags额外添加的标签，内部不产生标签
	enum SnapshotType { Json, Influx, Prometheus };
	static std::string Snapshot(SnapshotType type, const std::string& metricsprefix = "", const std::map<std::string, std::string>& tags = std::map<std::string, std::string>());

protected:
	bool Connect();
	bool CheckConnect();

	// 发送命令 单条命令 和 多条命令
	template<class QueueCmd>
	bool SendAndCheckConnect(const QueueCmd& cmd)
	{
		if (!CheckConnect()) return false;
		if (Send(cmd)) return true;
		if (!Connect()) return false; // 尝试连接下
		return Send(cmd);
	}
	bool Send(const RedisCommand& cmd);
	bool Send(const std::vector<RedisCommand>& cmd);

	// buff表示数据地址 buff中包括\r\n minlen表示buff中不包括\r\n的最少长度
	// 返回值表示buff长度 -1:网络读取失败 0:没有读取到
	virtual int ReadToCRLF(char** buff, int mindatalen) override;

	virtual bool ReadRollback(int len) override;

	void ClearRecvBuff();
	void ResetRecvBuff();

	boost::asio::io_service m_ioservice;
	boost::asio::ip::tcp::socket m_socket;
	boost::asio::ssl::context m_context;
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_sslsocket;

	bool m_bconnected = false;	// 是否已连接

	// 数据接受buff
	std::vector<char> m_recvbuff;
	int m_recvpos = 0;
	std::array<char, 2048> m_inbuff;

	std::list<QueueCmdPtr> m_queuecommands;

	// 标记订阅使用，订阅的Redis无法发送其他命令
	const bool m_subscribe;
	enum SubscribeState
	{
		SubscribeInvalid = 0,
		SubscribeSend = 1,		// 客户端订阅
		SubscribeRecv = 2,		// 服务器返回定语成功
		UnSubscribeSend = 3,	// 客户端取消订阅
	};
	std::map<std::string, SubscribeState> m_channel; // 订阅列表
	std::map<std::string, SubscribeState> m_pattern; // 模式订阅列表

	// 连接信息
	std::string m_host;
	unsigned short m_port = 0;
	std::string m_auth;
	int m_index = 0;
	bool m_bssl = false;

	// 统计使用
	std::atomic<int64_t> m_ops = { 0 };
	std::atomic<int64_t> m_sendbytes = { 0 };
	std::atomic<int64_t> m_recvbytes = { 0 };
	std::atomic<int64_t> m_sendcost = { 0 }; // 耗时 微妙
	std::atomic<int64_t> m_recvcost = { 0 };

private:
	// 禁止拷贝
	RedisAsync(const RedisAsync&) = delete;
	RedisAsync& operator=(const RedisAsync&) = delete;
};

#endif