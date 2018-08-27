#include <stdio.h>
#include <stdarg.h>
#include <boost/array.hpp>
#include "RedisSync.h"

#define LogError

RedisResult::RedisResult() : error(false)
{
}

bool RedisResult::IsError() const
{
	return error;
}

bool RedisResult::IsNull() const
{
	return v.empty();
}

bool RedisResult::IsInt() const
{
	return v.type() == typeid(long long);
}

bool RedisResult::IsString() const
{
	return v.type() == typeid(std::string);
}

bool RedisResult::IsArray() const
{
	return v.type() == typeid(Array);
}

int RedisResult::ToInt() const
{
	if (IsInt())
	{
		return (int)boost::any_cast<long long>(v);
	}
	return 0;
}

long long RedisResult::ToLongLong() const
{
	if (IsInt())
	{
		return boost::any_cast<long long>(v);
	}
	return 0;
}

const std::string& RedisResult::ToString() const
{
	if (IsString())
	{
		return *boost::any_cast<std::string>(&v);
	}
	static std::string empty;
	return empty;
}

const RedisResult::Array& RedisResult::ToArray() const
{
	if (IsArray())
	{
		return *boost::any_cast<Array >(&v);
	}
	static Array empty;
	return empty;
}

int RedisResult::StringToInt() const
{
	if (IsString())
	{
		return atoi(boost::any_cast<std::string>(v).c_str());
	}
	return 0;
}

float RedisResult::StringToFloat() const
{
	if (IsString())
	{
		return (float)atof(boost::any_cast<std::string>(v).c_str());
	}
	return 0.0f;
}

double RedisResult::StringToDouble() const
{
	if (IsString())
	{
		return atof(boost::any_cast<std::string>(v).c_str());
	}
	return 0.0f;
}

RedisSync::RedisSync() : m_socket(m_ioservice)
{
	m_bconnected = false;
	m_ip = "";
	m_port = 0;
	m_pipeline = false;
	m_cmdcount = 0;
}

RedisSync::~RedisSync()
{

}

bool RedisSync::InitRedis(const std::string& ip, unsigned short port, const std::string& auth)
{
	if(ip.empty())
	{
		LogError("Redis ip is empty");
		return false;
	}

	m_ip = ip;
	m_port = port;
	m_auth = auth;

	m_pipeline = false;
	m_cmdbuff.clear();

	if (!_Connect())
	{
		m_ip = "";
		m_port = 0;
		m_auth = "";
		return false;
	}
	return true;
}

bool RedisSync::Command(const std::string& cmd, RedisResult& rst)
{
	if (cmd.empty())
	{
		return false;
	}
	std::string temp = cmd;
	int protect = 0;	// 1 单引号保护  2 双引号保护
	int size = (int)temp.size();
	for (int i = 0; i < size; ++i)
	{
		if (protect==1)
		{
			if (temp[i] == '\'')
			{
				temp[i] = '\0';
				protect = 0;
			}
			continue;
		}
		if (protect == 2)
		{
			if (temp[i] == '\"')
			{
				temp[i] = '\0';
				protect = 0;
			}
			continue;
		}
		if (temp[i] == ' ' || temp[i] == '\t')
		{
			temp[i] = '\0';
		}
		if (temp[i] == '\'')
		{
			protect = 1; // 进入单引号
			temp[i] = '\0';
		}
		if (temp[i] == '\"')
		{
			protect = 2; // 进入双引号
			temp[i] = '\0';
		}
	}

	std::vector<std::string> buff;
	for (int i = 0; i < size;)
	{
		if (temp[i] == '\0')
		{
			++i;
			continue;
		}
		buff.push_back(&temp[i]);
		i += (int)buff.back().size();
	}
	
	if (!_DoCommand(buff, rst))
	{
		return false;
	}
	return true;
}

bool RedisSync::Command(RedisResult& rst, const char* cmd, ...)
{
	char buff[1024] = { 0 };
	va_list ap;
	va_start(ap, cmd);
	vsnprintf(buff, 1024 - 1, cmd, ap);
	va_end(ap);

	return Command(buff, rst);
}

