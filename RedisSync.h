#ifndef _REDISSYNC_H_
#define _REDISSYNC_H_

#include <memory>
#include <boost/asio.hpp>
#include "Redis.h"

// 同步调用，不支持多线程 yuwf
// 除了开启管道外，每个命令函数都支持原子性操作
// 订阅命令不能和其他非订阅命令一起使用

class RedisSync : public Redis
{
public:
	RedisSync();
	virtual ~RedisSync();
public:
	bool InitRedis(const std::string& host, unsigned short port, const std::string& auth = "", int index = 0);
	void Close();

	// 返回false表示解析失败或者网络读取失败
	bool Command(const std::string& cmd, RedisResult& rst);
	bool Command(RedisResult& rst, const char* cmd, ...);

	// 订阅使用 开启订阅后，执行任何命令都直接返回false或者-1
	// 返回false表示解析失败或者网络读取失败 返回true表示开启订阅
	// 若Redis发生了重连，订阅自动取消，外部可以Message函数返回值判断，然后重新订阅
	bool SubScribe(const std::string& channel);
	bool SubScribes(int cnt, ...);
	// 获取订阅的消息 返回-1表示网络错误, -2表示未开启订阅，0表示没有消息, 1表示收到消息
	// 参数block表示是否阻塞直到收到订阅消息
	int Message(std::string& channel, std::string& msg, bool block);
	// 取消订阅
	bool UnSubScribe();

	// 统计使用
	int64_t Ops() const { return m_ops; }
	int64_t SendBytes() const { return m_sendbytes; }
	int64_t RecvBytes() const { return m_recvbytes; }
	int64_t SendCost() const { return m_sendcost; }
	int64_t RecvCost() const { return m_recvcost; }
	int64_t NetIOCostTSC() const { return m_sendcosttsc + m_recvcosttsc; }
	void ResetOps() { m_ops = m_sendbytes = m_recvbytes = m_sendcost = m_recvcost = m_sendcosttsc = m_recvcosttsc = 0; }

protected:
	bool Connect();
	bool CheckConnect();

	// 返回false表示解析失败或者网络读取失败
	bool DoCommand(const std::vector<std::string>& buff, RedisResult& rst);

	// 发送命令
	bool SendCommand(const std::string& cmdbuff);
	bool SendCommandAndCheckConnect(const std::string& cmdbuff);

	// 返回值表示长度 buff表示数据地址 -1表示网络读取失败 0表示没有读取到
	// buff中包括\r\n minlen表示buff中不包括\r\n的最少长度 
	virtual int ReadToCRLF(char** buff, int mindatalen) override;

	void ClearRecvBuff();
	void ResetRecvBuff();

	boost::asio::io_service m_ioservice;
	boost::asio::ip::tcp::socket m_socket;
	bool m_bconnected = false;	// 是否已连接

	// 数据接受buff
	std::vector<char> m_recvbuff;
	int m_recvpos = 0;

	std::string m_host;
	unsigned short m_port = 0;
	std::string m_auth;
	int m_index = 0;

	// 订阅使用
	bool m_subscribe = false;

	// 统计使用
	int64_t m_ops = 0;
	int64_t m_sendbytes = 0;
	int64_t m_recvbytes = 0;
	int64_t m_sendcost = 0; // 耗时 微妙
	int64_t m_recvcost = 0;
	int64_t m_sendcosttsc = 0; // 耗时 CPU频率
	int64_t m_recvcosttsc = 0;

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

	// SET命令 
	// 返回1成功 0不成功 -1表示网络或其他错误
	// ex(秒) 和 px(毫秒) 只能使用一个，另一个必须有-1, 否则优先使用ex
	int Set(const std::string& key, const std::string& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false);
	template<class Value>
	int Set(const std::string& key, const Value& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false)
	{
		return Set(key, Redis::to_string(value), ex, px, nx);
	}

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
	int Incrby(const std::string& key, int value, long long& svalue);
	int Incrby(const std::string& key, int value, int& svalue);
	int Incrby(const std::string& key, int value);

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
	int HIncrby(const std::string& key, const std::string& field, int value, long long& svalue);
	int HIncrby(const std::string& key, const std::string& field, int value, int& svalue);
	int HIncrby(const std::string& key, const std::string& field, int value);

