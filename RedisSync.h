#ifndef _REDISSYNC_H_
#define _REDISSYNC_H_

// by yuwf qingting.water@gmail.com

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

	// 命令样式 "set key 123"
	// 见DoCommand解释
	bool Command(const std::string& str);
	bool Command(const std::string& str, RedisResult& rst);
	bool Command(const char* format, ...);
	bool Command(RedisResult& rst, const char* format, ...);
	bool Command(const std::vector<std::string>& strs);
	bool Command(const std::vector<std::string>& strs, std::vector<RedisResult>& rst);

	// 执行命令 等待返回结果
	// 返回结果只表示协议解析或者网络读取的结果
	// 命令错误在rst中，结果会返回true
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
	// metricsprefix指标名前缀 内部产生指标如下
	// metricsprefix_ops 调用次数
	// metricsprefix_sendbytes 发送字节数
	// metricsprefix_recvbytes 接受字节数
	// metricsprefix_sendcost 发送时间 微秒
	// metricsprefix_recvcost 接受时间 微秒
	// tags额外添加的标签，内部不产生标签
	enum SnapshotType { Json, Influx, Prometheus };
	static std::string Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string,std::string>& tags = std::map<std::string, std::string>());

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

	friend class RedisSyncPipeline;

public:
	// 辅助类接口==================================================
	// 注 ：
	// 管道开启后 命令会缓存起来 下面的函数都返回-1
	// 开启订阅后下面的函数直接返回-1

	// DEL命令
	// 返回值表示删除的个数 -1表示网络或者其他错误
	int Del(const std::string& key);
	int Del(const std::vector<std::string>& key);

	// EXISTS和命令
	// 返回1存在 0不存在 -1表示网络或者其他错误
	int Exists(const std::string& key);

	// 过期相关命令
	// 返回1成功 0失败 -1表示网络或其他错误
	int Expire(const std::string& key, long long value);
	int ExpireAt(const std::string& key, long long value);
	int PExpire(const std::string& key, long long value);
	int PExpireAt(const std::string& key, long long value);
	int TTL(const std::string& key, long long& value);
	int PTTL(const std::string& key, long long& value);

	//////////////////////////////////////////////////////////////////////////
	// SET命令 
	// 返回1成功 0不成功 -1表示网络或其他错误
	// ex(秒) 和 px(毫秒) 只能使用一个，另一个必须有-1, 否则优先使用ex
	template<class Value>
	int Set(const std::string& key, const Value& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false);
	// GET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int Get(const std::string& key, std::string& value);
	template<class VALUE>
	int Get(const std::string& key, VALUE& value)
	{
		std::string rst;
		int r = Get(key, rst);
		if (r != 1)
			return r;
		Redis::string_to(rst, value);
		return r;
	}

	// MSET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int MSet(const std::map<std::string, std::string>& kvs);
	template<class FieldValueMap>
	int MSet(const FieldValueMap& kvs)
	{
		std::map<std::string, std::string> kvs_;
		for (auto it = kvs.begin(); it != kvs.end(); ++it)
			kvs_[Redis::to_string(it->first)] = Redis::to_string(it->second);
		return MSet(kvs_);
	}

	// MGET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string或者null
	int MGet(const std::vector<std::string>& keys, RedisResult& rst);
	template<class RstList>
	int MGet(const std::vector<std::string>& keys, RstList& rst)
	{
		RedisResult rst_;
		int r = MGet(keys, rst_);
		if (r != 1)
			return r;
		rst_.ToArray(rst);
		return 1;
	}

	// INCR命令
	// 返回1成功 0不成功 -1表示网络或其他错误 svalue表示成功后的值
	int Incr(const std::string& key, long long& svalue);
	int Incr(const std::string& key, int& svalue);
	int Incr(const std::string& key);

	// INCRBY命令
	// 返回1成功 0不成功 -1表示网络或其他错误 svalue表示成功后的值
	int Incrby(const std::string& key, long long value, long long& svalue);
	int Incrby(const std::string& key, long long value);
	template<class Value>
	int Incrby(const std::string& key, Value value, long long& svalue)
	{
		return Incrby(key, (long long)value, svalue);
	}
	template<class Value>
	int Incrby(const std::string& key, Value value)
	{
		return Incrby(key, (long long)value);
	}

	// HSET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int HSet(const std::string& key, const std::string& field, const std::string& value);
	template<class Field, class Value>
	int HSet(const std::string& key, const Field& field, const Value& value)
	{
		return HSet(key, Redis::to_string(field), Redis::to_string(value));
	}

	// HGET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int HGet(const std::string& key, const std::string& field, std::string& value);
	template<class Field, class Value>
	int HGet(const std::string& key, const Field& field, Value& value)
	{
		std::string rst;
		int r = HGet(key, Redis::to_string(field), rst);
		if (r != 1)
			return r;
		Redis::string_to(rst, value);
		return r;
	}

	// HMSET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int HMSet(const std::string& key, const std::map<std::string, std::string>& kvs);
	template<class FieldValueMap>
	int HMSet(const std::string& key, const FieldValueMap& kvs)
	{
		std::map<std::string, std::string> kvs_;
		for (auto it = kvs.begin(); it != kvs.end(); ++it)
			kvs_[Redis::to_string(it->first)] = Redis::to_string(it->second);
		return HMSet(key, kvs_);
	}

	// HMGET命令
	// 返回1成功 0不成功 - 1表示网络或其他错误
	// 若成功 rst为数组 元素为string或者null
	int HMGet(const std::string& key, const std::vector<std::string>& fields, RedisResult& rst);
	template<class FieldList, class RstList>
	int HMGet(const std::string& key, const FieldList& fields, RstList& rst)
	{
		std::vector<std::string> fields_;
		fields_.reserve(fields.size());
		for (auto it = fields.begin(); it != fields.end(); ++it)
			fields_.emplace_back(Redis::to_string(*it));
		RedisResult rst_;
		int r = HMGet(key, fields_, rst_);
		if (r != 1)
			return r;
		rst_.ToArray(rst);
		return 1;
	}

	// HKEYS命令
	// 返回1成功 0不成功 - 1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int HKeys(const std::string& key, RedisResult& rst); // string数组
	template<class RstList>
	int HKeys(const std::string& key, RstList& rst)
	{
		RedisResult rst_;
		int r = HKeys(key, rst_);
		if (r != 1)
			return r;
		rst_.ToArray(rst);
		return 1;
	}

	// HVALS命令
	// 返回1成功 0不成功 - 1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int HVals(const std::string& key, RedisResult& rst); // string数组
	template<class RstList>
	int HVals(const std::string& key, RstList& rst)
	{
		RedisResult rst_;
		int r = HKeys(key, rst_);
		if (r != 1)
			return r;
		rst_.ToArray(rst);
		return 1;
	}

	// HGETALL命令
	// 返回1成功 0不成功 - 1表示网络或其他错误
	// 若成功 rst为数组 元素为string或者null
	int HGetAll(const std::string& key, RedisResult& rst);
	template<class FieldValueMap>
	int HGetAll(const std::string& key, FieldValueMap& rst)
	{
		RedisResult rst_;
		int r = HGetAll(key, rst_);
		if (r != 1)
			return r;
		rst_.ToMap(rst);
		return 1;
	}

	// HINCRBY命令
	// 返回1成功 0不成功 -1表示网络或其他错误 svalue表示成功后的值
	int HIncrby(const std::string& key, const std::string& field, long long value, long long& svalue);
	int HIncrby(const std::string& key, const std::string& field, long long value);
	template<class Field, class Value>
	int HIncrby(const std::string& key, const Field& field, Value value, long long& svalue)
	{
		return HIncrby(key, Redis::to_string(field), (long long)value, svalue);
	}
	template<class Field, class Value>
	int HIncrby(const std::string& key, const Field& field, Value value)
	{
		return HIncrby(key, Redis::to_string(field), (long long)value);
	}
	
	// HLEN命令
	// 返回列表长度 0不成功 -1表示网络或其他错误
	int HLen(const std::string& key);

	// HSCAN
	// 返回1成功 0不成功 -1表示网络或其他错误
	// 返回值已经过滤cursor数据 只有数据部分
	int HScan(const std::string& key, int cursor, const std::string& match, int count, int& rstcursor, RedisResult& rst);
	template<class FieldValueMap>
	int HScan(const std::string& key, int cursor, const std::string& match, int count, int& rstcursor, FieldValueMap& rst)
	{
		RedisResult rst_;
		int r = HScan(key, cursor, match, count, rstcursor, rst_);
		if (r != 1)
			return r;
		rst_.ToMap(rst);
		return 1;
	}

	// HEXISTS命令
	// 返回1存在 0不存在 -1表示网络或其他错误
	int HExists(const std::string& key, const std::string& field);

	// HEDL命令
	// 返回值表示删除的个数 -1表示网络或其他错误
	int HDel(const std::string& key, const std::string& field);
	int HDels(const std::string& key, const std::vector<std::string>& fields);
	template<class Field>
	int HDel(const std::string& key, const Field& field)
	{
		return HDel(key, Redis::to_string(field));
	}
	template<class FieldList>
	int HDels(const std::string& key, const FieldList& fields)
	{
		std::vector<std::string> fields_;
		fields_.reserve(fields.size());
		for (auto it = fields.begin(); it != fields.end(); ++it)
			fields_.emplace_back(Redis::to_string(*it));
		return HDels(key, fields_);
	}

	// LPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络或其他错误
	int LPush(const std::string& key, const std::string& value);
	int LPushs(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	int LPush(const std::string& key, const Value& value)
	{
		return LPush(key, Redis::to_string(value));
	}
	template<class ValueList>
	int LPushs(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return LPushs(key, values_);
	}

	// RPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络或其他错误
	int RPush(const std::string& key, const std::string& value);
	int RPushs(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	int RPush(const std::string& key, const Value& value)
	{
		return RPush(key, Redis::to_string(value));
	}
	template<class ValueList>
	int RPushs(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return RPushs(key, values_);
	}

	// LPOP命令
	// 1成功 0不成功 -1表示网络或其他错误
	// value表示移除的元素
	int LPop(const std::string& key);
	int LPop(const std::string& key, std::string& value);
	template<class Value>
	int LPop(const std::string& key, Value& value)
	{
		std::string rst;
		int r = LPop(key, rst);
		if (r != 1)
			return r;
		Redis::string_to(rst, value);
		return r;
	}

	// RPOP命令
	// 1成功 0不成功 -1表示网络或其他错误
	// value表示移除的元素
	int RPop(const std::string& key);
	int RPop(const std::string& key, std::string& value);
	template<class Value>
	int RPop(const std::string& key, Value& value)
	{
		std::string rst;
		int r = RPop(key, rst);
		if (r != 1)
			return r;
		Redis::string_to(rst, value);
		return r;
	}

	// LRANGE命令
	// 1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int LRange(const std::string& key, int start, int stop, RedisResult& rst);
	template<class ValueList>
	int LRange(const std::string& key, int start, int stop, ValueList& rst)
	{
		RedisResult rst_;
		int r = LRange(key, start, stop, rst_);
		if (r != 1)
			return r;
		rst_.ToArray(rst);
		return r;
	}

	// LREM命令
	// 成功返回移除元素的个数 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int LRem(const std::string& key, int count, std::string& value);

	// LTRIM命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int LTrim(const std::string& key, int start, int stop);

	// LLEN命令
	// 返回列表长度 0元素为空或者不成功 -1表示网络或其他错误
	int LLen(const std::string& key);

	//////////////////////////////////////////////////////////////////////////
	// SADD命令
	// 成功返回添加的数量 0不成功 -1表示网络或其他错误
	template<class Value>
	int SAdd(const std::string& key, const Value& value);
	template<class ValueList>
	int SAdds(const std::string& key, const ValueList& values);
	// SCARD命令
	// 返回集合长度 0元素为空或者不成功 -1表示网络或其他错误
	int SCard(const std::string& key);

	// SREM命令
	// 成功返回移除元素的数量 0不成功 -1表示网络或其他错误
	int SRem(const std::string& key, const std::string& value);
	int SRems(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	int SRem(const std::string& key, const Value& value)
	{
		return SRem(key, Redis::to_string(value));
	}
	template<class ValueList>
	int SRems(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return SRems(key, values_);
	}

	// SINTER命令
	// 1成功 0不成功 -1表示网络或其他错误
	int Sinter(const std::vector<std::string>& keys, RedisResult& rst);
	template<class ValueList>
	int Sinter(const std::vector<std::string>& keys, ValueList& rst)
	{
		RedisResult rst_;
		int r = Sinter(keys, rst_);
		if (r != 1)
			return r;
		rst_.ToArray(rst);
		return 1;
	}

	// SMEMBERS命令
	// 1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int SMembers(const std::string& key, RedisResult& rst);
	template<class ValueList>
	int SMembers(const std::string& key, ValueList& rst)
	{
		RedisResult rst_;
		int r = SMembers(key, rst_);
		if (r != 1)
			return r;
		rst_.ToArray(rst);
		return 1;
	}

	// 	SISMEMBER命令
	// 返回1存在 0不存在 -1表示网络或其他错误
	int SISMember(const std::string& key, const std::string& value);
	template<class Value>
	int SISMember(const std::string& key, const Value& value)
	{
		return SISMember(key, Redis::to_string(value));
	}

	//////////////////////////////////////////////////////////////////////////
	// EVAL命令
	// 1成功 0不成功 -1表示网络或其他错误
	int Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args);
	int Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst);
	// EVALSHA命令
	// 1成功 0不成功 -1表示网络或其他错误
	int Evalsha(const std::string& scriptsha1, const std::vector<std::string>& keys, const std::vector<std::string>& args);
	int Evalsha(const std::string& scriptsha1, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst);
	// SCRIPT EXISTS命令
	// 返回1存在 0不存在 -1表示网络或者其他错误
	int ScriptExists(const std::string& scriptsha1);
	// SCRIPT FLUSH命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int ScriptFlush();
	// SCRIPT KILL命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int ScriptKill();
	// SCRIPT LOAD命令
	// 1成功 0不成功 -1表示网络或其他错误
	int ScriptLoad(const std::string& script, std::string& scriptsha1);
};

template<class Value>
int RedisSync::Set(const std::string& key, const Value& value, unsigned int ex, unsigned int px, bool nx)
{
	RedisCommand cmd;
	cmd.Add("SET");
	cmd.Add(key);
	cmd.Add(Redis::to_string(value));
	if (ex != -1)
	{
		cmd.Add("EX");
		cmd.Add(ex);
	}
	else if (px != -1)
	{
		cmd.Add("PX");
		cmd.Add(px);
	}
	if (nx)
	{
		cmd.Add("NX");
	}

	// 如果 key 已经存储其他值， SET 就覆写旧值，且无视类型
	// 返回OK 或者nil
	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return -1;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

template<class Value>
int RedisSync::SAdd(const std::string& key, const Value& value)
{
	RedisCommand cmd;
	cmd.Add("SADD");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

template<class ValueList>
int RedisSync::SAdds(const std::string& key, const ValueList& values)
{
	RedisCommand cmd;
	cmd.Add("SADD");
	cmd.Add(key);
	for (const auto& it : values)
	{
		cmd.Add(Redis::to_string(it));
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

#endif