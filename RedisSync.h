#ifndef _REDISSYNC_H_
#define _REDISSYNC_H_

// by git@github.com:yuwf/redis.git

#include <memory>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "Redis.h"

// 同步调用，不支持多线程，IO默认是阻塞的
// 订阅命令类型的对象 只能调用SubScribe UnSubScribe Message命令，且IO都是非阻塞的
// 除订阅外 要保证消息发送和加收的对称性
class RedisSync : public Redis
{
public:
	RedisSync(bool subscribe = false);
	virtual ~RedisSync();

	bool InitRedis(const std::string& host, unsigned short port, const std::string& auth = "", int index = 0, bool bssl = false);
	void Close();

	// 执行命令 等待返回结果
	// 返回结果只表示协议解析或者网络读取的结果
	// 命令错误在rst中，结果会返回true
	bool DoCommand(const RedisCommand& cmd); // 不关心返回值
	bool DoCommand(const RedisCommand& cmd, RedisResult& rst);
	bool DoCommand(const std::vector<RedisCommand>& cmds, std::vector<RedisResult>& rst);

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
	// metricsprefix指标名前缀 内部产生指标如下，不包括[]
	//[metricsprefix]redissync_ops 调用次数
	//[metricsprefix]redissync_sendbytes 发送字节数
	//[metricsprefix]redissync_recvbytes 接受字节数
	//[metricsprefix]redissync_sendcost 发送时间 微秒
	//[metricsprefix]redissync_recvcost 接受时间 微秒
	// tags额外添加的标签，内部不产生标签
	enum SnapshotType { Json, Influx, Prometheus };
	static std::string Snapshot(SnapshotType type, const std::string& metricsprefix = "", const std::map<std::string,std::string>& tags = std::map<std::string, std::string>());

	//////////////////////////////////////////////////////////////////////////
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
	// PUBLISH命令
	// 返回接收到信息的订阅者数量 -1表示网络或其他错误
	int Publish(const std::string& channel, const std::string& msg);

protected:
	bool Connect();
	bool CheckConnect();

	// 发送命令 单条命令 和 多条命令
	template<class _Command_>
	bool SendAndCheckConnect(const _Command_& cmd)
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
	RedisSync(const RedisSync&) = delete;
	RedisSync& operator=(const RedisSync&) = delete;

public:
	// 辅助类接口 常用命令封装==================================================
	// 注 ：
	// 管道开启后 命令会缓存起来 下面的函数都返回-1
	// 开启订阅后下面的函数直接返回-1
	// 下面的函数返回值 1表示成功 0表示为空或者命令错误 -1表示者网络错误或者其他错误
	// 命令参考 http://doc.redisfans.com/ 函数的顺序最好按这个里面的来(String类型待完善)

