﻿#ifndef _REDIS_H_
#define _REDIS_H_

// by git@github.com:yuwf/redis.git

#include <set>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <boost/any.hpp>

// 需要依赖Clock git@github.com:yuwf/clock.git
#include "Clock.h"

// 需要依赖Locker git@github.com:yuwf/locker.git
#include "Locker.h"

// 依赖的日志输出，自行定义
#define RedisLogFatal
#define RedisLogError
#define RedisLogInfo
//#include "LLog.h"  
//#define RedisLogFatal LLOG_FATAL
//#define RedisLogError LLOG_ERROR
//#define RedisLogInfo LLOG_INFO

struct RedisResult;
class Redis
{
protected:
	Redis();
	virtual ~Redis();

	// 该函数通过调用下面的ReadToCRLF获取数据
	// 注意：ReadToCRLF返回的数据要求ReadReply结束前一直有效
	// 返回值 -1:网络错误 -2:协议解析错误 0:未读取到 1:读取到了
	int ReadReply(RedisResult& rst);

	// 解释同ReadReply
	int _ReadReply(RedisResult& rst);

	// buff表示数据地址 buff中包括\r\n minlen表示buff中不包括\r\n的最少长度
	// 返回值表示buff长度 -1:网络读取失败 0:没有读取到
	virtual int ReadToCRLF(char** buff, int minlen) = 0;

	// 读取回滚 ReadReply函数通过ReadToCRLF读取的数据不是完整的数据，回滚本次读取
	virtual bool ReadRollback(int len) = 0;

	int m_readlen = 0;

public:
	// 转化成string
	static const std::string& to_string(const std::string& v)
	{
		return v;
	}
	static std::string to_string(const char* v)
	{
		return std::move(std::string(v));
	}
	template<class TYPE>
	static std::string to_string(const TYPE& v)
	{
		return std::move(std::to_string(v));
	}
	// 对应string_to的解析
	static std::string& string_to(const std::string& x, std::string& v)
	{
		v = x;
		return v;
	}
	static int string_to(const std::string& x, int& v)
	{
		v = atoi(x.c_str());
		return v;
	}
	static unsigned int string_to(const std::string& x, unsigned int& v)
	{
		v = (unsigned int)atoi(x.c_str());
		return v;
	}
	static int string_to(const std::string& x, long& v)
	{
		v = atol(x.c_str());
		return v;
	}
	static unsigned long string_to(const std::string& x, unsigned long& v)
	{
		v = (unsigned long)atol(x.c_str());
		return v;
	}
	static long long string_to(const std::string& x, long long& v)
	{
		v = strtoll(x.c_str(), NULL, 10);
		return v;
	}
	static unsigned long long string_to(const std::string& x, unsigned long long& v)
	{
		v = (unsigned long long)strtoll(x.c_str(), NULL, 10);
		return v;
	}
	static float string_to(const std::string& x, float& v)
	{
		v = (float)atof(x.c_str());
		return v;
	}
	static double string_to(const std::string& x, double& v)
	{
		v = atof(x.c_str());
		return v;
	}
	//reserve
	template<class TYPE>
	static void reserve(std::vector<TYPE>& v, std::size_t size)
	{
		v.reserve(size);
	}
	template<class TYPE>
	static void reserve(std::list<TYPE>& v, std::size_t size)
	{	
	}
	template<class TYPE>
	static void reserve(std::set<TYPE>& v, std::size_t size)
	{
	}
	template<class KEY, class VALUE>
	static void reserve(std::map<KEY, VALUE>& v, std::size_t size)
	{
	}
	template<class KEY, class VALUE>
	static void reserve(std::unordered_map<KEY, VALUE>& v, std::size_t size)
	{
		v.reserve(size);
	}

private:
	// 禁止拷贝
	Redis(const Redis&) = delete;
	Redis& operator=(const Redis&) = delete;
};

struct RedisCommand
{
	RedisCommand()
	{
	}
	RedisCommand(const std::string& cmdname)
	{ Add(cmdname); }

	template<class T1>
	RedisCommand(const std::string& cmdname, const T1& t1)
	{ Add(cmdname); Add(t1); }

