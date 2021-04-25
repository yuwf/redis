#ifndef _REDIS_HSETGET_H_
#define _REDIS_HSETGET_H_

#include <stdlib.h>
#include <ostream>
#include <string>
#include <vector>
#include <regex>
#include <boost/preprocessor/seq.hpp>
#include <boost/preprocessor/tuple.hpp>
#include "RedisSync.h"

inline void RedisStringSplit(const std::string& input, std::vector<std::string>& output, const std::string& delim)
{
	try
	{
		std::regex re{ delim };
		output = std::vector<std::string>{ std::sregex_token_iterator(input.begin(), input.end(), re, -1), std::sregex_token_iterator() };
	}
	catch (...)
	{
	}
}

template<class VALUE>
inline std::string ToRedisString(const VALUE& x)
{
	return Redis::to_string(x);
}

template<class VALUE>
inline void FromRedisString(const std::string& x, VALUE& v)
{
	Redis::string_to(x, v);
}

// vector对象
inline std::string ToRedisString(const std::vector<int>& x)
{
	std::ostringstream pt;
	for (Lint i = 0; i < (Lint)x.size(); ++i)
	{
		if (i != 0)
		{
			pt << ";";
		}
		pt << x[i];
	}
	return std::move(pt.str());
}

inline void FromRedisString(const std::string& x, std::vector<int>& v)
{
	std::vector<std::string> des;
	RedisStringSplit(x, des, ";");
	v.reserve(des.size());
	for (auto it = des.begin(); it != des.end(); ++it)
	{
		v.push_back(atoi(it->c_str()));
	}
}

#define REDIS_H_KV_SET(r, index, elem) kvs[fields[index++]] = ToRedisString(elem);
#define REDIS_H_KV_GET(r, index, elem) FromRedisString(ar[index++].ToString(),elem);

// 参数为可以转化std::string的seq
#define REDIS_HFIELD(...) \
	static const std::vector<std::string>& RedisHField() \
	{ \
		static std::vector<std::string> fields = { BOOST_PP_TUPLE_REM_CTOR(BOOST_PP_SEQ_TO_TUPLE(__VA_ARGS__)) }; \
		return fields; \
	}

// 参数为结构字段seq ()()()
#define REDIS_HMSET(...) \
	void RedisHMSet(RedisSync& redis, const std::string& key, long long exp = 0) const \
	{ \
		const std::vector<std::string>& fields = RedisHField(); \
		std::map<std::string, std::string> kvs; \
		Lint index = 0; \
		BOOST_PP_SEQ_FOR_EACH(REDIS_H_KV_SET, index, __VA_ARGS__); \
		redis.HMSet(key, kvs); \
		if ( exp > 0 ) \
			redis.Expire(key, exp); \
	}\
	void RedisHMSet(RedisSyncPipeline& pipeline, const std::string& key, long long exp = 0) const \
	{ \
		const std::vector<std::string>& fields = RedisHField(); \
		std::map<std::string, std::string> kvs; \
		Lint index = 0; \
		BOOST_PP_SEQ_FOR_EACH(REDIS_H_KV_SET, index, __VA_ARGS__); \
		pipeline.HMSet(key, kvs); \
		if ( exp > 0 ) \
			pipeline.Expire(key, exp); \
	}

// 参数为结构字段seq ()()()
// 读取时空的表示不存在
#define REDIS_HMGET(...) \
	bool RedisHMGet(RedisSync& redis, const std::string& key) \
	{ \
		RedisResult rst; \
		if (redis.HMGet(key, RedisHField(), rst) != 1) \
			return false; \
		if (!rst.IsArray() || rst.IsEmptyArray()) \
			return false; \
		const RedisResult::Array& ar = rst.ToArray(); \
		if (ar.size() != BOOST_PP_SEQ_SIZE(__VA_ARGS__)) \
			return false; \
		Lint index = 0; \
		BOOST_PP_SEQ_FOR_EACH(REDIS_H_KV_GET, index, __VA_ARGS__); \
		return true; \
	} \
	void RedisHMGet(RedisSyncPipeline& pipeline, const std::string& key) \
	{ \
		pipeline.HMGet(key, RedisHField()).BindObj(*this); \
	}

// 参数为结构字段seq ()()()
// 读取时空的表示不存在
#define REDIS_FROMHMGET(...) \
	bool RedisFromHMGet(const RedisResult& rst) \
	{ \
		if (!rst.IsArray() || rst.IsEmptyArray()) \
			return false; \
		const RedisResult::Array& ar = rst.ToArray(); \
		if (ar.size() != BOOST_PP_SEQ_SIZE(__VA_ARGS__)) \
			return false; \
		Lint index = 0; \
		BOOST_PP_SEQ_FOR_EACH(REDIS_H_KV_GET, index, __VA_ARGS__); \
		return true; \
	} \
	bool From(const RedisResult& rst) \
	{ \
		return RedisFromHMGet(rst); \
	}

#define REDIS_SEQ_FOR(...) REDIS_SEQ_FOR_TAIL(__VA_ARGS__)
#define REDIS_SEQ_FOR_TAIL(...) __VA_ARGS__##0

// (first, second)(first, second)(first, second) -> (first)(first)(first)
#define REDIS_PAIR_FIRST_A(first, second) REDIS_PAIR_FIRST_I(first, second) REDIS_PAIR_FIRST_B
#define REDIS_PAIR_FIRST_B(first, second) REDIS_PAIR_FIRST_I(first, second) REDIS_PAIR_FIRST_A
#define REDIS_PAIR_FIRST_A0
#define REDIS_PAIR_FIRST_B0
#define REDIS_PAIR_FIRST_I(first, second) (first)

// (first, second)(first, second)(first, second) -> (second)(second)(second)
#define REDIS_PAIR_SECOND_A(first, second) REDIS_PAIR_SECOND_I(first, second) REDIS_PAIR_SECOND_B
#define REDIS_PAIR_SECOND_B(first, second) REDIS_PAIR_SECOND_I(first, second) REDIS_PAIR_SECOND_A
#define REDIS_PAIR_SECOND_A0
#define REDIS_PAIR_SECOND_B0
#define REDIS_PAIR_SECOND_I(first, second) (second)


// (field,value)(field,value)
// field为可以转化std::string的类型 value为结构字段
#define REDIS_HMSETGET(...) \
	REDIS_HFIELD(REDIS_SEQ_FOR(REDIS_PAIR_FIRST_A __VA_ARGS__)) \
	REDIS_HMSET(REDIS_SEQ_FOR(REDIS_PAIR_SECOND_A __VA_ARGS__)) \
	REDIS_HMGET(REDIS_SEQ_FOR(REDIS_PAIR_SECOND_A __VA_ARGS__)) \
	REDIS_FROMHMGET(REDIS_SEQ_FOR(REDIS_PAIR_SECOND_A __VA_ARGS__))


#endif