	// HLEN命令
	// 返回列表长度 0不成功 -1表示网络或其他错误
	int HLen(const std::string& key);

	// HSCAN
	// 返回1成功 0不成功 -1表示网络或其他错误 svalue表示成功后的值
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

	// SADD命令
	// 成功返回添加的数量 0不成功 -1表示网络或其他错误
	int SAdd(const std::string& key, const std::string& value);
	int SAdds(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	int SAdd(const std::string& key, const Value& value)
	{
		return SAdd(key, Redis::to_string(value));
	}
	template<class ValueList>
	int SAdds(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return SAdds(key, values_);
	}

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

	// SCARD命令
	// 返回集合长度 0元素为空或者不成功 -1表示网络或其他错误
	int SCard(const std::string& key);

	// EVAL命令
	// 1成功 0不成功 -1表示网络或其他错误
	int Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args);
	int Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst);
};

class RedisSyncPipeline
{
public:
	RedisSyncPipeline(RedisSync& redis) : m_redis(redis) {};
	virtual ~RedisSyncPipeline() {}

	// 字符串命令
	RedisResultBind& Command(const std::string& cmd); // 绑定值 根据命令来确定

	// DEL命令
	RedisResultBind& Del(const std::string& key);	// int 删除个数
	RedisResultBind& Del(const std::vector<std::string>& key);

	RedisResultBind& Exists(const std::string& key); // int 0不存在 1存在

	RedisResultBind& Expire(const std::string& key, long long value);	//int 0成功 1失败
	RedisResultBind& ExpireAt(const std::string& key, long long value);
	RedisResultBind& PExpire(const std::string& key, long long value);
	RedisResultBind& PExpireAt(const std::string& key, long long value);

	RedisResultBind& TTL(const std::string& key);	//long long 剩余的过期时间
	RedisResultBind& PTTL(const std::string& key);

	// SET 命令
	// ex(秒) 和 px(毫秒) 只能使用一个，另一个必须有-1, 否则优先使用ex
	RedisResultBind& Set(const std::string& key, const std::string& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false); // string OK 或者 nil
	template<class Value>
	RedisResultBind& Set(const std::string& key, const Value& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false)
	{
		return Set(key, Redis::to_string(value), ex, px, nx);
	}
	RedisResultBind& Get(const std::string& key); // string

	// MSET命令
	RedisResultBind& MSet(const std::map<std::string, std::string>& kvs); // string OK
	template<class FieldValueMap>
	RedisResultBind& MSet(const FieldValueMap& kvs)
	{
		std::map<std::string, std::string> kvs_;
		for (auto it = kvs.begin(); it != kvs.end(); ++it)
			kvs_[Redis::to_string(it->first)] = Redis::to_string(it->second);
		return MSet(kvs_);
	}

	// MGET命令
	RedisResultBind& MGet(const std::vector<std::string>& keys); // string数组

	// INCR命令
	RedisResultBind& Incr(const std::string& key); // int 最新值
	
	// INCRBY命令
	RedisResultBind& Incrby(const std::string& key, int value); // int 最新值

	// HSET命令
	RedisResultBind& HSet(const std::string& key, const std::string& field, const std::string& value); // string OK
	template<class Field, class Value>
	RedisResultBind& HSet(const std::string& key, const Field& field, const Value& value)
	{
		return HSet(key, Redis::to_string(field), Redis::to_string(value));
	}

	// HGET命令
	RedisResultBind& HGet(const std::string& key, const std::string& field); // string
	template<class Field>
	RedisResultBind& HGet(const std::string& key, const Field& field)
	{
		return HGet(key, Redis::to_string(field));
	}
	// MHGET自定义复合命令 获取多个key中相同field字段的值
	RedisResultBind& MHGet(const std::vector<std::string>& keys, const std::string& field); // string数组

