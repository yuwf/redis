#include <stdio.h>
#include <stdarg.h>
#include "Redis.h"

Redis::Redis()
{
}

Redis::~Redis()
{

}

int Redis::ReadReply(RedisResult& rst)
{
	m_readlen = 0;
	int r = _ReadReply(rst);
	if (r == 0 && m_readlen > 0)
	{
		if (!ReadRollback(m_readlen))
		{
			return -2;
		}
	}
	return r;
}

int Redis::_ReadReply(RedisResult& rst)
{
	char* buff = NULL;
	int len = ReadToCRLF(&buff,1);
	if (len <= 0)
	{
		return len;
	}
	m_readlen += len;

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
			else
			{
				char* buff2 = NULL;
				int len = ReadToCRLF(&buff2, strlen);
				if (len <= 0)
				{
					return len;
				}
				m_readlen += len;

				len -= 2; // \r\n

				if (len < strlen)
				{
					return -2;
				}
				if (len == 0)
				{
					rst.v = std::string("");
				}
				else
				{
					rst.v = std::string(&buff2[0], strlen);
				}
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
				int r = _ReadReply(rst2);
				if ( r <= 0 )
				{
					return r;
				}
				pArray->push_back(rst2);
			}
			break;
		}
		default:
		{
			return -2;
		}
	}
	
	return 1;
}

void RedisCommand::FromString(const std::string& cmd)
{
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
}

std::string RedisCommand::ToString() const
{
	std::stringstream ss;
	for (size_t i = 0; i < buff.size(); ++i)
	{
		ss << (i == 0 ? "" : " ") << buff[i];
	}
	return std::move(ss.str());
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

bool RedisResult::IsEmptyArray() const
{
	if (v.type() == typeid(Array))
	{
		return boost::any_cast<Array>(&v)->empty();
	}
	return false;
}

int RedisResult::ToInt() const
{
	if (IsInt())
	{
		return (int)boost::any_cast<long long>(v);
	}
	if (IsString())
	{
		return StringToInt();
	}
	return 0;
}

long long RedisResult::ToLongLong() const
{
	if (IsInt())
	{
		return boost::any_cast<long long>(v);
	}
	if (IsString())
	{
		return StringToLongLong();
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
		return *boost::any_cast<Array>(&v);
	}
	static Array empty;
	return empty;
}

void RedisResult::Clear()
{
	v.clear();
	error = false;
}

int RedisResult::StringToInt() const
{
	if (IsString())
	{
		return atoi(boost::any_cast<std::string>(v).c_str());
	}
	return 0;
}

long long RedisResult::StringToLongLong() const
{
	if (IsString())
	{
		return strtoll(boost::any_cast<std::string>(v).c_str(), NULL, 10);
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
