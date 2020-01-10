#ifndef _REDISASYNC_H_
#define _REDISASYNC_H_


#include "Redis.h"

// 异步调用  待实现 yuwf

class RedisAsync : public Redis
{
public:
	RedisAsync();
	virtual ~RedisAsync();


protected:
	
	// 返回值表示长度 buff表示数据地址 -1表示网络读取失败 0表示没有读取到
	// buff中包括\r\n minlen表示buff中不包括\r\n的最少长度 
	virtual int ReadToCRLF(char** buff, int mindatalen) override { return 0; };

private:
	// 禁止拷贝
	RedisAsync(const RedisAsync&) = delete;
	RedisAsync& operator=(const RedisAsync&) = delete;

};

#endif