	// HMSET命令
	RedisResultBind& HMSet(const std::string& key, const std::map<std::string, std::string>& kvs); // string OK
	template<class FieldValueMap>
	RedisResultBind& HMSet(const std::string& key, const FieldValueMap& kvs)
	{
		std::map<std::string, std::string> kvs_;
		for (auto it = kvs.begin(); it != kvs.end(); ++it)
			kvs_[Redis::to_string(it->first)] = Redis::to_string(it->second);
		return HMSet(key, kvs_);
	}

	// HMGET命令
	RedisResultBind& HMGet(const std::string& key, const std::vector<std::string>& fields); // string数组
	template<class FieldList>
	int HMGet(const std::string& key, const FieldList& fields)
	{
		std::vector<std::string> fields_;
		fields_.reserve(fields.size());
		for (auto it = fields.begin(); it != fields.end(); ++it)
			fields_.emplace_back(Redis::to_string(*it));
		return HMGet(key, fields_);
	}

	// MHMGET自定义复合命令 获取多个key中多个相同field字段的值
	RedisResultBind& MHMGet(const std::vector<std::string>& keys, const std::vector<std::string>& fields); // string两维数组

	// HKEYS命令
	RedisResultBind& HKeys(const std::string& key); // string数组

	// HVALS命令
	RedisResultBind& HVals(const std::string& key); // string数组

	// HGETALL命令
	RedisResultBind& HGetAll(const std::string& key); // string数组 key 和 value
	// MHMGETALL自定义复合命令 获取多个key中的值
	RedisResultBind& MHGetAll(const std::vector<std::string>& keys); // string两维数组

	// HINCRBY命令
	RedisResultBind& HIncrby(const std::string& key, const std::string& field, int value); // int 最新值

	// HLEN命令
	RedisResultBind& HLen(const std::string& key); // int 最新值

	// HSCAN
	RedisResultBind& HScan(const std::string& key, int cursor, const std::string& match, int count); // Array 0位：表示下个游标 1位：string数组 key 和 value

	// HEXISTS命令
	RedisResultBind& HExists(const std::string& key, const std::string& field); // int 0不存在 1存在
	template<class Field>
	RedisResultBind& HExists(const std::string& key, const Field& field)
	{
		return HExists(key, Redis::to_string(field));
	}

	// HEDL命令
	RedisResultBind& HDel(const std::string& key, const std::string& field); // int 删除个数
	RedisResultBind& HDel(const std::string& key, const std::vector<std::string>& fields);
	template<class Field>
	RedisResultBind& HDel(const std::string& key, const Field& field)
	{
		return HDel(key, Redis::to_string(field));
	}
	template<class FieldList>
	RedisResultBind& HDels(const std::string& key, const FieldList& fields)
	{
		std::vector<std::string> fields_;
		fields_.reserve(fields.size());
		for (auto it = fields.begin(); it != fields.end(); ++it)
			fields_.emplace_back(Redis::to_string(*it));
		return HDels(key, fields_);
	}

