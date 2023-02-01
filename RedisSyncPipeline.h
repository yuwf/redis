#ifndef _REDISSYNCPIPELINE_H_
#define _REDISSYNCPIPELINE_H_

// by git@github.com:yuwf/redis.git

#include "RedisSync.h"

class RedisSyncPipeline
{
public:
	RedisSyncPipeline(RedisSync& redis) : m_redis(redis) {};
	virtual ~RedisSyncPipeline() {}

	// 绑定值 根据命令来确定
	RedisResultBind& DoCommand(const RedisCommand& cmd)
	{
		m_cmds.push_back(cmd);
		m_binds.emplace_back();
		return m_binds.back();
	}

	RedisResultBind& DoCommand(RedisCommand&& cmd)
	{
		m_cmds.emplace_back(std::forward<RedisCommand>(cmd));
		m_binds.emplace_back();
		return m_binds.back();
	}

	// 执行批处理
	bool Do()
	{
		if ((int)m_cmds.size() == 0) return true;
		std::vector<RedisResult> rst;
		return Do(rst);
	}
	bool Do(std::vector<RedisResult>& rst)
	{
		if ((int)m_cmds.size() == 0) return true;

		// 缓存直接move掉 防止下面的回调中出现了异常，导致本缓存的数据没有清除
		std::vector<RedisCommand> cmds(std::move(m_cmds));
		std::vector<RedisResultBind> binds(std::move(m_binds));
		std::map<int, std::shared_ptr<_RedisResultBind_>> binds2(std::move(m_binds2));

		std::vector<RedisResult> rst_;
		if (!m_redis.DoCommand(cmds, rst_))
		{
			return false;
		}
		rst.reserve(rst.size() + rst_.size());
		for (int i = 0; i < rst_.size(); ++i)
		{
			rst.emplace_back(std::move(rst_[i]));
			if (binds[i].callback)
			{
				binds[i].callback(rst.back());
			}
			// 自定义复合命令
			auto it = binds2.find(i);
			if (it != binds2.end())
			{
				// 从rst中pop出这个命令 放到复合命令的数组中 复合命令组织完后 将组织的result放到rst中
				it->second->m_result.emplace_back(std::move(rst.back()));
				rst.pop_back();
				if (i == it->second->m_end)
				{
					rst.emplace_back(RedisResult(std::move(it->second->m_result)));
					if (it->second->m_bind.callback)
					{
						it->second->m_bind.callback(rst.back());
					}
				}
			}
		}
		return true;
	}

	//////////////////////////////////////////////////////////////////////////
	// DEL命令 绑定：int 删除个数
	RedisResultBind& Del(const std::string& key)
	{
		return DoCommand(RedisCommand("DEL", key));
	}

	// DUMP命令 绑定：string
	RedisResultBind& Dump(const std::string& key)
	{
		return DoCommand(RedisCommand("DUMP", key));
	}

	//EXISTS命令 绑定：int 0不存在 1存在
	RedisResultBind& Exists(const std::string& key)
	{
		return DoCommand(RedisCommand("EXISTS", key));
	}

	// EXPIRE 绑定：int 1成功 0失败
	RedisResultBind& Expire(const std::string& key, long long value)
	{
		return DoCommand(RedisCommand("EXPIRE", key, value));
	}
	RedisResultBind& ExpireAt(const std::string& key, long long value)
	{
		return DoCommand(RedisCommand("EXPIREAT", key, value));
	}
	RedisResultBind& PExpire(const std::string& key, long long value)
	{
		return DoCommand(RedisCommand("PEXPIRE", key, value));
	}
	RedisResultBind& PExpireAt(const std::string& key, long long value)
	{
		return DoCommand(RedisCommand("PEXPIREAT", key, value));
	}
	// PERSIST命令 绑定：int 1成功 0失败
	RedisResultBind& Persist(const std::string& key)
	{
		return DoCommand(RedisCommand("PERSIST", key));
	}

	// TTL命令 绑定：long long 剩余的过期时间
	RedisResultBind& TTL(const std::string& key)
	{
		return DoCommand(RedisCommand("TTL", key));
	}
	RedisResultBind& PTTL(const std::string& key)
	{
		return DoCommand(RedisCommand("PTTL", key));
	}

