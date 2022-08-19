#ifndef _REDISSYNCPIPELINE_H_
#define _REDISSYNCPIPELINE_H_

// by git@github.com:yuwf/redis.git

#include "RedisSync.h"

class RedisSyncPipeline
{
public:
	RedisSyncPipeline(RedisSync& redis) : m_redis(redis) {};
	virtual ~RedisSyncPipeline() {}

	// 字符串命令
	RedisResultBind& Command(const std::string& str); // 绑定值 根据命令来确定


	//////////////////////////////////////////////////////////////////////////
	// DEL命令
	RedisResultBind& Del(const std::string& key);	// int 删除个数
	RedisResultBind& Del(const std::vector<std::string>& key);

	RedisResultBind& Dump(const std::string& key);	// string

	RedisResultBind& Exists(const std::string& key); // int 0不存在 1存在

	RedisResultBind& Expire(const std::string& key, long long value);//int 1成功 0失败
	RedisResultBind& ExpireAt(const std::string& key, long long value);
	RedisResultBind& PExpire(const std::string& key, long long value);
	RedisResultBind& PExpireAt(const std::string& key, long long value);
	RedisResultBind& Persist(const std::string& key);
	RedisResultBind& TTL(const std::string& key);	//long long 剩余的过期时间
	RedisResultBind& PTTL(const std::string& key);

	RedisResultBind& Keys(const std::string& pattern); // string数组

	RedisResultBind& Move(const std::string& key, int index); //int 0成功 1失败

	RedisResultBind& RandomKey(); // string

	RedisResultBind& Rename(const std::string& key, const std::string& newkey); // string OK:成功 空字符串失败
	RedisResultBind& RenameNX(const std::string& key, const std::string& newkey);//int 1成功 0失败

	RedisResultBind& Scan(int cursor, const std::string& match, int count); // Array 0位：表示下个游标(=0:结尾) 1位：string数组 key
	
	RedisResultBind& Type(const std::string& key); // string [none、string、list、set、zset、hash]


	//////////////////////////////////////////////////////////////////////////
	// GET 命令
	RedisResultBind& Get(const std::string& key); // string

	// INCR命令
	RedisResultBind& Incr(const std::string& key); // int 最新值

	// INCRBY命令
	RedisResultBind& Incrby(const std::string& key, long long value); // long long 最新值
	template<class Value>
	RedisResultBind& Incrby(const std::string& key, Value value)
	{
		return Incrby(key, (long long)value);
	}

	// MGET命令
	RedisResultBind& MGet(const std::vector<std::string>& keys); // string数组

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

	// SET 命令
	// ex(秒) 和 px(毫秒) 只能使用一个，另一个必须有-1, 否则优先使用ex
	RedisResultBind& Set(const std::string& key, const std::string& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false); // string OK:成功 空字符串失败
	template<class Value>
	RedisResultBind& Set(const std::string& key, const Value& value, unsigned int ex = -1, unsigned int px = -1, bool nx = false)
	{
		return Set(key, Redis::to_string(value), ex, px, nx);
	}


	//////////////////////////////////////////////////////////////////////////
	// HEDL命令
	RedisResultBind& HDel(const std::string& key, const std::string& field); // int 删除个数
	RedisResultBind& HDels(const std::string& key, const std::vector<std::string>& fields);
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

	// HEXISTS命令
	RedisResultBind& HExists(const std::string& key, const std::string& field); // int 0不存在 1存在
	template<class Field>
	RedisResultBind& HExists(const std::string& key, const Field& field)
	{
		return HExists(key, Redis::to_string(field));
	}

	// HGET命令
	RedisResultBind& HGet(const std::string& key, const std::string& field); // string
	template<class Field>
	RedisResultBind& HGet(const std::string& key, const Field& field)
	{
		return HGet(key, Redis::to_string(field));
	}

	// HGETALL命令
	RedisResultBind& HGetAll(const std::string& key); // string数组 key 和 value
	// MHMGETALL自定义复合命令 获取多个key中的值
	RedisResultBind& MHGetAll(const std::vector<std::string>& keys); // string两维数组

	// HINCRBY命令
	RedisResultBind& HIncrby(const std::string& key, const std::string& field, long long value); // int 最新值
	template<class Field, class Value>
	RedisResultBind& HIncrby(const std::string& key, const Field& field, Value value)
	{
		return HIncrby(key, Redis::to_string(field), (long long)value);
	}

	// HKEYS命令
	RedisResultBind& HKeys(const std::string& key); // string数组

	// HLEN命令
	RedisResultBind& HLen(const std::string& key); // int 最新值
	
	// HMGET命令
	RedisResultBind& HMGet(const std::string& key, const std::vector<std::string>& fields); // string数组
	template<class FieldList>
	RedisResultBind& HMGet(const std::string& key, const FieldList& fields)
	{
		std::vector<std::string> fields_;
		fields_.reserve(fields.size());
		for (auto it = fields.begin(); it != fields.end(); ++it)
			fields_.emplace_back(Redis::to_string(*it));
		return HMGet(key, fields_);
	}
	// MHMGET自定义复合命令 获取多个key中多个相同field字段的值
	RedisResultBind& MHMGet(const std::vector<std::string>& keys, const std::vector<std::string>& fields); // string两维数组
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

