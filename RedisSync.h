#ifndef _REDISSYNC_H_
#define _REDISSYNC_H_

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/any.hpp>

// 同步调用，不支持多线程 yuwf
// 除了开启管道外，每个命令函数都支持原子性操作
// 订阅命令不能和其他非订阅命令一起使用

class RedisResult
{
public:
	typedef std::vector<RedisResult> Array;

	RedisResult();

	bool IsError() const;
	
	bool IsNull() const;
	bool IsInt() const;
	bool IsString() const;
	bool IsArray() const;

	int ToInt() const;
	long long ToLongLong() const;
	const std::string& ToString() const;
	const Array& ToArray() const;

	// String类型使用 方便使用
	int StringToInt() const;
	float StringToFloat() const;
	double StringToDouble() const;

	void Clear();

protected:
	friend class RedisSync;
	boost::any v;
	bool error;
};

class RedisSync
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
	bool SubScribe(int cnt, ...);
	// 获取订阅的消息 返回-1表示未开启订阅或者网络错误，0表示没有消息, 1表示收到消息
	// 参数block表示是否阻塞直到收到订阅消息
	int Message(std::string& channel, std::string& msg, bool block);
	// 取消订阅
	bool UnSubScribe();

protected:
	bool _Connect();
	void _Close();
	bool _CheckConnect();

	// 返回false表示解析失败或者网络读取失败
	bool _DoCommand(const std::vector<std::string>& buff, RedisResult& rst);
	// 格式化命令
	void _FormatCommand(const std::vector<std::string>& buff, std::stringstream &cmdbuff);
	// 发送命令
	bool _SendCommand(const std::string& cmdbuff);

	// 读取并分析 buff表示读取的内容 pos表示解析到的位置
	// 返回值 -1 表示网络错误或者解析错误 0 表示未读取到 1 表示读取到了
	int _ReadReply(RedisResult& rst, std::vector<char>& buff, int& pos);

	// 返回值表示长度 -1表示网络读取失败 0表示没有读取到 pos表示buff的起始位置
	int _ReadByCRLF(std::vector<char>& buff, int pos);
	int _ReadByLen(int len, std::vector<char>& buff, int pos);

	boost::asio::io_service m_ioservice;
	boost::asio::ip::tcp::socket m_socket;
	bool m_bconnected;	// 是否已连接

	std::string m_host;
	unsigned short m_port;
	std::string m_auth;

	// 开启管道使用
	bool m_pipeline;
	std::stringstream m_pipecmdbuff;
	int m_pipecmdcount;

	// 订阅使用
	bool m_subscribe;
	std::vector<char> m_submsgbuff;

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
	int Del(int cnt, ...);

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
	int HDel(const std::string& key, int cnt, ...);

	// LPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int LPush(const std::string& key, const std::string& value);
	int LPush(const std::string& key, int value);
	int LPush(const std::string& key, float value);
	int LPush(const std::string& key, double value);
	int LPush(const std::string& key, int cnt, ...);

	// RPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int RPush(const std::string& key, const std::string& value);
	int RPush(const std::string& key, int value);
	int RPush(const std::string& key, float value);
	int RPush(const std::string& key, double value);
	int RPush(const std::string& key, int cnt, ...);

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
	// 返回列表长度 0不成功 -1表示网络或其他错误
	int LLen(const std::string& key);

	// SADD命令
	// 成功返回添加的数量 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int SAdd(const std::string& key, const std::string& value);
	int SAdd(const std::string& key, int value);
	int SAdd(const std::string& key, float value);
	int SAdd(const std::string& key, double value);
	int SAdd(const std::string& key, int cnt, ...);

	// SREM命令
	// 成功返回移除元素的数量 0不成功 -1表示网络或其他错误
	// cnt 表示...参数的个数 参数类型必须是C样式的字符串char*
	int SRem(const std::string& key, const std::string& value);
	int SRem(const std::string& key, int value);
	int SRem(const std::string& key, float value);
	int SRem(const std::string& key, double value);
	int SRem(const std::string& key, int cnt, ...);
};

#endif