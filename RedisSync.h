#ifndef _REDISSYNC_H_
#define _REDISSYNC_H_

#include <map>
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
	bool InitRedis(const std::string& host, unsigned short port, const std::string& auth = "");
	void Close();

	// 返回false表示解析失败或者网络读取失败
	bool Command(const std::string& cmd, RedisResult& rst);
	bool Command(RedisResult& rst, const char* cmd, ...);

	// 管道开启后 Command函数都false 其他辅助函数都返回-1
	// Begin和Commit必须对称调用 返回值表示命令是否执行成功
	bool PipelineBegin();
	bool PipelineCommit();
	bool PipelineCommit(RedisResult::Array& rst);

	// 因为订阅消息和请求消息是不对称的，这里只能把订阅功能写的尽量简单
	// 订阅使用 开启订阅后，执行任何命令都直接返回false或者-1
	// 返回false表示解析失败或者网络读取失败 返回true表示开启订阅
	bool SubScribe(const std::string& channel);
	bool SubScribes(int cnt, ...);
	// 获取订阅的消息 返回-1表示未开启订阅或者网络错误，0表示没有消息, 1表示收到消息
	// 参数block表示是否阻塞直到收到订阅消息
	int Message(std::string& channel, std::string& msg, bool block);
	// 取消订阅
	bool UnSubScribe();

protected:
	bool Connect();
	bool CheckConnect();

	// 返回false表示解析失败或者网络读取失败
	bool DoCommand(const std::vector<std::string>& buff, RedisResult& rst);

	// 发送命令
	bool SendCommand(const std::string& cmdbuff);

	// 返回值表示长度 buff表示数据地址 -1表示网络读取失败 0表示没有读取到
	// buff中包括\r\n minlen表示buff中不包括\r\n的最少长度 
	virtual int ReadToCRLF(char** buff, int mindatalen) override;

	void ClearRecvBuff();
	void ResetRecvBuff();

	boost::asio::io_service m_ioservice;
	boost::asio::ip::tcp::socket m_socket;
	bool m_bconnected;	// 是否已连接

	// 数据接受buff
	std::vector<char> m_recvbuff;
	int m_recvpos;

	std::string m_host;
	unsigned short m_port;
	std::string m_auth;

	// 开启管道使用
	bool m_pipeline;
	std::stringstream m_pipecmdbuff;
	int m_pipecmdcount;

	// 订阅使用
	bool m_subscribe;

private:
	// 禁止拷贝
	RedisSync(const RedisSync&) = delete;
	RedisSync& operator=(const RedisSync&) = delete;