	//KEYS命令 绑定：string数组
	RedisResultBind& Keys(const std::string& pattern)
	{
		return DoCommand(RedisCommand("KEYS", pattern));
	}

	// MOVE命令 绑定：int 0成功 1失败
	RedisResultBind& Move(const std::string& key, int index)
	{
		return DoCommand(RedisCommand("MOVE", key, index));
	}

	// RANDOMKEY命令 绑定： string
	RedisResultBind& RandomKey()
	{
		return DoCommand(RedisCommand("RANDOMKEY"));
	}

	// RENAME命令 绑定：string OK:成功 空字符串失败
	RedisResultBind& Rename(const std::string& key, const std::string& newkey)
	{
		return DoCommand(RedisCommand("RENAME", key, newkey));
	}

	//RENAMENX命令 绑定：int 1成功 0失败
	RedisResultBind& RenameNX(const std::string& key, const std::string& newkey)
	{
		return DoCommand(RedisCommand("RENAMENX", key, newkey));
	}

	// SCAN命令 绑定：Array 0位：表示下个游标(=0:结尾) 1位：string数组 key
	RedisResultBind& Scan(int cursor, const std::string& match, int count)
	{
		RedisCommand cmd("SCAN", cursor);
		if (!match.empty()) { cmd.Add("MATCH"); cmd.Add(match); }
		if (count > 0) { cmd.Add("COUNT"); cmd.Add(count); }
		return DoCommand(std::move(cmd));
	}
	
	// TYPE命令 绑定：string [none、string、list、set、zset、hash]
	RedisResultBind& Type(const std::string& key)
	{
		return DoCommand(RedisCommand("TYPE", key));
	}


	//////////////////////////////////////////////////////////////////////////
	// GET 命令 绑定：string
	RedisResultBind& Get(const std::string& key)
	{
		return DoCommand(RedisCommand("GET", key));
	}

	// INCR命令 绑定：int 最新值
	RedisResultBind& Incr(const std::string& key)
	{
		return DoCommand(RedisCommand("INCR", key));
	}

	// INCRBY命令 绑定：int 最新值
	template<class Value>
	RedisResultBind& Incrby(const std::string& key, Value value)
	{
		return DoCommand(RedisCommand("INCRBY", key, value));
	}

	// MGET命令 绑定：string数组
	template<class KeyList>
	RedisResultBind& MGet(const KeyList& keys)
	{
		return DoCommand(RedisCommand("MGET", keys));
	}

	// MSET命令 绑定：string OK
	template<class FieldValueMap>
	RedisResultBind& MSet(const FieldValueMap& kvs)
	{
		if (kvs.empty()) return RedisResultBind::Empty();
		return DoCommand(RedisCommand("MSET", kvs));
	}

	// SET 命令 ex(秒) 和 px(毫秒) 只能使用一个，另一个必须有-1, 否则优先使用ex 绑定：string OK:成功 空字符串失败
	template<class Value>
	RedisResultBind& Set(const std::string& key, const Value& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false)
	{
		RedisCommand cmd("SET", key, value);
		if (ex != -1) { cmd.Add("EX"); cmd.Add(ex); }
		else if (px != -1) { cmd.Add("PX"); cmd.Add(px); }
		if (nx) { cmd.Add("NX"); }
		return DoCommand(std::move(cmd));
	}


	//////////////////////////////////////////////////////////////////////////
	// HEDL命令 绑定：int 删除个数
	template<class Field>
	RedisResultBind& HDel(const std::string& key, const Field& field)
	{
		return DoCommand(RedisCommand("HDEL", key, field));
	}

	// HEXISTS命令 绑定：
	template<class Field>
	RedisResultBind& HExists(const std::string& key, const Field& field) // int 0不存在 1存在
	{
		return DoCommand(RedisCommand("HEXISTS", key, field));
	}

	// HGET命令 绑定：string
	template<class Field>
	RedisResultBind& HGet(const std::string& key, const Field& field)
	{
		return DoCommand(RedisCommand("HGET", key, field));
	}

