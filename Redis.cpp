#include <stdio.h>
#include <stdarg.h>
#include "Redis.h"

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

void RedisResult::Clear()
{
	v.clear();
	error = false;
}

Redis::Redis()
{
}

Redis::~Redis()
{

}

void Redis::FormatCommand(const std::vector<std::string>& buff, std::stringstream &cmdbuff)
{
	cmdbuff << "*" << buff.size() << "\r\n";
	for (auto it = buff.begin(); it != buff.end(); ++it)
	{
		cmdbuff << "$" << it->size() << "\r\n" << *it << "\r\n";
	}
}

int Redis::ReadReply(RedisResult& rst)
{
	char* buff = NULL;
	int len = ReadToCRLF(&buff,1);
	if (len <= 0)
	{
		return len;
	}

	len -= 2; // 后面的\r\n

	char type = buff[0];
	switch (type)
	{
		case '+':
		{
			rst.v = std::string(&buff[1], &buff[len]);
			break;
		}
		case '-':
		{
			rst.v = std::string(&buff[1], &buff[len]);
			rst.error = true;
			break;
		}
		case ':':
		{
			buff[len] = '\0'; // 修改
			rst.v = atoll(&buff[1]);
			buff[len] = '\r'; // 恢复
			break;
		}
		case '$':
		{
			buff[len] = '\0'; // 修改
			int strlen = atoi(&buff[1]);
			buff[len] = '\r'; // 恢复

			if (strlen < 0)
			{
				// nil
			}
			else if (strlen == 0)
			{
				rst.v = std::string("");
			}
			else
			{
				char* buff2 = NULL;
				int len = ReadToCRLF(&buff2, strlen);
				if (len <= 0)
				{
					return len;
				}

				len -= 2; // \r\n

				if (len < strlen)
				{
					return -1;
				}

				rst.v = std::string(&buff2[0], strlen);
			}
			break;
		}
		case '*':
		{
			buff[len] = '\0'; // 修改
			char* p = &buff[1];
			int size = atoi(p);
			buff[len] = '\r'; // 恢复

			rst.v = RedisResult::Array();
			RedisResult::Array* pArray = boost::any_cast<RedisResult::Array >(&rst.v);
			for (int i = 0; i < size; ++i)
			{
				RedisResult rst2;
				if (ReadReply(rst2) != 1)
				{
					return -1;
				}
				pArray->push_back(rst2);
			}
			break;
		}
		default:
		{
			return -1;
		}
	}
	
	return 1;
}