	// LPUSH命令
	RedisResultBind& LPush(const std::string& key, const std::string& value); // int 最新长度
	RedisResultBind& LPushs(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	RedisResultBind& LPush(const std::string& key, const Value& value)
	{
		return LPush(key, Redis::to_string(value));
	}
	template<class ValueList>
	RedisResultBind& LPushs(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return LPushs(key, values_);
	}

	// RPUSH命令
	RedisResultBind& RPush(const std::string& key, const std::string& value); // int 最新长度
	RedisResultBind& RPushs(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	RedisResultBind& RPush(const std::string& key, const Value& value)
	{
		return RPush(key, Redis::to_string(value));
	}
	template<class ValueList>
	RedisResultBind& RPushs(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return RPushs(key, values_);
	}

	// LPOP命令
	RedisResultBind& LPop(const std::string& key); // string 被移除的元素

	// RPOP命令
	RedisResultBind& RPop(const std::string& key); // string 被移除的元素

	// LRANGE命令
	// 1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	RedisResultBind& LRange(const std::string& key, int start, int stop); // string数组

	// LREM命令
	RedisResultBind& LRem(const std::string& key, int count, std::string& value); // int 移除的数量

	// LTRIM命令
	RedisResultBind& LTrim(const std::string& key, int start, int stop);// string OK 或者 nil

	// LLEN命令
	RedisResultBind& LLen(const std::string& key); // int 长度

	// SADD命令
	RedisResultBind& SAdd(const std::string& key, const std::string& value); // int 添加的数量
	RedisResultBind& SAdds(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	RedisResultBind& SAdd(const std::string& key, const Value& value)
	{
		return SAdd(key, Redis::to_string(value));
	}
	template<class ValueList>
	RedisResultBind& SAdds(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return SAdds(key, values_);
	}

	// SREM命令
	RedisResultBind& SRem(const std::string& key, const std::string& value); // int 移除的数量
	RedisResultBind& SRems(const std::string& key, const std::vector<std::string>& values);
	template<class Value>
	RedisResultBind& SRem(const std::string& key, const Value& value)
	{
		return SRem(key, Redis::to_string(value));
	}
	template<class ValueList>
	RedisResultBind& SRems(const std::string& key, const ValueList& values)
	{
		std::vector<std::string> values_;
		values_.reserve(values.size());
		for (auto it = values.begin(); it != values.end(); ++it)
			values_.emplace_back(Redis::to_string(*it));
		return SRems(key, values_);
	}

	// SMEMBERS命令
	RedisResultBind& SMembers(const std::string& key); // string数组

	// 	SISMEMBER命令
	RedisResultBind& SISMember(const std::string& key, const std::string& value); // int 1存在 0不存在
	template<class Value>
	RedisResultBind& SISMember(const std::string& key, const Value& value)
	{
		return SISMember(key, Redis::to_string(value));
	}

	// SCARD命令
	RedisResultBind& SCard(const std::string& key); // int 长度

	// EVAL命令
	RedisResultBind& Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args); // 根据返回值来绑定

	// 执行批处理
	bool Do();
	bool Do(RedisResult::Array& rst);

protected:
	RedisSync& m_redis;
	std::stringstream m_cmdbuff;
	std::vector<RedisResultBind> m_binds;

	// 自定义复合命令使用
	struct _RedisResultBind_
	{
		int m_begin = 0;	// 对应m_binds下标
		int m_end = 0;	// 对应m_binds下标
		RedisResultBind m_bind;
		RedisResult m_result;

		void AddResult(const RedisResult& rst)
		{
			if (m_result.v.empty())
			{
				m_result.v = RedisResult::Array();
			}
			RedisResult::Array* pArray = boost::any_cast<RedisResult::Array>(&m_result.v);
			pArray->push_back(rst);
		}

		bool IsEnd(int index) { return index == m_end; }
	};
	std::map<int, std::shared_ptr<_RedisResultBind_>> m_binds2; // index对应m_binds下标
	RedisResult m_result2;

private:
	// 禁止拷贝
	RedisSyncPipeline(const RedisSyncPipeline&) = delete;
	RedisSyncPipeline& operator=(const RedisSyncPipeline&) = delete;
};

// 销毁时执行Do接口，方便使用
class RedisSyncPipeline2 : public RedisSyncPipeline
{
public:
	RedisSyncPipeline2(RedisSync& redis) : RedisSyncPipeline(redis) {};
	virtual ~RedisSyncPipeline2() { Do(); }

private:
	// 禁止拷贝
	RedisSyncPipeline2(const RedisSyncPipeline2&) = delete;
	RedisSyncPipeline2& operator=(const RedisSyncPipeline2&) = delete;
};

#endif