	// HGETALL命令 绑定：string数组 key 和 value
	RedisResultBind& HGetAll(const std::string& key)
	{
		return DoCommand(RedisCommand("HGETALL", key));
	}

	// MHMGETALL自定义复合命令 获取多个key中的值 绑定：string两维数组
	template<class KeyList>
	RedisResultBind& MHGetAll(const KeyList& keys)
	{
		std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
		bindptr->m_end = m_binds.size() - 1 + keys.size();
		for (auto it : keys)
		{
			DoCommand(RedisCommand("HGETALL", it));
			m_binds2[m_binds.size() - 1] = bindptr; // 记录下标对应的新绑定
		}
		return bindptr->m_bind;
	}

	// HINCRBY命令 绑定：int 最新值
	template<class Field, class Value>
	RedisResultBind& HIncrby(const std::string& key, const Field& field, Value value)
	{
		return DoCommand(RedisCommand("HINCRBY", key, field, value));
	}

	// HKEYS命令 绑定：string数组
	RedisResultBind& HKeys(const std::string& key)
	{
		return DoCommand(RedisCommand("HKEYS", key));
	}

	// HLEN命令 绑定：int 最新值
	RedisResultBind& HLen(const std::string& key)
	{
		return DoCommand(RedisCommand("HLEN", key));
	}
	
	// HMGET命令  绑定：string数组
	template<class FieldList>
	RedisResultBind& HMGet(const std::string& key, const FieldList& fields)
	{
		if (fields.empty()) return RedisResultBind::Empty();
		return DoCommand(RedisCommand("HMGET", key, fields));
	}

	// MHMGET自定义复合命令 获取多个key中多个相同field字段的值 绑定：string两维数组
	template<class KeyList, class FieldList>
	RedisResultBind& MHMGet(const KeyList& keys, const FieldList& fields)
	{
		if (fields.empty()) return RedisResultBind::Empty();
		std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
		bindptr->m_end = m_binds.size() - 1 + keys.size();
		for (auto kit : keys)
		{
			DoCommand(RedisCommand("HMGET", kit, fields));
			m_binds2[m_binds.size() - 1] = bindptr; // 记录下标对应的新绑定
		}
		return bindptr->m_bind;
	}

	// MHGET自定义复合命令 获取多个key中相同field字段的值 绑定：string数组
	template<class KeyList>
	RedisResultBind& MHGet(const KeyList& keys, const std::string& field)
	{
		if (keys.empty()) return RedisResultBind::Empty();
		std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
		bindptr->m_end = m_binds.size() - 1 + keys.size();
		for (auto it : keys)
		{
			DoCommand(RedisCommand("HGET", it, field));
			m_binds2[m_binds.size() - 1] = bindptr; // 记录下标对应的新绑定
		}
		return bindptr->m_bind;
	}

	// HMSET命令 绑定：string OK
	template<class FieldValueMap>
	RedisResultBind& HMSet(const std::string& key, const FieldValueMap& kvs)
	{
		if (kvs.empty()) return RedisResultBind::Empty();
		return DoCommand(RedisCommand("HMSET", key, kvs));
	}

	// HSET命令 绑定：int 之前没有返回1 之前有返回0
	template<class Field, class Value>
	RedisResultBind& HSet(const std::string& key, const Field& field, const Value& value)
	{
		return DoCommand(RedisCommand("HSET", key, field, value));
	}

	// HSET命令 绑定：int 之前没有返回1 之前有返回0
	template<class Field, class Value>
	RedisResultBind& HSetNX(const std::string& key, const Field& field, const Value& value)
	{
		return DoCommand(RedisCommand("HSETNX", key, field, value));
	}

	// HVALS命令 绑定：string数组
	RedisResultBind& HVals(const std::string& key)
	{
		return DoCommand(RedisCommand("HVALS", key));
	}

	// HSCAN命令 绑定：Array 0位：表示下个游标(=0:结尾) 1位：string数组 key 和 value
	RedisResultBind& HScan(const std::string& key, int cursor, const std::string& match, int count)
	{
		RedisCommand cmd("HSCAN", key, cursor);
		if (!match.empty()) { cmd.Add("MATCH"); cmd.Add(match); }
		if (count > 0) { cmd.Add("COUNT"); cmd.Add(count); }
		return DoCommand(std::move(cmd));
	}
	

