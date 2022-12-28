#include "Redis.h"

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
			rst = RedisResult::String(&buff[1], &buff[len]);
			break;
		}
		case '-':
		{
			rst = RedisResult::String(&buff[1], &buff[len]);
			rst.SetError(true);
			break;
		}
		case ':':
		{
			buff[len] = '\0'; // 修改
			rst = atoll(&buff[1]);
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
					rst = RedisResult::String();
				}
				else
				{
					rst = RedisResult::String(&buff2[0], strlen);
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

			RedisResult::Array ar;
			for (int i = 0; i < size; ++i)
			{
				RedisResult rst2;
				int r = _ReadReply(rst2);
				if ( r <= 0 )
				{
					return r;
				}
				ar.emplace_back(std::move(rst2));
			}
			rst = std::move(ar);
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
			if (temp[i] == '\'' && temp[i - 1] != '\\')
			{
				temp[i] = '\0';
				protect = 0;
			}
			continue;
		}
		if (protect == 2)
		{
			if (temp[i] == '\"' && temp[i - 1] != '\\')
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
			if (!(i > 0 && temp[i - 1] == '\\'))
			{
				protect = 1; // 进入单引号
				temp[i] = '\0';
			}
		}
		if (temp[i] == '\"' )
		{
			if (!(i > 0 && temp[i - 1] == '\\'))
			{
				protect = 2; // 进入双引号
				temp[i] = '\0';
			}
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