	template<class T1, class T2>
	RedisCommand(const std::string& cmdname, const T1& t1, const T2& t2)
	{ Add(cmdname); Add(t1); Add(t2); }

	template<class T1, class T2, class T3>
	RedisCommand(const std::string& cmdname, const T1& t1, const T2& t2, const T3& t3)
	{ Add(cmdname); Add(t1); Add(t2); Add(t3); }

	template<class T1, class T2, class T3, class T4>
	RedisCommand(const std::string& cmdname, const T1& t1, const T2& t2, const T3& t3, const T4& t4)
	{ Add(cmdname); Add(t1); Add(t2); Add(t3); Add(t4); }

	template<class T1, class T2, class T3, class T4, class T5>
	RedisCommand(const std::string& cmdname, const T1& t1, const T2& t2, const T3& t3, const T4& t4, const T5& t5)
	{ Add(cmdname); Add(t1); Add(t2); Add(t3); Add(t4); Add(t5); }

	template<class T1, class T2, class T3, class T4, class T5, class T6>
	RedisCommand(const std::string& cmdname, const T1& t1, const T2& t2, const T3& t3, const T4& t4, const T5& t5, const T6& t6)
	{ Add(cmdname); Add(t1); Add(t2); Add(t3); Add(t4); Add(t5); Add(t6); }

	template<class T>
	void Add(const T& t)
	{
		buff.emplace_back(Redis::to_string(t));
	}
	template<class T>
	void Add(const std::vector<T>& t)
	{
		for (const auto& it : t)
		{
			buff.emplace_back(Redis::to_string(it));
		}
	}
	template<class K, class V>
	void Add(const std::map<K, V>& t)
	{
		for (const auto& it : t)
		{
			buff.emplace_back(Redis::to_string(it.first));
			buff.emplace_back(Redis::to_string(it.second));
		}
	}

	// 参数为 "set key 123" 样式, 命令是空格分隔，支持字符串内空格引号安全保护
	void FromString(const std::string& cmd);

	// 转化为 "set key 123" 样式
	std::string ToString() const;

	// 命令按照Redis协议格式写入到stream中
	template<class Stream>
	void ToStream(Stream &stream) const
	{
		static std::string rn = "\r\n";
		stream << "*" << buff.size() << rn;
		for (const auto& it : buff)
		{
			stream << "$" << it.size() << rn << it << rn;
		}
	}

protected:
	std::vector<std::string> buff;
};

// Redis结果值对象
struct RedisResult
{
	typedef std::vector<RedisResult> Array;

	bool IsError() const;

	bool IsNull() const;
	bool IsInt() const;
	bool IsString() const;
	bool IsArray() const;
	bool IsEmptyArray() const;

	// 若返回值是string类型 次类型也支持ToInt和ToLongLong转换
	int ToInt() const;
	long long ToLongLong() const;
	const std::string& ToString() const;
	const Array& ToArray() const;

	void Clear();

	// String类型使用 方便使用
	int StringToInt() const;
	long long StringToLongLong() const;
	float StringToFloat() const;
	double StringToDouble() const;

	// Array中String类型 方便使用
	template<class ListType>
	bool ToArray(ListType& values) const
	{
		if (!IsArray())
			return false;
		const RedisResult::Array& ar = ToArray();
		Redis::reserve(values, values.size() + ar.size());
		for (auto it = ar.begin(); it != ar.end(); ++it)
		{
			typename ListType::value_type v;
			Redis::string_to(it->ToString(), v);
			values.insert(values.end(), v);
		}
		return true;
	}
	
	// Array中String类型 两个值组成一个键值对 针对dict结构 方便使用
	template<class MapType>
	bool ToMap(MapType& values) const
	{
		if (!IsArray())
			return false;
		const std::vector<RedisResult>& ar = ToArray();
		Redis::reserve(values, values.size() + ar.size() / 2);
		for (auto it = ar.begin(); it != ar.end(); it++)
		{
			typename MapType::key_type key;
			Redis::string_to(it->ToString(), key);
			it++;
			if (it != ar.end())
			{
				Redis::string_to(it->ToString(), values[key]);
			}
			else
			{
				break;
			}
		}
		return true;
	}

	boost::any v;
	bool error = false;
};

