#ifndef _REDIS_H_
#define _REDIS_H_

#include <set>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <unordered_map>
#include <sstream>
#include <boost/any.hpp>

class RedisResult;
class Redis
{
protected:
	Redis();
	virtual ~Redis();

	// 读取返回值
	// 该函数通过调用下面的两个虚函数获取数据 获取的数据要求在返回之前一直有效
	// 返回值 -1 表示网络错误或者解析错误 0 表示未读取到 1 表示读取到了
	int ReadReply(RedisResult& rst);

	// 返回值表示长度 buff表示数据地址 -1表示网络读取失败 0表示没有读取到
	// buff中包括\r\n minlen表示buff中不包括\r\n的最少长度 
	virtual int ReadToCRLF(char** buff, int minlen) = 0;

public:
	// 格式化命令
	static void FormatCommand(const std::vector<std::string>& buff, std::stringstream &cmdbuff);

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

// Redis结果值对象
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

protected:
	friend class Redis;
	friend class RedisSyncPipeline;
	boost::any v;
	bool error;
};

class RedisResultBind
{
public:
	void Bind(int& v)
	{
		callback = [&](const RedisResult& rst) { v = rst.ToInt(); };
	}
	void Bind(long long& v)
	{
		callback = [&](const RedisResult& rst) { v = rst.ToLongLong(); };
	}
	void Bind(float& v)
	{
		callback = [&](const RedisResult& rst) { v = rst.StringToFloat(); };
	}
	void Bind(double& v)
	{
		callback = [&](const RedisResult& rst) { v = rst.StringToDouble(); };
	}
	void Bind(std::string& v)
	{
		callback = [&](const RedisResult& rst) { v = rst.ToString(); };
	}
	template<class ListType>
	void BindList(ListType& v)
	{
		callback = [&](const RedisResult& rst) { rst.ToArray(v); };
	}
	template<class MapType>
	void BindMap(MapType& v) // 一般针对HGetAll命令
	{
		callback = [&](const RedisResult& rst) { rst.ToMap(v); };
	}
	template<class MapType>
	void BindMapList(std::vector<MapType>& v) // 一般针对多个HGetAll命令
	{
		callback = [&](const RedisResult& rst)
		{
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

	// ObjType需要支持 From(const RedisResult& rst)
	template<class ObjType>
	void BindObj(ObjType& v)
	{
		callback = [&](const RedisResult& rst) { v.From(rst); };
	}
	template<class ObjListType>
	void BindObjList(ObjListType& v)
	{
		callback = [&](const RedisResult& rst)
		{
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
};

#endif