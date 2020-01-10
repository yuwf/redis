#ifndef _REDIS_H_
#define _REDIS_H_

#include <string>
#include <vector>
#include <sstream>
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
	friend class Redis;
	boost::any v;
	bool error;
};

class Redis
{
protected:
	Redis();
	virtual ~Redis();

	// 格式化命令
	void FormatCommand(const std::vector<std::string>& buff, std::stringstream &cmdbuff);

	// 读取返回值
	// 该函数通过调用下面的两个虚函数获取数据 获取的数据要求在返回之前一直有效
	// 返回值 -1 表示网络错误或者解析错误 0 表示未读取到 1 表示读取到了
	int ReadReply(RedisResult& rst);

	// 返回值表示长度 buff表示数据地址 -1表示网络读取失败 0表示没有读取到
	// buff中包括\r\n minlen表示buff中不包括\r\n的最少长度 
	virtual int ReadToCRLF(char** buff, int minlen) = 0;

private:
	// 禁止拷贝
	Redis(const Redis&) = delete;
	Redis& operator=(const Redis&) = delete;
};

#endif