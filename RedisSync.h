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
	bool InitRedis(const std::string& ip, unsigned short port, const std::string& auth = "");

	// 返回false表示解析失败或者网络读取失败
	bool Command(const std::string& cmd, RedisResult& rst);
	bool Command(RedisResult& rst, const char* cmd, ...);

	// 管道开启后 其他的命令函数都返回-1或者false
	// Begin和Commit必须对称调用 返回值表示命令是否执行成功
	bool PipelineBegin();
	bool PipelineCommit();
	bool PipelineCommit(RedisResult::Array& rst);

	// 辅助接口 ==============================================

	// DEL命令
	// 返回值表示删除的个数 -1表示网络其他错误
	// cnt 表示...参数的个数 参数类型必须是std::string
	int Del(const std::string& key);
	int Del(int cnt, ...);

	// EXISTS和命令
	// 返回1存在 0不存在 -1表示网络其他错误
	int Exists(const std::string& key);

	// 过期相关命令
	// 返回1成功 0失败 -1表示网络其他错误
	int Expire(const std::string& key, long long value);
	int ExpireAt(const std::string& key, long long value);
	int PExpire(const std::string& key, long long value);
	int PExpireAt(const std::string& key, long long value);
	int TTL(const std::string& key, long long& value);
	int PTTL(const std::string& key, long long& value);

	// SET命令 
	// 返回1成功 0不成功 -1表示网络其他错误
	int Set(const std::string& key, const std::string& value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, int value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, float value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, double value, unsigned int ex = -1, bool nx = false);

	// GET命令
	// 返回1成功 0不成功 -1表示网络其他错误
	int Get(const std::string& key, std::string& value);
	int Get(const std::string& key, int& value);
	int Get(const std::string& key, float& value);
	int Get(const std::string& key, double& value);

	// MSET命令
	// 返回1成功 0不成功 -1表示网络其他错误
	// cnt 表示...参数的个数/2 参数类型必须是std::string
	int MSet(const std::map<std::string,std::string>& kvs);
	int MSet(const std::map<std::string, int>& kvs);
	int MSet(const std::map<std::string, float>& kvs);
	int MSet(const std::map<std::string, double>& kvs);
	int MSet(int cnt, ...);

	// MGET命令
	// 返回1成功 0不成功 -1表示网络其他错误
	// 若成功 rst为数组 元素为string或者null
	int MGet(const std::vector<std::string>& keys, RedisResult& rst);

	// HSET命令
	// 返回1成功 0不成功 -1表示网络其他错误
	int HSet(const std::string& key, const std::string& field, const std::string& value);
	int HSet(const std::string& key, const std::string& field, int value);
	int HSet(const std::string& key, const std::string& field, float value);
	int HSet(const std::string& key, const std::string& field, double value);

	// HGET命令
	// 返回1成功 0不成功 -1表示网络其他错误
	int HGet(const std::string& key, const std::string& field, std::string& value);
	int HGet(const std::string& key, const std::string& field, int& value);
	int HGet(const std::string& key, const std::string& field, float& value);
	int HGet(const std::string& key, const std::string& field, double& value);

	// HLEN命令
	// 返回列表长度 0不成功 -1表示网络其他错误
	int HLen(const std::string& key);

	// HEXISTS命令
	// 返回1存在 0不存在 -1表示网络其他错误
	int HExists(const std::string& key, const std::string& field);

	// HEDL命令
	// 返回值表示删除的个数 -1表示网络其他错误
	// cnt 表示...参数的个数 参数类型必须是std::string
	int HDel(const std::string& key, const std::string& field);
	int HDel(const std::string& key, int cnt, ...);

	// LPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络其他错误
	// cnt 表示...参数的个数 参数类型必须是std::string
	int LPush(const std::string& key, const std::string& value);
	int LPush(const std::string& key, int value);
	int LPush(const std::string& key, float value);
	int LPush(const std::string& key, double value);
	int LPush(const std::string& key, int cnt, ...);

	// RPUSH命令
	// 成功返回列表长度 0不成功 -1表示网络其他错误
	// cnt 表示...参数的个数 参数类型必须是std::string
	int RPush(const std::string& key, const std::string& value);
	int RPush(const std::string& key, int value);
	int RPush(const std::string& key, float value);
	int RPush(const std::string& key, double value);
	int RPush(const std::string& key, int cnt, ...);

	// LPOP命令
	// 1成功 0不成功 -1表示网络其他错误
	// value表示移除的元素
	int LPop(const std::string& key);
	int LPop(const std::string& key, std::string& value);
	int LPop(const std::string& key, int& value);
	int LPop(const std::string& key, float& value);
	int LPop(const std::string& key, double& value);

	// RPOP命令
	// 1成功 0不成功 -1表示网络其他错误
	// value表示移除的元素
	int RPop(const std::string& key);
	int RPop(const std::string& key, std::string& value);
	int RPop(const std::string& key, int& value);
	int RPop(const std::string& key, float& value);
	int RPop(const std::string& key, double& value);

	// LRANGE命令
	// 1成功 0不成功 -1表示网络其他错误
	// 若成功 rst为数组 元素为string
	int LRange(const std::string& key, int start, int stop, RedisResult& rst);

	// LREM命令
	// 成功返回移除元素的个数 0不成功 -1表示网络其他错误
	// 若成功 rst为数组 元素为string
	int LRem(const std::string& key, int count, std::string& value);

	// LLEN命令
	// 返回列表长度 0不成功 -1表示网络其他错误
	int LLen(const std::string& key);

	// SADD命令
	// 成功返回添加的数量 0不成功 -1表示网络其他错误
	// cnt 表示...参数的个数 参数类型必须是std::string
	int SAdd(const std::string& key, const std::string& value);
	int SAdd(const std::string& key, int value);
	int SAdd(const std::string& key, float value);
	int SAdd(const std::string& key, double value);
	int SAdd(const std::string& key, int cnt, ...);

	// SREM命令
	// 成功返回移除元素的数量 0不成功 -1表示网络其他错误
	// cnt 表示...参数的个数 参数类型必须是std::string
	int SRem(const std::string& key, const std::string& value);
	int SRem(const std::string& key, int value);
	int SRem(const std::string& key, float value);
	int SRem(const std::string& key, double value);
	int SRem(const std::string& key, int cnt, ...);