class RedisResultBind
{
public:
	void Bind(int& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			v = rst.ToInt();
		};
	}
	void Bind(long long& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			v = rst.ToLongLong();
		};
	}
	void Bind(float& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			v = rst.StringToFloat();
		};
	}
	void Bind(double& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			v = rst.StringToDouble();
		};
	}
	void Bind(std::string& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			v = rst.ToString();
		};
	}
	template<class ListType>
	void BindList(ListType& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			rst.ToArray(v);
		};
	}
	template<class MapType>
	void BindMap(MapType& v) // 一般针对HGetAll命令
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			rst.ToMap(v);
		};
	}
	template<class MapType>
	void BindMapList(std::vector<MapType>& v) // 一般针对多个HGetAll命令
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			if (!rst.IsArray())
				return;
			const std::vector<RedisResult>& ar = rst.ToArray();
			Redis::reserve(v, v.size() + ar.size());
			for (auto it = ar.begin(); it != ar.end(); it++)
			{
				// 直接加入 保存和命令个数对称
				v.insert(v.end(), MapType());
				MapType& v2 = v.back();
				it->ToMap(v2);
			}
		};
	}
	template<class ListType>
	void ScanBindList(int& cursor, ListType& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			if (!rst.IsArray())
				return;
			const RedisResult::Array& ar = rst.ToArray();
			if (ar.size() != 2)
				return;
			cursor = ar[0].ToInt();
			ar[1].ToArray(v);
		};
	}
	template<class MapType>
	void ScanBindMap(int& cursor, MapType& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			if (!rst.IsArray())
				return;
			const RedisResult::Array& ar = rst.ToArray();
			if (ar.size() != 2)
				return;
			cursor = ar[0].ToInt();
			ar[1].ToMap(v);
		};
	}

	// ObjType需要支持 From(const RedisResult& rst)
	template<class ObjType>
	void BindObj(ObjType& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			v.From(rst);
		};
	}
	template<class ObjListType>
	void BindObjList(ObjListType& v)
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			if (!rst.IsArray())
				return;
			const RedisResult::Array& ar = rst.ToArray();
			Redis::reserve(v, v.size() + ar.size());
			for (const auto& it : ar)
			{
				typename ObjListType::value_type itv;
				itv.From(it);
				v.insert(v.end(), itv);
			}
		};
	}
	template<class ObjMapType>
	void BindObjMap(ObjMapType& v) // 一般针对HGetAll命令
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			if (!rst.IsArray())
				return;
			const std::vector<RedisResult>& ar = rst.ToArray();
			for (auto it = ar.begin(); it != ar.end(); it++)
			{
				typename ObjMapType::key_type key;
				Redis::string_to(it->ToString(), key);
				it++;
				if (it != ar.end())
				{
					v[key].From(*it);
				}
				else
				{
					break;
				}
			}
		};
	}
	template<class ObjMapType>
	void BindObjMapList(std::vector<ObjMapType>& v) // 一般针对多个HGetAll命令
	{
		callback = [&](const RedisResult& rst)
		{
			if (rst.IsError())
				return;
			if (!rst.IsArray())
				return;
			const std::vector<RedisResult>& ar = rst.ToArray();
			Redis::reserve(v, v.size() + ar.size());
			for (auto it = ar.begin(); it != ar.end(); it++)
			{
				// 先加入 保存和命令个数对称
				v.insert(v.end(), ObjMapType());
				ObjMapType& v2 = v.back();

				if (!it->IsArray())
				{
					continue;
				}
				const std::vector<RedisResult>& ar2 = it->ToArray();
				for (auto it2 = ar2.begin(); it2 != ar2.end(); it2++)
				{
					typename ObjMapType::key_type key;
					Redis::string_to(it2->ToString(), key);
					it2++;
					if (it2 != ar2.end())
					{
						v2[key].From(*it2);
					}
					else
					{
						break;
					}
				}
			}
		};
	}

	std::function<void(const RedisResult&)> callback;

	static RedisResultBind& Empty()
	{
		static RedisResultBind empty;
		return empty;
	}
};

struct RedisScript
{
	RedisScript(const std::string& s)
		: script(s)
	{
	}

	const std::string script;
	// 考虑多线程安全 使用数组，外部不需要访问
	char scriptsha1[65] = {0};
};

#endif