bool RedisSync::PipelineBegin()
{
	if (!m_bconnected)
	{
		// 如果ip不为空 重新连接下
		if (!m_ip.empty())
		{
			if (!_Connect())
			{
				return false;
			}
		}
		else
		{
			LogError("Redis Not Connect");
			return false;
		}
	}

	m_pipeline = true;
	return true;
}

bool RedisSync::PipelineCommit()
{
	if (!m_pipeline)
	{
		LogError("Not Open Pipeline");
		return false;
	}
	m_pipeline = false;
	RedisResult::Array rst;
	if (!_DoCommand(rst))
	{
		return false;
	}
	return true;
}

bool RedisSync::PipelineCommit(RedisResult::Array& rst)
{
	if (!m_pipeline)
	{
		LogError("Not Open Pipeline");
		return false;
	}
	m_pipeline = false;
	if (!_DoCommand(rst))
	{
		return false;
	}
	return true;
}


int RedisSync::Del(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff,rst))
	{
		return -1;
	}
	if ( rst.IsError() )
	{
		return 0;
	}
	if ( rst.IsInt() )
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Del(int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, std::string) );
	}
	va_end(ap);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::Exists(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("EXISTS");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::Expire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::ExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::PExpire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::PExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::TTL(const std::string& key, long long& value)
{
	std::vector<std::string> buff;
	buff.push_back("TTL");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		value = rst.ToLongLong();
		return true;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::PTTL(const std::string& key, long long& value)
{
	std::vector<std::string> buff;
	buff.push_back("PTTL");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		value = rst.ToLongLong();
		return true;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Set(const std::string& key, const std::string& value, unsigned int ex, bool nx)
{
	std::vector<std::string> buff;
	buff.push_back("SET");
	buff.push_back(key);
	buff.push_back(value);
	if (ex != -1)
	{
		buff.push_back("EX");
		buff.push_back(std::to_string(ex));
	}
	if (nx)
	{
		buff.push_back("NX");
	}

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::Set(const std::string& key, int value, unsigned int ex, bool nx)
{
	return Set(key, std::to_string(value), ex, nx);
}

int RedisSync::Set(const std::string& key, float value, unsigned int ex, bool nx)
{
	return Set(key, std::to_string(value), ex, nx);
}

int RedisSync::Set(const std::string& key, double value, unsigned int ex, bool nx)
{
	return Set(key, std::to_string(value), ex, nx);
}

int RedisSync::Get(const std::string& key, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("GET");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff,rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Get(const std::string& key, int& value)
{
	std::string rst;
	int r = Get(key, rst);
	if ( r != 1 )
	{
		return r;
	}
	value = atoi(rst.c_str());
	return 1;
}

int RedisSync::Get(const std::string& key, float& value)
{
	std::string rst;
	int r = Get(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::Get(const std::string& key, double& value)
{
	std::string rst;
	int r = Get(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
}

int RedisSync::MSet(const std::map<std::string, std::string>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(it->second);
	}

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::MSet(const std::map<std::string, int>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
	}

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::MSet(const std::map<std::string, float>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
	}

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::MSet(const std::map<std::string, double>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
	}

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::MSet(int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, std::string));
		buff.push_back(va_arg(ap, std::string));
	}
	va_end(ap);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::MGet(const std::vector<std::string>& keys, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("MGET");

	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		buff.push_back(*it);
	}

	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HSet(const std::string& key, const std::string& field, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("HSET");
	buff.push_back(key);
	buff.push_back(field);
	buff.push_back(value);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsInt())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HSet(const std::string& key, const std::string& field, int value)
{
	return HSet(key, field, std::to_string(value));
}

int RedisSync::HSet(const std::string& key, const std::string& field, float value)
{
	return HSet(key, field, std::to_string(value));
}

int RedisSync::HSet(const std::string& key, const std::string& field, double value)
{
	return HSet(key, field, std::to_string(value));
}

int RedisSync::HGet(const std::string& key, const std::string& field, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("HGET");
	buff.push_back(key);
	buff.push_back(field);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HGet(const std::string& key, const std::string& field, int& value)
{
	std::string rst;
	int r = HGet(key, field, rst);
	if (r != 1)
	{
		return r;
	}
	value = atoi(rst.c_str());
	return 1;
}

int RedisSync::HGet(const std::string& key, const std::string& field, float& value)
{
	std::string rst;
	int r = HGet(key, field, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::HGet(const std::string& key, const std::string& field, double& value)
{
	std::string rst;
	int r = HGet(key, field, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
}

int RedisSync::HLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HLEN");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::HExists(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HEXISTS");
	buff.push_back(key);
	buff.push_back(field);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::HDel(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);
	buff.push_back(field);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::HDel(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, std::string));
	}
	va_end(ap);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::LPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::LPush(const std::string& key, int value)
{
	return LPush(key, std::to_string(value));
}

int RedisSync::LPush(const std::string& key, float value)
{
	return LPush(key, std::to_string(value));
}

int RedisSync::LPush(const std::string& key, double value)
{
	return LPush(key, std::to_string(value));
}

int RedisSync::LPush(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, std::string));
	}
	va_end(ap);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::RPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::RPush(const std::string& key, int value)
{
	return RPush(key, std::to_string(value));
}

int RedisSync::RPush(const std::string& key, float value)
{
	return RPush(key, std::to_string(value));
}

int RedisSync::RPush(const std::string& key, double value)
{
	return RPush(key, std::to_string(value));
}

int RedisSync::RPush(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, std::string));
	}
	va_end(ap);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::LPop(const std::string& key)
{
	std::string rst;
	return LPop(key, rst);
}

int RedisSync::LPop(const std::string& key, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LPOP");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsNull())
	{
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LPop(const std::string& key, int& value)
{
	std::string rst;
	int r = LPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atoi(rst.c_str());
	return 1;
}

int RedisSync::LPop(const std::string& key, float& value)
{
	std::string rst;
	int r = LPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::LPop(const std::string& key, double& value)
{
	std::string rst;
	int r = LPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
}

int RedisSync::RPop(const std::string& key)
{
	std::string rst;
	return RPop(key, rst);
}

int RedisSync::RPop(const std::string& key, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("RPOP");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsNull())
	{
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::RPop(const std::string& key, int& value)
{
	std::string rst;
	int r = RPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atoi(rst.c_str());
	return 1;
}

int RedisSync::RPop(const std::string& key, float& value)
{
	std::string rst;
	int r = RPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::RPop(const std::string& key, double& value)
{
	std::string rst;
	int r = RPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
}

int RedisSync::LRange(const std::string& key, int start, int stop, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("LRANGE");
	buff.push_back(key);
	buff.push_back(std::to_string(start));
	buff.push_back(std::to_string(stop));

	if (!_DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsNull())
	{
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LRem(const std::string& key, int count, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LREM");
	buff.push_back(key);
	buff.push_back(std::to_string(count));
	buff.push_back(value);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::LLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("LLEN");
	buff.push_back(key);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::SAdd(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::SAdd(const std::string& key, int value)
{
	return SAdd(key, std::to_string(value));
}

int RedisSync::SAdd(const std::string& key, float value)
{
	return SAdd(key, std::to_string(value));
}

int RedisSync::SAdd(const std::string& key, double value)
{
	return SAdd(key, std::to_string(value));
}

int RedisSync::SAdd(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, std::string));
	}
	va_end(ap);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::SRem(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

int RedisSync::SRem(const std::string& key, int value)
{
	return SRem(key, std::to_string(value));
}

int RedisSync::SRem(const std::string& key, float value)
{
	return SRem(key, std::to_string(value));
}

int RedisSync::SRem(const std::string& key, double value)
{
	return SRem(key, std::to_string(value));
}

int RedisSync::SRem(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, std::string));
	}
	va_end(ap);

	RedisResult rst;
	if (!_DoCommand(buff, rst))
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

bool RedisSync::_Connect()
{
	_Close();

	boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(m_ip), m_port);
	boost::system::error_code ec;
	m_socket.open(endpoint.protocol(), ec);
	if (ec)
	{
		LogError("RedisSync Open Fail, ip=%s port=%d", m_ip.c_str(), (int)m_port);
		return false;
	}

	m_socket.set_option(boost::asio::ip::tcp::no_delay(true), ec);
	m_socket.set_option(boost::asio::socket_base::keep_alive(true), ec);

	m_socket.connect(endpoint, ec);
	if (ec)
	{
		LogError("RedisSync Connect Fail, ip=%s port=%d", m_ip.c_str(), (int)m_port);
		return false;
	}

	// 密码或者测试连接
	std::vector<std::string> buff;
	if (!m_auth.empty())
	{
		buff.push_back("AUTH");
		buff.push_back(m_auth);
	}
	else
	{
		buff.push_back("PING");
	}

	std::ostringstream cmdbuff;
	cmdbuff << "*" << buff.size() << "\r\n";
	for (auto it = buff.begin(); it != buff.end(); ++it)
	{
		cmdbuff << "$" << it->size() << "\r\n" << *it << "\r\n";
	}
	boost::asio::write(m_socket, boost::asio::buffer(cmdbuff.str()), boost::asio::transfer_all(), ec);
	if (ec)
	{
		LogError("RedisSync Write Error, %s", ec.message().c_str());
		return false;
	}
	std::vector<char> readbuff;
	int pos = 0;
	if (_ReadByCRLF(readbuff, pos) == -1)
	{
		return false;
	}
	if (readbuff.size() == 0 || readbuff[0] == '-')
	{
		LogError("RedisSync Auto Error, %s", m_auth.c_str());
		return false;
	}

	LogError("RedisSync Connect Success, ip=%s port=%d", m_ip.c_str(), (int)m_port);
	m_bconnected = true;
	return true;
}

void RedisSync::_Close()
{
	boost::system::error_code ec;
	if (m_socket.is_open())
	{
		m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
		m_socket.close(ec);
	}
	m_bconnected = false;
}

bool RedisSync::_DoCommand(const std::vector<std::string>& buff, RedisResult& rst)
{
	BuffClearHelper helper(*this);

	// 命令写入buff
	m_cmdbuff << "*" << buff.size() << "\r\n";
	for (auto it = buff.begin(); it != buff.end(); ++it)
	{
		m_cmdbuff << "$" << it->size() << "\r\n" << *it << "\r\n";
	}
	m_cmdcount++;

	if (m_pipeline)
	{
		// 开启了管道 返回false
		return false;
	}

	// 如果没有连接先尝试连接下
	if (!m_bconnected)
	{
		// 如果ip不为空 重新连接下
		if ( !m_ip.empty() )
		{
			if (!_Connect())
			{
				return false;
			}
		}
		else
		{
			LogError("Redis Not Connect");
			return false;
		}
	}

	boost::system::error_code ec;
	boost::asio::write(m_socket, boost::asio::buffer(m_cmdbuff.str()), boost::asio::transfer_all(), ec);
	if (ec)
	{
		// 尝试连接下
		if (!_Connect())
		{
			return false;
		}
		boost::asio::write(m_socket, boost::asio::buffer(m_cmdbuff.str()), boost::asio::transfer_all(), ec);
		if ( ec )
		{
			LogError("RedisSync Write Error, %s", ec.message().c_str());
			return false;
		}
	}

	std::vector<char> readbuff;
	readbuff.reserve(512);
	int pos = 0;
	if (!_ReadReply(rst, readbuff, pos))
	{
		_Close();
		return false;
	}
	return true;
}

bool RedisSync::_DoCommand(RedisResult::Array& rst)
{
	BuffClearHelper helper(*this);

	// 如果没有连接先尝试连接下
	if (!m_bconnected)
	{
		// 如果ip不为空 重新连接下
		if (!m_ip.empty())
		{
			if (!_Connect())
			{
				return false;
			}
		}
		else
		{
			LogError("Redis Not Connect");
			return false;
		}
	}

	boost::system::error_code ec;
	boost::asio::write(m_socket, boost::asio::buffer(m_cmdbuff.str()), boost::asio::transfer_all(), ec);
	if (ec)
	{
		// 尝试连接下
		if (!_Connect())
		{
			return false;
		}
		boost::asio::write(m_socket, boost::asio::buffer(m_cmdbuff.str()), boost::asio::transfer_all(), ec);
		if (ec)
		{
			LogError("RedisSync Write Error, %s", ec.message().c_str());
			return false;
		}
	}

	std::vector<char> readbuff;
	readbuff.reserve(512);
	int pos = 0;
	for (int i = 0; i < m_cmdcount; ++i)
	{
		rst.push_back(RedisResult());
		RedisResult& rst2 = rst.back();
		if (!_ReadReply(rst2, readbuff, pos))
		{
			_Close();
			return false;
		}
	}
	
	return true;
}

bool RedisSync::_ReadReply(RedisResult& rst, std::vector<char>& buff, int& pos)
{
	int len = _ReadByCRLF(buff,pos);
	if ( len <= 1 )
	{
		return false;
	}

	char type = buff[pos];
	len -= 1;
	pos += 1;
	switch (type)
	{
		case '+':
		{
			rst.v = std::string(&buff[pos], &buff[pos + len]);

			pos += len;
			pos += 2; // \r\n
			break;
		}
		case '-':
		{
			rst.v = std::string(&buff[pos], &buff[pos + len]);
			rst.error = true;
			LogError("Redis %s", rst.ToString().c_str());

			pos += len;
			pos += 2; // \r\n
			break;
		}
		case ':':
		{
			buff[pos + len] = '\0';
			char* p = &buff[pos];
			rst.v = atoll(p);
			buff[pos + len] = '\r';

			pos += len;
			pos += 2; // \r\n
			break;
		}
		case '$':
		{
			buff[pos + len] = '\0';
			char* p = &buff[pos];
			int strlen = atoi(p);
			buff[pos + len] = '\r';

			pos += len;
			pos += 2; // \r\n

			if (strlen==-1)
			{
				// nil
			}
			else
			{
				len = _ReadByLen(strlen, buff, pos);
				if (len == -1)
				{
					return false;
				}

				rst.v = std::string(&buff[pos], &buff[pos + len]);

				pos += len;
				pos += 2; // \r\n
			}
			break;
		}
		case '*':
		{
			buff[pos + len] = '\0';
			char* p = &buff[pos];
			int size = atoi(p);
			buff[pos + len] = '\r';

			pos += len;
			pos += 2; // \r\n

			rst.v = RedisResult::Array();
			RedisResult::Array* pArray = boost::any_cast<RedisResult::Array >(&rst.v);
			for (int i =0; i < size; ++i)
			{
				RedisResult rst2;
				if (!_ReadReply(rst2, buff, pos))
				{
					return false;
				}
				pArray->push_back(rst2);
			}
			break;
		}
		default:
		{
			return false;
		}
	}
	
	return true;
}

int RedisSync::_ReadByCRLF(std::vector<char>& buff, int pos)
{
	do 
	{
		for (int i = pos; i < (int)buff.size()-1; ++i)
		{
			if (memcmp(&buff[i], "\r\n", 2) == 0)
			{
				return i- pos;
			}
		}

		boost::array<char, 512> inbuff;
		boost::system::error_code ec;
		size_t size = m_socket.read_some(boost::asio::buffer(inbuff), ec);
		if (ec)
		{
			LogError("RedisSync Read Error, %s", ec.message().c_str());
			return -1;
		}
		if ( size > 0 )
		{
			buff.insert(buff.end(), inbuff.begin(), inbuff.begin() + size);
			continue;
		}
		break;
	} while (true);

	return -1;
}

int RedisSync::_ReadByLen(int len, std::vector<char>& buff, int pos)
{
	do
	{
		if ((int)buff.size()- pos >= len)
		{
			return len;
		}

		boost::array<char, 512> inbuff;
		boost::system::error_code ec;
		size_t size = m_socket.read_some(boost::asio::buffer(inbuff), ec);
		if (ec)
		{
			LogError("RedisSync Read Error, %s", ec.message().c_str());
			return -1;
		}
		if (size > 0)
		{
			buff.insert(buff.end(), inbuff.begin(), inbuff.begin() + size);
			continue;
		}
		break;
	} while (true);

	return -1;
}