protected:
	bool _Connect();
	void _Close();

	// 返回false表示解析失败或者网络读取失败
	bool _DoCommand(const std::vector<std::string>& buff, RedisResult& rst);
	bool _DoCommand(RedisResult::Array& rst);

	// 读取并分析 buff表示读取的内容 pos表示解析到的位置
	// 返回false表示解析失败或者网络读取失败
	bool _ReadReply(RedisResult& rst, std::vector<char>& buff, int& pos);

	// 返回值表示长度 -1表示网络读取失败
	int _ReadByCRLF(std::vector<char>& buff, int pos);
	int _ReadByLen(int len, std::vector<char>& buff, int pos);

	boost::asio::io_service m_ioservice;
	boost::asio::ip::tcp::socket m_socket;
	bool m_bconnected;	// 是否已连接

	std::string m_ip;
	unsigned short m_port;
	std::string m_auth;

	bool m_pipeline; // 是否开启管道
	std::stringstream m_cmdbuff;
	int m_cmdcount;

	// 缓存命令清理辅助
	class BuffClearHelper
	{
	public:
		BuffClearHelper(RedisSync& redis) : m_redis(redis)
		{
		}
		~BuffClearHelper()
		{
			if (!m_redis.m_pipeline) // 没有开启管道，清除buff
			{
				m_redis.m_cmdbuff.str("");
				m_redis.m_cmdcount = 0;
			}
		}
	protected:
		RedisSync& m_redis;
	};
};

#endif