	// HSET命令
	RedisResultBind& HSet(const std::string& key, const std::string& field, const std::string& value); // int 之前没有返回1 之前有返回0
	template<class Field, class Value>
	RedisResultBind& HSet(const std::string& key, const Field& field, const Value& value)
	{
		return HSet(key, Redis::to_string(field), Redis::to_string(value));
	}

	// HVALS命令
	RedisResultBind& HVals(const std::string& key); // string数组

	// HSCAN
	RedisResultBind& HScan(const std::string& key, int cursor, const std::string& match, int count); // Array 0位：表示下个游标(=0:结尾) 1位：string数组 key 和 value
	

	//////////////////////////////////////////////////////////////////////////
	// LLEN命令
	RedisResultBind& LLen(const std::string& key); // int 长度

	// LPOP命令
	RedisResultBind& LPop(const std::string& key); // string 被移除的元素

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

	// LRANGE命令
	// 1成功 0不成功 -1表示网络或其他错误
	// 若成功 rst为数组 元素为string
	RedisResultBind& LRange(const std::string& key, int start, int stop); // string数组

	// LREM命令
	RedisResultBind& LRem(const std::string& key, int count, const std::string& value); // int 移除的数量
	template<class Value>
	RedisResultBind&  LRem(const std::string& key, int count, const Value& value)
	{
		return LRem(key, count, Redis::to_string(value));
	}

	// LTRIM命令
	RedisResultBind& LTrim(const std::string& key, int start, int stop);// string OK 或者 nil

	// RPOP命令
	RedisResultBind& RPop(const std::string& key); // string 被移除的元素
	
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

	//////////////////////////////////////////////////////////////////////////
	// SADD命令
	template<class Value>
	RedisResultBind& SAdd(const std::string& key, const Value& value); // int 添加的数量
	template<class ValueList>
	RedisResultBind& SAdds(const std::string& key, const ValueList& values); // int 添加的数量

	// SCARD命令
	RedisResultBind& SCard(const std::string& key); // int 长度

	// SDIFF命令
	RedisResultBind& SDiff(const std::vector<std::string>& keys); // string数组

	// SINTER命令
	RedisResultBind& Sinter(const std::vector<std::string>& keys); // string数组

	// 	SISMEMBER命令
	RedisResultBind& SISMember(const std::string& key, const std::string& value); // int 1存在 0不存在
	template<class Value>
	RedisResultBind& SISMember(const std::string& key, const Value& value)
	{
		return SISMember(key, Redis::to_string(value));
	}

	// SMEMBERS命令
	RedisResultBind& SMembers(const std::string& key); // string数组

	// SPOP命令
	RedisResultBind& SPop(const std::string& key); // string

	// SRANDMEMBER命令
	RedisResultBind& SRandMember(const std::string& key); // string

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

	// SUNION命令
	RedisResultBind& SUnion(const std::vector<std::string>& keys); // string数组


	//////////////////////////////////////////////////////////////////////////
	// EVAL命令
	RedisResultBind& Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args); // 根据返回值来绑定
	// EVALSHA命令
	RedisResultBind& Evalsha(const std::string& scriptsha1, const std::vector<std::string>& keys, const std::vector<std::string>& args); // 根据返回值来绑定
	// SCRIPT EXISTS命令
	RedisResultBind& ScriptExists(const std::string& scriptsha1); // int 1存在 0不存在
	RedisResultBind& ScriptExists(const std::vector<std::string>& scriptsha1s); // int数组
	// SCRIPT FLUSH命令
	RedisResultBind& ScriptFlush(); // string OK
	// SCRIPT KILL命令
	RedisResultBind& ScriptKill(); // string OK
	// SCRIPT LOAD命令
	RedisResultBind& ScriptLoad(const std::string& script); // string script的SHA1校验和

	// 执行批处理
	bool Do();
	bool Do(std::vector<RedisResult>& rst);

protected:
	RedisSync& m_redis;
	std::vector<RedisCommand> m_cmds;
	std::vector<RedisResultBind> m_binds;

	// 自定义复合命令使用 支持绑定多条命令
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

template<class Value>
RedisResultBind& RedisSyncPipeline::SAdd(const std::string& key, const Value& value)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SADD");
	cmd.Add(key);
	cmd.Add(value);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

template<class ValueList>
RedisResultBind& RedisSyncPipeline::SAdds(const std::string& key, const ValueList& values)
{
	m_cmds.push_back(RedisCommand());
	RedisCommand& cmd = m_cmds.back();
	cmd.Add("SADD");
	cmd.Add(key);
	for (const auto& it : values)
	{
		cmd.Add(Redis::to_string(it));
	}

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

#endif