	//////////////////////////////////////////////////////////////////////////
	// LLEN命令  绑定：int 长度
	RedisResultBind& LLen(const std::string& key)
	{
		return DoCommand(RedisCommand("LLEN", key));
	}

	// LPOP命令 绑定：string 被移除的元素
	RedisResultBind& LPop(const std::string& key)
	{
		return DoCommand(RedisCommand("LPOP", key));
	}

	// LPUSH命令 绑定：int 最新长度
	template<class Value>
	RedisResultBind& LPush(const std::string& key, const Value& value)
	{
		return DoCommand(RedisCommand("LPUSH", key, value));
	}

	// LPUSHX命令 绑定：int 最新长度
	template<class Value>
	RedisResultBind& LPushX(const std::string& key, const Value& value)
	{
		return DoCommand(RedisCommand("LPUSHX", key, value));
	}

	// LRANGE命令 绑定：string数组
	RedisResultBind& LRange(const std::string& key, int start, int stop) // 
	{
		return DoCommand(RedisCommand("LRANGE", key, start, stop));
	}

	// LREM命令 绑定：int 移除的数量
	template<class Value>
	RedisResultBind& LRem(const std::string& key, int count, const Value& value)
	{
		return DoCommand(RedisCommand("LREM", key, count, value));
	}

	// LSET命令 绑定：string OK 或者 nil
	template<class Value>
	RedisResultBind& LSet(const std::string& key, int index, const Value& value)
	{
		return DoCommand(RedisCommand("LSET", key, index, value));
	}

	// LTRIM命令 绑定：string OK 或者 nil
	RedisResultBind& LTrim(const std::string& key, int start, int stop)
	{
		return DoCommand(RedisCommand("LTRIM", key, start, stop));
	}

	// RPOP命令  绑定:string 被移除的元素
	RedisResultBind& RPop(const std::string& key)
	{
		return DoCommand(RedisCommand("RPOP", key));
	}

	// RPOP命令  绑定:string 被移动的元素
	RedisResultBind& RPopLPush(const std::string& key, const std::string& keydest)
	{
		return DoCommand(RedisCommand("RPOPLPUSH", key, keydest));
	}
	
	// RPUSH命令 绑定：int 最新长度
	template<class Value>
	RedisResultBind& RPush(const std::string& key, const Value& value)
	{
		return DoCommand(RedisCommand("RPUSH", key, value));
	}

	// RPUSHX命令 绑定：int 最新长度
	template<class Value>
	RedisResultBind& RPushX(const std::string& key, const Value& value)
	{
		return DoCommand(RedisCommand("RPUSHX", key, value));
	}

	//////////////////////////////////////////////////////////////////////////
	// SADD命令 绑定：int 添加的数量
	template<class Value>
	RedisResultBind& SAdd(const std::string& key, const Value& value)
	{
		return DoCommand(RedisCommand("SADD", key, value));
	}

	// SCARD命令 绑定：int 长度
	RedisResultBind& SCard(const std::string& key)
	{
		return DoCommand(RedisCommand("SCARD", key));
	}

	// SDIFF命令 绑定：string数组
	template<class KeyList>
	RedisResultBind& SDiff(const KeyList& keys)
	{
		if (keys.empty()) return RedisResultBind::Empty();
		return DoCommand(RedisCommand("SDIFF", keys));
	}

	// SINTER命令 绑定：string数组
	template<class KeyList>
	RedisResultBind& Sinter(const KeyList& keys)
	{
		if (keys.empty()) return RedisResultBind::Empty();
		return DoCommand(RedisCommand("SINTER", keys));
	}

	// 	SISMEMBER命令 绑定：int 1存在 0不存在
	template<class Value>
	RedisResultBind& SISMember(const std::string& key, const Value& value)
	{
		return DoCommand(RedisCommand("SISMEMBER", key, value));
	}

	// SMEMBERS命令 绑定：string数组
	RedisResultBind& SMembers(const std::string& key)
	{
		return DoCommand(RedisCommand("SMEMBERS", key));
	}