public:
	// 辅助类接口==================================================
	// 注 ：
	// 管道开启后 命令会缓存起来 下面的函数都返回-1
	// 开启订阅后下面的函数直接返回-1

	// DEL命令
	// 返回值表示删除的个数 -1表示网络或者其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int Del(const std::string& key);
	int Dels(int cnt, ...);

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
	int Set(const std::string& key, const std::string& value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, int value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, float value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, double value, unsigned int ex = -1, bool nx = false);

	// GET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int Get(const std::string& key, std::string& value);
	int Get(const std::string& key, int& value);
	int Get(const std::string& key, float& value);
	int Get(const std::string& key, double& value);

	// MSET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数/2 参数类型必须是C样式的字符串char*
	int MSet(const std::map<std::string, std::string>& kvs);
	int MSet(const std::map<std::string, int>& kvs);
	int MSet(const std::map<std::string, float>& kvs);
	int MSet(const std::map<std::string, double>& kvs);
	int MSet(int cnt, ...);

	// MGET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string或者null
	int MGet(const std::vector<std::string>& keys, RedisResult& rst);

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
	int HSet(const std::string& key, const std::string& field, int value);
	int HSet(const std::string& key, const std::string& field, float value);
	int HSet(const std::string& key, const std::string& field, double value);

	// HGET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	int HGet(const std::string& key, const std::string& field, std::string& value);
	int HGet(const std::string& key, const std::string& field, int& value);
	int HGet(const std::string& key, const std::string& field, float& value);
	int HGet(const std::string& key, const std::string& field, double& value);

	// HMSET命令
	// 返回1成功 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数/2 参数类型必须是C样式的字符串char*
	int HMSet(const std::string& key, const std::map<std::string, std::string>& kvs);
	int HMSet(const std::string& key, const std::map<std::string, int>& kvs);
	int HMSet(const std::string& key, const std::map<std::string, float>& kvs);
	int HMSet(const std::string& key, const std::map<std::string, double>& kvs);
	int HMSet(const std::string& key, int cnt, ...);

	// HMGET命令
	// 返回1成功 0不成功 - 1表示网络或其他错误
	// 若成功 rst为数组 元素为string或者null
	int HMGet(const std::string& key, const std::vector<std::string>& fields, RedisResult& rst);

	// HGETALL命令
	// 返回1成功 0不成功 - 1表示网络或其他错误
	// 若成功 rst为数组 元素为string或者null
	int HGetAll(const std::string& key, RedisResult& rst);

	// HINCRBY命令
	// 返回1成功 0不成功 -1表示网络或其他错误 svalue表示成功后的值
	int HIncrby(const std::string& key, const std::string& field, int value, long long& svalue);
	int HIncrby(const std::string& key, const std::string& field, int value, int& svalue);
	int HIncrby(const std::string& key, const std::string& field, int value);

	// HLEN命令
	// 返回列表长度 0不成功 -1表示网络或其他错误
	int HLen(const std::string& key);

	// HEXISTS命令
	// 返回1存在 0不存在 -1表示网络或其他错误
	int HExists(const std::string& key, const std::string& field);

	// HEDL命令
	// 返回值表示删除的个数 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int HDel(const std::string& key, const std::string& field);
	int HDels(const std::string& key, int cnt, ...);

	// LPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int LPush(const std::string& key, const std::string& value);
	int LPush(const std::string& key, int value);
	int LPush(const std::string& key, float value);
	int LPush(const std::string& key, double value);
	int LPushs(const std::string& key, int cnt, ...);

	// RPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int RPush(const std::string& key, const std::string& value);
	int RPush(const std::string& key, int value);
	int RPush(const std::string& key, float value);
	int RPush(const std::string& key, double value);
	int RPushs(const std::string& key, int cnt, ...);

	// LPOP命令
	// 1成功 0不成功 -1表示网络或其他错误
	// value表示移除的元素
	int LPop(const std::string& key);
	int LPop(const std::string& key, std::string& value);
	int LPop(const std::string& key, int& value);
	int LPop(const std::string& key, float& value);
	int LPop(const std::string& key, double& value);

	// RPOP命令
	// 1成功 0不成功 -1表示网络或其他错误
	// value表示移除的元素
	int RPop(const std::string& key);
	int RPop(const std::string& key, std::string& value);
	int RPop(const std::string& key, int& value);
	int RPop(const std::string& key, float& value);
	int RPop(const std::string& key, double& value);

	// LRANGE命令
	// 1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int LRange(const std::string& key, int start, int stop, RedisResult& rst);

	// LREM命令
	// 成功返回移除元素的个数 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int LRem(const std::string& key, int count, std::string& value);

	// LLEN命令
	// 返回列表长度 0元素为空或者不成功 -1表示网络或其他错误
	int LLen(const std::string& key);

	// SADD命令
	// 成功返回添加的数量 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int SAdd(const std::string& key, const std::string& value);
	int SAdd(const std::string& key, int value);
	int SAdd(const std::string& key, float value);
	int SAdd(const std::string& key, double value);
	int SAdds(const std::string& key, int cnt, ...);

	// SREM命令
	// 成功返回移除元素的数量 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int SRem(const std::string& key, const std::string& value);
	int SRem(const std::string& key, int value);
	int SRem(const std::string& key, float value);
	int SRem(const std::string& key, double value);
	int SRems(const std::string& key, int cnt, ...);

	// SMEMBERS命令
	// 1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	int SMembers(const std::string& key, RedisResult& rst);

	// SCARD命令
	// 返回集合长度 0元素为空或者不成功 -1表示网络或其他错误
	int Scard(const std::string& key);

	// EVAL命令
	// 1成功 0不成功 -1表示网络或其他错误
	int Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args);
	int Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst);
};

#endif