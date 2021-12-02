#include "RedisSyncPipeline.h"

#include "LLog.h"
#define LogError LLOG_ERROR

RedisResultBind& RedisSyncPipeline::Command(const std::string& cmd)
{
	if (cmd.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	std::string temp = cmd;
	int protect = 0;	// 1 单引号保护  2 双引号保护
	int size = (int)temp.size();
	for (int i = 0; i < size; ++i)
	{
		if (protect == 1)
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

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Del(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Del(const std::vector<std::string>& key)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");
	for (auto it = key.begin(); it != key.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Exists(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("EXISTS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Expire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PExpire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::TTL(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("TTL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PTTL(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("PTTL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Set(const std::string& key, const std::string& value, unsigned int ex, unsigned int px, bool nx)
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
	else if (px != -1)
	{
		buff.push_back("PX");
		buff.push_back(std::to_string(px));
	}
	if (nx)
	{
		buff.push_back("NX");
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}


RedisResultBind& RedisSyncPipeline::Get(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("GET");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MSet(const std::map<std::string, std::string>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(it->second);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MGet(const std::vector<std::string>& keys)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	std::vector<std::string> buff;
	buff.push_back("MGET");
	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}


RedisResultBind& RedisSyncPipeline::Incr(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("INCR");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Incrby(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("INCRBY");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HSet(const std::string& key, const std::string& field, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("HSET");
	buff.push_back(key);
	buff.push_back(field);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HGet(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HGET");
	buff.push_back(key);
	buff.push_back(field);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MHGet(const std::vector<std::string>& keys, const std::string& field)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	
	std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
	bindptr->m_begin = m_binds.size() - 1;
	bindptr->m_end = bindptr->m_begin + keys.size();

	for (auto keyit = keys.begin(); keyit != keys.end(); ++keyit)
	{
		std::vector<std::string> buff;
		buff.push_back("HGET");
		buff.push_back(*keyit);
		buff.push_back(field);
		Redis::FormatCommand(buff, m_cmdbuff);

		m_binds.push_back(RedisResultBind());
		m_binds.back().callback = [&, bindptr](const RedisResult& rst)
		{
			bindptr->AddResult(rst);
		};
		m_binds2[m_binds.size() - 1] = bindptr;
	}

	return bindptr->m_bind;
}

RedisResultBind& RedisSyncPipeline::HMSet(const std::string& key, const std::map<std::string, std::string>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("HMSET");
	buff.push_back(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(it->second);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HMGet(const std::string& key, const std::vector<std::string>& fields)
{
	std::vector<std::string> buff;
	buff.push_back("HMGET");
	buff.push_back(key);
	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MHMGet(const std::vector<std::string>& keys, const std::vector<std::string>& fields)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}

	std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
	bindptr->m_begin = m_binds.size() - 1;
	bindptr->m_end = bindptr->m_begin + keys.size();

	for (auto keyit = keys.begin(); keyit != keys.end(); ++keyit)
	{
		std::vector<std::string> buff;
		buff.push_back("HMGET");
		buff.push_back(*keyit);
		for (auto it = fields.begin(); it != fields.end(); ++it)
		{
			buff.push_back(*it);
		}
		Redis::FormatCommand(buff, m_cmdbuff);

		m_binds.push_back(RedisResultBind());
		m_binds.back().callback = [&, bindptr](const RedisResult& rst)
		{
			bindptr->AddResult(rst);
		};
		m_binds2[m_binds.size() - 1] = bindptr;
	}
	
	return bindptr->m_bind;
}

RedisResultBind& RedisSyncPipeline::HKeys(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HKEYS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HVals(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HVALS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HGetAll(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HGETALL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MHGetAll(const std::vector<std::string>& keys)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}

	std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
	bindptr->m_begin = m_binds.size() - 1;
	bindptr->m_end = bindptr->m_begin + keys.size();

	for (auto keyit = keys.begin(); keyit != keys.end(); ++keyit)
	{
		std::vector<std::string> buff;
		buff.push_back("HGETALL");
		buff.push_back(*keyit);
		
		Redis::FormatCommand(buff, m_cmdbuff);

		m_binds.push_back(RedisResultBind());
		m_binds.back().callback = [&, bindptr](const RedisResult& rst)
		{
			bindptr->AddResult(rst);
		};
		m_binds2[m_binds.size() - 1] = bindptr;
	}

	return bindptr->m_bind;
}

RedisResultBind& RedisSyncPipeline::HIncrby(const std::string& key, const std::string& field, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("HINCRBY");
	buff.push_back(key);
	buff.push_back(field);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HLEN");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HScan(const std::string& key, int cursor, const std::string& match, int count)
{
	std::vector<std::string> buff;
	buff.push_back("HSCAN");
	buff.push_back(key);
	buff.push_back(std::to_string(cursor));
	if (!match.empty())
	{
		buff.push_back("MATCH");
		buff.push_back(match);
	}
	if (count > 0)
	{
		buff.push_back("COUNT");
		buff.push_back(std::to_string(count));
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HExists(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HEXISTS");
	buff.push_back(key);
	buff.push_back(field);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HDel(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);
	buff.push_back(field);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HDels(const std::string& key, const std::vector<std::string>& fields)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);
	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPushs(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPushs(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPop(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("LPOP");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPop(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("RPOP");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LRange(const std::string& key, int start, int stop)
{
	std::vector<std::string> buff;
	buff.push_back("LRANGE");
	buff.push_back(key);
	buff.push_back(std::to_string(start));
	buff.push_back(std::to_string(stop));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LRem(const std::string& key, int count, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LREM");
	buff.push_back(key);
	buff.push_back(std::to_string(count));
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LTrim(const std::string& key, int start, int stop)
{
	std::vector<std::string> buff;
	buff.push_back("LTRIM");
	buff.push_back(key);
	buff.push_back(std::to_string(start));
	buff.push_back(std::to_string(stop));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("LLEN");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SAdd(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SAdds(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SRem(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SRems(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Sinter(const std::vector<std::string>& keys)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	std::vector<std::string> buff;
	buff.push_back("SINTER");
	for (const auto& it : keys)
	{
		buff.push_back(it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SMembers(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("SMEMBERS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SISMember(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SISMEMBER");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SCard(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("SCARD");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	std::vector<std::string> buff;
	buff.push_back("EVAL");
	buff.push_back(script);
	buff.push_back(std::to_string(keys.size()));

	for (const auto& it : keys)
	{
		buff.push_back(it);
	}
	for (const auto& it : args)
	{
		buff.push_back(it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Evalsha(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	std::vector<std::string> buff;
	buff.push_back("EVALSHA");
	buff.push_back(script);
	buff.push_back(std::to_string(keys.size()));

	for (const auto& it : keys)
	{
		buff.push_back(it);
	}
	for (const auto& it : args)
	{
		buff.push_back(it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ScriptLoad(const std::string& script)
{
	std::vector<std::string> buff;
	buff.push_back("SCRIPT");
	buff.push_back("LOAD");
	buff.push_back(script);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

bool RedisSyncPipeline::Do()
{
	int cmdcount = (int)m_binds.size();
	if (cmdcount == 0)
	{
		return true;
	}

	m_redis.m_ops += cmdcount;

	if (!m_redis.SendCommandAndCheckConnect(m_cmdbuff.str()))
	{
		m_cmdbuff.str("");
		m_binds.clear();
		m_binds2.clear();
		return false;
	}

	for (int i = 0; i < cmdcount; ++i)
	{
		RedisResult rst;
		if (m_redis.ReadReply(rst) != 1)
		{
			m_redis.Close();
			return false;
		}
		if (rst.IsError())
		{
			LogError("Redis %s", rst.ToString().c_str());
		}
		else
		{
			if (m_binds[i].callback)
			{
				m_binds[i].callback(rst);
			}

			// 自定义复合命令
			auto it = m_binds2.find(i);
			if (it != m_binds2.end())
			{
				if (it->second->IsEnd(i))
				{
					if (it->second->m_bind.callback)
					{
						it->second->m_bind.callback(it->second->m_result);
					}
				}
			}
		}
	}
	m_cmdbuff.str("");
	m_binds.clear();
	m_binds2.clear();
	return true;
}

bool RedisSyncPipeline::Do(RedisResult::Array& rst)
{
	int cmdcount = (int)m_binds.size();
	if (cmdcount == 0)
	{
		return true;
	}

	m_redis.m_ops += cmdcount;

	if (!m_redis.SendCommandAndCheckConnect(m_cmdbuff.str()))
	{
		m_cmdbuff.str("");
		m_binds.clear();
		m_binds2.clear();
		return false;
	}

	for (int i = 0; i < cmdcount; ++i)
	{
		rst.push_back(RedisResult());
		RedisResult& rst2 = rst.back();
		if (m_redis.ReadReply(rst2) != 1)
		{
			m_redis.Close();
			return false;
		}
		if (rst2.IsError())
		{
			LogError("Redis %s", rst2.ToString().c_str());
		}
		else
		{
			if (m_binds[i].callback)
			{
				m_binds[i].callback(rst2);
			}

			// 自定义复合命令
			auto it = m_binds2.find(i);
			if (it != m_binds2.end())
			{
				rst.pop_back();
				if (it->second->IsEnd(i))
				{
					rst.push_back(it->second->m_result);
					if (it->second->m_bind.callback)
					{
						it->second->m_bind.callback(it->second->m_result);
					}
				}
			}
		}
	}
	m_cmdbuff.str("");
	m_binds.clear();
	m_binds2.clear();
	return true;
}