	//////////////////////////////////////////////////////////////////////////
	// DEL命令 返回值表示删除的个数
	template<class KeyList>
	int Del(const KeyList& keys)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("DEL", keys), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// DUMP命令
	int Dump(const std::string& key, std::string& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("DUMP", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { value = rst.ToString(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// EXISTS和命令
	int Exists(const std::string& key)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("EXISTS", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// 过期相关命令
	// EXPIRE 秒
	int Expire(const std::string& key, long long value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("EXPIRE", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	// EXPIREAT 秒 时间戳
	int ExpireAt(const std::string& key, long long value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("EXPIREAT", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	// PEXPIRE 毫秒
	int PExpire(const std::string& key, long long value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("PEXPIRE", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	// PEXPIREAT 毫秒 时间戳
	int PExpireAt(const std::string& key, long long value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("PEXPIREAT", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	// PERSIST
	int Persist(const std::string& key)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("PERSIST", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	// TTL
	int TTL(const std::string& key, long long& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("TTL", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { value = rst.ToInt(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	// PTTL
	int PTTL(const std::string& key, long long& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("PTTL", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { value = rst.ToInt(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// KEYS命令
	template<class ValueList>
	int Keys(const std::string& pattern, ValueList& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("KEYS", pattern), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// MOVE命令
	int Move(const std::string& key, int index)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("MOVE", key, index), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.ToInt(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// RANDOMKEY命令
	int RandomKey(std::string& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("RANDOMKEY"), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { value = rst.ToString(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// RENAME命令
	int Rename(const std::string& key, const std::string& newkey)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("RENAME", key, newkey), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	int RenameNX(const std::string& key, const std::string& newkey)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("RENAMENX", key, newkey), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.ToInt(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SCAN 命令 返回值已经过滤cursor数据 只有数据部分
	template<class ValueList>
	int Scan(int cursor, const std::string& match, int count, int& rstcursor, ValueList& values)
	{
		RedisCommand cmd("SCAN", cursor);
		if (!match.empty()) cmd.Add("MATCH", match);
		if (count > 0) cmd.Add("COUNT", count);

		RedisResult rst;
		if (!DoCommand(cmd, rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray() && rst.ToArray().size() == 2)
		{
			const RedisResult::Array& ar = rst.ToArray();
			rstcursor = ar[0].Toint();
			ar[1].ToArray(values);
			return 1;
		}
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// TYPE命令
	// value in [none、string、list、set、zset、hash]
	int Type(const std::string& key, std::string& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("TYPE", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { value = rst.ToString(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}


	//////////////////////////////////////////////////////////////////////////
	// GET命令
	template<class VALUE>
	int Get(const std::string& key, VALUE& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("GET", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { Redis::string_to(rst.ToString(), value); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// INCR命令 svalue表示成功后的值
	int Incr(const std::string& key)
	{
		long long svalue = 0;
		return Incr(key, svalue);
	}
	int Incr(const std::string& key, long long& svalue)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("INCR", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { svalue = rst.ToInt(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	
	// INCRBY命令 svalue表示成功后的值
	int Incrby(const std::string& key, long long value)
	{
		long long svalue = 0;
		return Incrby(key, (long long)value, svalue);
	}
	template<class Value>
	int Incrby(const std::string& key, Value value, long long& svalue)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("INCRBY", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { svalue = rst.ToInt(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// MGET命令 若成功 rst为数组 元素为string或者null
	template<class KeyList, class ValueList>
	int MGet(const KeyList& keys, ValueList& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("MGET", keys), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// MSET命令
	template<class FieldValueMap>
	int MSet(const FieldValueMap& kvs)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("MSET", kvs), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SET命令 
	// ex(秒) 和 px(毫秒) 只能使用一个，另一个必须有-1, 否则优先使用ex
	template<class Value>
	int Set(const std::string& key, const Value& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false)
	{
		RedisCommand cmd("SET", key, value);
		if (ex != -1) cmd.Add("EX", ex);
		else if (px != -1) cmd.Add("PX", px);
		if (nx) cmd.Add("NX");

		RedisResult rst;
		if (!DoCommand(cmd, rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	//////////////////////////////////////////////////////////////////////////
	// HEDL命令 返回值表示删除的个数
	template<class Field>
	int HDel(const std::string& key, const Field& field)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HDEL", key, field), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HEXISTS命令
	int HExists(const std::string& key, const std::string& field)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HEXISTS", key, field), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HGET命令
	template<class Field, class Value>
	int HGet(const std::string& key, const Field& field, Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HGET", key, field), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { Redis::string_to(rst.ToString(), value); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HGETALL命令 若成功 rst为数组 元素为string或者null
	template<class FieldValueMap>
	int HGetAll(const std::string& key, FieldValueMap& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HGETALL", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToMap(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HINCRBY命令 svalue表示成功后的值
	template<class Field, class Value>
	int HIncrby(const std::string& key, const Field& field, Value value)
	{
		std::string svalue = 0;
		return HIncrby(key, field, value, svalue);
	}
	template<class Field, class Value>
	int HIncrby(const std::string& key, const Field& field, Value value, long long& svalue)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HINCRBY", key, field, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { svalue = rst.ToInt(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	
	// HKEYS命令 若成功 values为数组 元素为string
	template<class ValueList>
	int HKeys(const std::string& key, ValueList& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HKEYS", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HLEN命令
	int HLen(const std::string& key)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HLEN", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HMGET命令 若成功 values为数组 元素为string或者null
	template<class FieldList, class ValueList>
	int HMGet(const std::string& key, const FieldList& fields, ValueList& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HMGET", key, fields), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HMSET命令
	template<class FieldValueMap>
	int HMSet(const std::string& key, const FieldValueMap& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HMSET", key, values), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HSET命令
	template<class Field, class Value>
	int HSet(const std::string& key, const Field& field, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HSET", key, field, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HSETNX命令
	template<class Field, class Value>
	int HSetNX(const std::string& key, const Field& field, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HSETNX", key, field, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HVALS命令 若成功 rst为数组 元素为string
	template<class ValueList>
	int HVals(const std::string& key, ValueList& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("HVALS", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// HSCAN 返回值已经过滤cursor数据 只有数据部分
	template<class FieldValueMap>
	int HScan(const std::string& key, int cursor, const std::string& match, int count, int& rstcursor, FieldValueMap& values)
	{
		RedisCommand cmd("HSCAN", key, cursor);
		if (!match.empty()) cmd.Add("MATCH", match);
		if (count > 0) cmd.Add("COUNT", count);

		RedisResult rst;
		if (!DoCommand(cmd, rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray() && rst.ToArray().size() == 2)
		{
			const RedisResult::Array& ar = rst.ToArray();
			rstcursor = ar[0].Toint();
			ar[1].ToMap(values);
			return 1;
		}
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	//////////////////////////////////////////////////////////////////////////
	// LLEN命令 成功返回列表长度
	int LLen(const std::string& key)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LLEN", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// LPOP命令 value表示移除的元素
	int LPop(const std::string& key)
	{
		std::string rvalue;
		return LPop(key, rvalue);
	}
	template<class Value>
	int LPop(const std::string& key, Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LPOP", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { Redis::string_to(rst.ToString(), value); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// LPUSH命令 成功返回列表长度
	template<class Value>
	int LPush(const std::string& key, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LPUSH", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// LPUSHX命令 成功返回列表长度
	template<class Value>
	int LPushX(const std::string& key, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LPUSHX", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// LRANGE命令
	template<class ValueList>
	int LRange(const std::string& key, int start, int stop, ValueList& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LRANGE", key, start, stop), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// LREM命令 成功返回移除元素的个数
	template<class Value>
	int LRem(const std::string& key, int count, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LREM", key, count, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// LSet命令
	template<class Value>
	int LSet(const std::string& key, int index, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LSET", key, index, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// LTRIM命令
	int LTrim(const std::string& key, int start, int stop)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("LTRIM", key, start, stop), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// RPOP命令 value表示移除的元素
	int RPop(const std::string& key)
	{
		std::string value;
		return RPop(key, value);
	}
	template<class Value>
	int RPop(const std::string& key, Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("RPOP", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { Redis::string_to(rst.ToString(), value); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// RPOPLPUSH命令 value表示移动的元素
	int RPopLPush(const std::string& key, const std::string& keydest)
	{
		std::string value;
		return RPopLPush(key, value);
	}
	template<class Value>
	int RPopLPush(const std::string& key, const std::string& keydest, Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("RPOPLPUSH", key, keydest), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { Redis::string_to(rst.ToString(), value); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// RPUSH命令 成功返回列表长度
	template<class Value>
	int RPush(const std::string& key, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("RPUSH", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// RPUSHX命令 成功返回列表长度
	template<class Value>
	int RPushX(const std::string& key, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("RPUSHX", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	//////////////////////////////////////////////////////////////////////////
	// SADD命令
	template<class Value>
	int SAdd(const std::string& key, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SADD", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SCARD命令 成功后返回集合长度
	int SCard(const std::string& key)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SCARD", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SDIFF命令
	template<class KeyList, class ValueList>
	int SDiff(const KeyList& keys, ValueList& values)
	{
		if (keys.empty()) return 0;
		RedisResult rst;
		if (!DoCommand(RedisCommand("SDIFF", keys), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SINTER命令
	template<class KeyList, class ValueList>
	int Sinter(const KeyList& keys, ValueList& values)
	{
		if (keys.empty()) return 0;
		RedisResult rst;
		if (!DoCommand(RedisCommand("SINTER", keys), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// 	SISMEMBER命令
	template<class Value>
	int SISMember(const std::string& key, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SISMEMBER", key, value), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SMEMBERS命令 若成功 rst为数组 元素为string
	template<class ValueList>
	int SMembers(const std::string& key, ValueList& values)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SMEMBERS", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SPOP命令
	template<class VALUE>
	int SPop(const std::string& key, VALUE& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SPOP", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { Redis::string_to(rst.ToString(), value); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SRANDMEMBER命令
	template<class VALUE>
	int SRandMember(const std::string& key, VALUE& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SRANDMEMBER", key), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { Redis::string_to(rst.ToString(), value); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SREM命令 成功返回移除元素的数量
	template<class Value>
	int SRem(const std::string& key, const Value& value)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SREM", key, value), rst)){ return -1;}
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SUNION命令
	template<class KeyList, class ValueList>
	int SUnion(const KeyList& keys, ValueList& values)
	{
		if (keys.empty()) return 0;
		RedisResult rst;
		if (!DoCommand(RedisCommand("SUNION", keys), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsArray()) { rst.ToArray(values); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}


	//////////////////////////////////////////////////////////////////////////
	// EVAL命令 【结果为空也返回1】
	template<class KeyList, class ArgList>
	int Eval(const std::string& script, const KeyList& keys, const ArgList& args)
	{
		RedisResult rst;
		return Eval(script, keys, args, rst);
	}
	template<class KeyList, class ArgList>
	int Eval(const std::string& script, const KeyList& keys, const ArgList& args, RedisResult& rst)
	{
		if (!DoCommand(RedisCommand("EVAL", script, keys.size(), keys, args), rst)) { return -1; }
		if (rst.IsError()) { return 0; } // 命令错误 结果可能为空
		return 1;
	}

	// EVALSHA命令 【结果为空也返回1】
	template<class KeyList, class ArgList>
	int Evalsha(const std::string& scriptsha1, const KeyList& keys, const ArgList& args)
	{
		RedisResult rst;
		return Evalsha(scriptsha1, keys, args, rst);
	}
	template<class KeyList, class ArgList>
	int Evalsha(const std::string& scriptsha1, const KeyList& keys, const ArgList& args, RedisResult& rst)
	{
		if (!DoCommand(RedisCommand("EVALSHA", scriptsha1, keys.size(), keys, args), rst)) { return -1; }
		if (rst.IsError()) { return 0; } // 命令错误 结果可能为空
		return 1;
	}

	// SCRIPT EXISTS命令
	int ScriptExists(const std::string& scriptsha1)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SCRIPT", "EXISTS", scriptsha1), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsInt()) { return rst.Toint(); }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SCRIPT FLUSH命令
	int ScriptFlush()
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SCRIPT", "FLUSH"), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SCRIPT KILL命令
	int ScriptKill()
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SCRIPT", "KILL"), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString() && rst.ToString() == "OK") { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// SCRIPT LOAD命令
	int ScriptLoad(const std::string& script, std::string& scriptsha1)
	{
		RedisResult rst;
		if (!DoCommand(RedisCommand("SCRIPT", "LOAD", script), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { scriptsha1 = rst.ToString(); return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}
	int ScriptLoad(const std::string& script, RedisResult& rst)
	{
		if (!DoCommand(RedisCommand("SCRIPT", "LOAD", script), rst)) { return -1; }
		if (rst.IsError() || rst.IsNull()) { return 0; } // 命令错误或者结果为空
		if (rst.IsString()) { return 1; }
		else { RedisLogError("UnKnown Error"); return -1; }
	}

	// 执行脚本
	template<class KeyList, class ArgList>
	int Script(const RedisScript& script, const KeyList& keys, const ArgList& args)
	{
		RedisResult rst;
		return Script(script, keys, args, rst);
	}
	template<class KeyList, class ArgList>
	int Script(const RedisScript& script, const KeyList& keys, const ArgList& args, RedisResult& rst)
	{
		if (Evalsha(script.scriptsha1, keys, args, rst) == 1)
			return 1;
		if (rst.IsError() && rst.ToString().find("NOSCRIPT ") != std::string::npos) // 如果是脚本不存在的错误
		{
			rst.Clear();
			return Eval(script.script, keys, args, rst);
		}
		return 0;
	}
};


#endif