#include "RedisSyncPipeline.h"

RedisResultBind& RedisSyncPipeline::Command(const std::string& str)
{
	if (str.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.FromString(str);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Del(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("DEL");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Del(const std::vector<std::string>& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("DEL");
	for (auto it = key.begin(); it != key.end(); ++it)
	{
		cmd.Add(*it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Dump(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("DUMP");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Exists(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("EXISTS");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Expire(const std::string& key, long long value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("EXPIRE");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ExpireAt(const std::string& key, long long value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("EXPIREAT");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PExpire(const std::string& key, long long value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("PEXPIRE");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PExpireAt(const std::string& key, long long value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("PEXPIREAT");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Persist(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("PERSIST");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::TTL(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("TTL");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PTTL(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("PTTL");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Keys(const std::string& pattern)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("KEYS");
	cmd.Add(pattern);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Move(const std::string& key, int index)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("MOVE");
	cmd.Add(key);
	cmd.Add(index);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RandomKey()
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("RANDOMKEY");

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Rename(const std::string& key, const std::string& newkey)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("RENAME");
	cmd.Add(key);
	cmd.Add(newkey);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RenameNX(const std::string& key, const std::string& newkey)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("RENAMENX");
	cmd.Add(key);
	cmd.Add(newkey);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Scan(int cursor, const std::string& match, int count)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SCAN");
	cmd.Add(cursor);
	if (!match.empty())
	{
		cmd.Add("MATCH");
		cmd.Add(match);
	}
	if (count > 0)
	{
		cmd.Add("COUNT");
		cmd.Add(count);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Type(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("TYPE");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Get(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("GET");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Incr(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("INCR");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Incrby(const std::string& key, long long value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("INCRBY");
	cmd.Add(key);
	cmd.Add(value);

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
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("MGET");
	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		cmd.Add(*it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MSet(const std::map<std::string, std::string>& kvs)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		cmd.Add(it->first);
		cmd.Add(it->second);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Set(const std::string& key, const std::string& value, unsigned int ex, unsigned int px, bool nx)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SET");
	cmd.Add(key);
	cmd.Add(value);
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

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HDel(const std::string& key, const std::string& field)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HDEL");
	cmd.Add(key);
	cmd.Add(field);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HDels(const std::string& key, const std::vector<std::string>& fields)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HDEL");
	cmd.Add(key);
	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		cmd.Add(*it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HExists(const std::string& key, const std::string& field)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HEXISTS");
	cmd.Add(key);
	cmd.Add(field);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HGet(const std::string& key, const std::string& field)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HGET");
	cmd.Add(key);
	cmd.Add(field);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HGetAll(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HGETALL");
	cmd.Add(key);

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
		m_cmds.push_back(RedisCommand());
		RedisCommand& cmd = m_cmds.back();
		cmd.Add("HGETALL");
		cmd.Add(*keyit);

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
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HINCRBY");
	cmd.Add(key);
	cmd.Add(field);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HKeys(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HKEYS");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HLen(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HLEN");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HMGet(const std::string& key, const std::vector<std::string>& fields)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HMGET");
	cmd.Add(key);
	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		cmd.Add(*it);
	}

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
		m_cmds.push_back(RedisCommand());
		RedisCommand& cmd = m_cmds.back();
		cmd.Add("HMGET");
		cmd.Add(*keyit);
		for (auto it = fields.begin(); it != fields.end(); ++it)
		{
			cmd.Add(*it);
		}

		m_binds.push_back(RedisResultBind());
		m_binds.back().callback = [&, bindptr](const RedisResult& rst)
		{
			bindptr->AddResult(rst);
		};
		m_binds2[m_binds.size() - 1] = bindptr;
	}

	return bindptr->m_bind;
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
		m_cmds.push_back(RedisCommand());
		RedisCommand& cmd = m_cmds.back();
		cmd.Add("HGET");
		cmd.Add(*keyit);
		cmd.Add(field);

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
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HMSET");
	cmd.Add(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		cmd.Add(it->first);
		cmd.Add(it->second);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HSet(const std::string& key, const std::string& field, const std::string& value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HSET");
	cmd.Add(key);
	cmd.Add(field);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}


RedisResultBind& RedisSyncPipeline::HVals(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HVALS");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HScan(const std::string& key, int cursor, const std::string& match, int count)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("HSCAN");
	cmd.Add(key);
	cmd.Add(cursor);
	if (!match.empty())
	{
		cmd.Add("MATCH");
		cmd.Add(match);
	}
	if (count > 0)
	{
		cmd.Add("COUNT");
		cmd.Add(count);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LLen(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("LLEN");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPop(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("LPOP");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPush(const std::string& key, const std::string& value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("LPUSH");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPushs(const std::string& key, const std::vector<std::string>& values)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("LPUSH");
	cmd.Add(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		cmd.Add(*it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LRange(const std::string& key, int start, int stop)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("LRANGE");
	cmd.Add(key);
	cmd.Add(start);
	cmd.Add(stop);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LRem(const std::string& key, int count, const std::string& value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("LREM");
	cmd.Add(key);
	cmd.Add(count);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LTrim(const std::string& key, int start, int stop)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("LTRIM");
	cmd.Add(key);
	cmd.Add(start);
	cmd.Add(stop);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPop(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("RPOP");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPush(const std::string& key, const std::string& value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("RPUSH");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPushs(const std::string& key, const std::vector<std::string>& values)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("RPUSH");
	cmd.Add(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		cmd.Add(*it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SCard(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SCARD");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SDiff(const std::vector<std::string>& keys)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SDIFF");
	for (const auto& it : keys)
	{
		cmd.Add(it);
	}

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
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SINTER");
	for (const auto& it : keys)
	{
		cmd.Add(it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SISMember(const std::string& key, const std::string& value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SISMEMBER");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SMembers(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SMEMBERS");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SPop(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SPOP");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SRandMember(const std::string& key)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SRANDMEMBER");
	cmd.Add(key);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SRem(const std::string& key, const std::string& value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SREM");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SRems(const std::string& key, const std::vector<std::string>& values)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SREM");
	cmd.Add(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		cmd.Add(*it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SUnion(const std::vector<std::string>& keys)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SUNION");
	for (const auto& it : keys)
	{
		cmd.Add(it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("EVAL");
	cmd.Add(script);
	cmd.Add(keys.size());

	for (const auto& it : keys)
	{
		cmd.Add(it);
	}
	for (const auto& it : args)
	{
		cmd.Add(it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Evalsha(const std::string& scriptsha1, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("EVALSHA");
	cmd.Add(scriptsha1);
	cmd.Add(keys.size());

	for (const auto& it : keys)
	{
		cmd.Add(it);
	}
	for (const auto& it : args)
	{
		cmd.Add(it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ScriptExists(const std::string& scriptsha1)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SCRIPT");
	cmd.Add("EXISTS");
	cmd.Add(scriptsha1);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ScriptExists(const std::vector<std::string>& scriptsha1s)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SCRIPT");
	cmd.Add("EXISTS");
	for (const auto& it : scriptsha1s)
	{
		cmd.Add(it);
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ScriptFlush()
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SCRIPT");
	cmd.Add("FLUSH");

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ScriptKill()
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SCRIPT");
	cmd.Add("KILL");

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ScriptLoad(const std::string& script)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SCRIPT");
	cmd.Add("LOAD");
	cmd.Add(script);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

bool RedisSyncPipeline::Do()
{
	if ((int)m_cmds.size() == 0)
	{
		return true;
	}
	std::vector<RedisResult> rst;
	return Do(rst);
}

bool RedisSyncPipeline::Do(std::vector<RedisResult>& rst)
{
	int cmdcount = (int)m_cmds.size();
	if (cmdcount == 0)
	{
		return true;
	}

	std::vector<RedisResult> rst_;
	if (!m_redis.DoCommand(m_cmds, rst_))
	{
		m_cmds.clear();
		m_binds.clear();
		m_binds2.clear();
		return false;
	}

	rst.clear(); // 防止外部带入数据影响下面的逻辑
	for (int i = 0; i < rst_.size(); ++i)
	{
		rst.push_back(rst_[i]);
		if (rst_[i].IsError())
		{
		}
		else
		{
			if (m_binds[i].callback)
			{
				m_binds[i].callback(rst_[i]);
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
	m_cmds.clear();
	m_binds.clear();
	m_binds2.clear();
	return true;
}