	// SPOP命令 绑定：string
	RedisResultBind& SPop(const std::string& key)
	{
		return DoCommand(RedisCommand("SPOP", key));
	}

	// SRANDMEMBER命令 绑定：string
	RedisResultBind& SRandMember(const std::string& key)
	{
		return DoCommand(RedisCommand("SRANDMEMBER", key));
	}

	// SREM命令 绑定：int 移除的数量
	template<class Value>
	RedisResultBind& SRem(const std::string& key, const Value& value)
	{
		return DoCommand(RedisCommand("SREM", key, value));
	}

	// SUNION命令 绑定：string数组
	template<class KeyList>
	RedisResultBind& SUnion(const KeyList& keys)
	{
		if (keys.empty()) return RedisResultBind::Empty();
		return DoCommand(RedisCommand("SUNION", keys));
	}


	//////////////////////////////////////////////////////////////////////////
	// EVAL命令 绑定：根据返回值来绑定
	template<class KeyList, class ArgList>
	RedisResultBind& Eval(const std::string& script, const KeyList& keys, const ArgList& args)
	{
		return DoCommand(RedisCommand("EVAL", script, keys.size(), keys, args));
	}
	// EVALSHA命令 绑定：根据返回值来绑定
	template<class KeyList, class ArgList>
	RedisResultBind& Evalsha(const std::string& scriptsha1, const KeyList& keys, const ArgList& args)
	{
		return DoCommand(RedisCommand("EVALSHA", scriptsha1, keys.size(), keys, args));
	}

	// SCRIPT EXISTS命令 绑定：int 1存在 0不存在
	RedisResultBind& ScriptExists(const std::string& scriptsha1)
	{
		return DoCommand(RedisCommand("SCRIPT", "EXISTS", scriptsha1));
	}
	// SCRIPT EXISTS命令 绑定：int数组
	RedisResultBind& ScriptExists(const std::vector<std::string>& scriptsha1s)
	{
		if (scriptsha1s.empty()) return RedisResultBind::Empty();
		return DoCommand(RedisCommand("SCRIPT", "EXISTS", scriptsha1s));
	}
	// SCRIPT FLUSH命令 绑定：string OK
	RedisResultBind& ScriptFlush()
	{
		return DoCommand(RedisCommand("SCRIPT", "FLUSH"));
	}
	// SCRIPT KILL命令 绑定：string OK
	RedisResultBind& ScriptKill()
	{
		return DoCommand(RedisCommand("SCRIPT", "KILL"));
	}
	// SCRIPT LOAD命令 绑定：string script的SHA1校验和
	RedisResultBind& ScriptLoad(const std::string& script)
	{
		return DoCommand(RedisCommand("SCRIPT", "LOAD", script));
	}
	// 执行脚本 绑定：根据返回值来绑定
	template<class KeyList, class ArgList>
	RedisResultBind& Script(const RedisScript& script, const KeyList& keys, const ArgList& args)
	{
		std::shared_ptr<RedisResultBind> bindptr = std::make_shared<RedisResultBind>();
		Evalsha(script.scriptsha1, keys, args).callback = [&, bindptr, script, keys, args](const RedisResult& rst)
		{
			if (rst.IsError() && rst.ToString().find("NOSCRIPT ") != std::string::npos) // 如果是脚本不存在的错误
			{
				RedisResult rst2;
				m_redis.Eval(script.script, keys, args, rst2); // 直接执行Eval 他也会加载脚本
				if (bindptr->callback)
					bindptr->callback(rst2);
			}
			else
			{
				if (bindptr->callback)
					bindptr->callback(rst);
			}
		};
		return *bindptr.get();
	}

protected:
	RedisSync& m_redis;
	std::vector<RedisCommand> m_cmds;
	std::vector<RedisResultBind> m_binds;

	// 自定义复合命令使用 支持绑定多条命令
	struct _RedisResultBind_
	{
		int m_end = 0;	// 结束为止 对应m_binds下标
		RedisResultBind m_bind;
		RedisResult::Array m_result;	// 存储符合命令的结果集 结果组织完就被move了
	};
	std::map<int, std::shared_ptr<_RedisResultBind_>> m_binds2; // index对应m_binds下标

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