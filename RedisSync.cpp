#include <stdio.h>
#include <stdarg.h>
#include <boost/array.hpp>
#include "RedisSync.h"
#include "SpeedTest.h"

#include "LLog.h"
#define LogError LLOG_ERROR
#define LogInfo LLOG_INFO

struct _IOCostTest
{
	int64_t begin = TSC();

	// CPU频率值
	int64_t elapsed() const
	{
		return TSC() - begin;
	}
	//微秒
	int64_t elapsed_micro() const
	{
		return (TSC() - begin) / TSCPerUS();
	}
};

RedisSync::RedisSync() : m_socket(m_ioservice)
{
}

RedisSync::~RedisSync()
{

}

bool RedisSync::InitRedis(const std::string& host, unsigned short port, const std::string& auth, int index)
{
	if(host.empty())
	{
		LogError("Redis ip is empty");
		return false;
	}

	m_host = host;
	m_port = port;
	m_auth = auth;
	m_index = index;

	m_subscribe = false;

	if (!Connect())
	{
		m_host.clear();
		m_port = 0;
		m_auth.clear();
		m_index = 0;
		return false;
	}
	return true;
}

void RedisSync::Close()
{
	boost::system::error_code ec;
	if (m_socket.is_open())
	{
		m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
		m_socket.close(ec);
	}
	m_bconnected = false;
	ClearRecvBuff();
	m_subscribe = false; // 断开连接订阅肯定就不能用了
}

bool RedisSync::Command(const std::string& cmd, RedisResult& rst)
{
	if (cmd.empty())
	{
		return false;
	}
	std::string temp = cmd;
	int protect = 0;	// 1 单引号保护  2 双引号保护
	int size = (int)temp.size();
	for (int i = 0; i < size; ++i)
	{
		if (protect==1)
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

	std::vector<std::string> buff;
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
	
	if (!DoCommand(buff, rst))
	{
		return false;
	}
	return true;
}

bool RedisSync::Command(RedisResult& rst, const char* cmd, ...)
{
	char buff[1024] = { 0 };
	va_list ap;
	va_start(ap, cmd);
	vsnprintf(buff, 1024 - 1, cmd, ap);
	va_end(ap);

	return Command(buff, rst);
}

bool RedisSync::SubScribe(const std::string& channel)
{
	if (m_subscribe)
	{
		LogError("SubScribe Running, Only Open Once SubScribe");
		return false;
	}
	std::vector<std::string> buff;
	buff.push_back("SUBSCRIBE");
	buff.push_back(channel);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return false;
	}
	// 判断是否监听成功
	if (!rst.IsArray())
	{
		LogError("UnKnown Error");
		return false;
	}
	auto rstarray = rst.ToArray();
	if (rstarray.size() < 3 || !rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsInt())
	{
		LogError("UnKnown Error");
		return false;
	}
	if (rstarray[0].ToString() != "subscribe" || rstarray[1].ToString() != channel)
	{
		LogError("UnKnown Error");
		return false;
	}

	m_subscribe = true;
	
	ResetRecvBuff();

	return true;
}

bool RedisSync::SubScribes(int cnt, ...)
{
	if (m_subscribe)
	{
		LogError("SubScribe Running, Only Open Once SubScribe");
		return false;
	}
	if ( cnt <= 0 )
	{
		LogError("SubScribe channel count must > 0");
		return false;
	}
	std::vector<std::string> buff;
	buff.push_back("SUBSCRIBE");

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

	// 命令写入buff
	std::stringstream cmdbuff;
	FormatCommand(buff, cmdbuff);

	if (!SendCommandAndCheckConnect(cmdbuff.str()))
	{
		return false;
	}

	ClearRecvBuff();

	for (int i = 0; i < cnt; ++i)
	{
		RedisResult rst;
		if (ReadReply(rst) != 1)
		{
			Close();
			return false;
		}
		// 判断是否监听成功
		if (!rst.IsArray())
		{
			LogError("UnKnown Error");
			return false;
		}
		auto rstarray = rst.ToArray();
		if (rstarray.size() < 3 || !rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsInt())
		{
			LogError("UnKnown Error");
			return false;
		}
		if (rstarray[0].ToString() != "subscribe" || rstarray[1].ToString() != buff[i+1])
		{
			LogError("UnKnown Error");
			return false;
		}
	}
	
	m_subscribe = true;

	ResetRecvBuff();

	return true;
}

int RedisSync::Message(std::string& channel, std::string& msg, bool block)
{
	if (!m_subscribe)
	{
		LogError("SubScribe Not Call");
		return -2;
	}

	boost::system::error_code ec;
	bool non_block(!block);
	m_socket.non_blocking(non_block, ec);

	RedisResult value;
	int rst = ReadReply(value);
	if ( rst == -1 )
	{
		Close();
		return -1;
	}
	else if ( rst == 0 )
	{
		return 0;
	}

	// 判断数据类型
	if (!value.IsArray())
	{
		LogError("UnKnown Error");
		return -1;
	}
	auto rstarray = value.ToArray();
	if (rstarray.size() < 3 || !rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsString())
	{
		LogError("UnKnown Error");
		return -1;
	}
	if (rstarray[0].ToString() != "message")
	{
		LogError("UnKnown Error");
		return -1;
	}
	channel = rstarray[1].ToString();
	msg = rstarray[2].ToString();

	if (m_recvbuff.size() > 1024)
	{
		ResetRecvBuff();
	}

	return 1;
}

bool RedisSync::UnSubScribe()
{
	if (!m_subscribe)
	{
		LogError("SubScribe Not Call");
		return false;
	}

	boost::system::error_code ec;
	bool non_block(false);
	m_socket.non_blocking(non_block, ec);

	std::vector<std::string> buff;
	buff.push_back("UNSUBSCRIBE");

	// 命令写入buff
	std::stringstream cmdbuff;
	FormatCommand(buff, cmdbuff);

	if (!SendCommand(cmdbuff.str()))
	{
		return false;
	}

	do 
	{
		RedisResult value;
		if (ReadReply(value) != 1)
		{
			Close();
			return false;
		}

		// 判断数据类型
		if (!value.IsArray())
		{
			LogError("UnKnown Error");
			return false;
		}
		auto rstarray = value.ToArray();
		if (rstarray.size() < 3 || !rstarray[0].IsString() || !rstarray[2].IsInt())
		{
			LogError("UnKnown Error");
			return false;
		}

		if (rstarray[0].ToString() == "message")
		{
			// 未处理的订阅消息
			continue;
		}
		else if (rstarray[0].ToString() == "unsubscribe")
		{
			if (rstarray[2].ToInt() == 0)
			{
				break;
			}
		}
		else
		{
			// 什么情况
			LogError("UnKnown Error");
			return false;
		}
	} while (true);

	m_subscribe = false;
	return true;
}

bool RedisSync::Connect()
{
	Close();

	// 支持域名
	boost::asio::ip::tcp::resolver nresolver(m_ioservice);
	boost::asio::ip::tcp::resolver::query nquery(m_host, std::to_string(m_port));
	boost::asio::ip::tcp::resolver::iterator endpoint_iterator = nresolver.resolve(nquery);
	boost::asio::ip::tcp::resolver::iterator end_iterator;
	boost::system::error_code ec = boost::asio::error::host_not_found;
	while (ec && endpoint_iterator != end_iterator)
	{
		m_socket.open(endpoint_iterator->endpoint().protocol(), ec);
		if (ec)
		{
			endpoint_iterator++;
			continue;
		}
		
		//int nRet = setsockopt(m_socket.native(), SOL_SOCKET, SO_CONNECT_TIME, (const char*)&timeout, sizeof(timeout));

		m_socket.set_option(boost::asio::ip::tcp::no_delay(true), ec);
		m_socket.set_option(boost::asio::socket_base::keep_alive(true), ec);
		bool non_block(false);
		m_socket.non_blocking(non_block, ec);

		m_socket.connect(*endpoint_iterator, ec);
		if (ec)
		{
			boost::system::error_code ec2;
			m_socket.close(ec2);
			endpoint_iterator++;
			continue;
		}
	}

	if (ec)
	{
		LogError("RedisSync Connect Fail, host=%s port=%d, %s", m_host.c_str(), (int)m_port, ec.message().c_str());
		return false;
	}

	// 密码或者测试连接
	std::vector<std::string> buff;
	if (!m_auth.empty())
	{
		buff.push_back("AUTH");
		buff.push_back(m_auth);
	}
	else
	{
		buff.push_back("PING");
	}

	std::stringstream cmdbuff;
	FormatCommand(buff, cmdbuff);
	
	if (!SendCommand(cmdbuff.str()))
	{
		m_socket.close(ec);
		return false;
	}

	RedisResult rst;
	if (ReadReply(rst) != 1)
	{
		LogError("RedisSync Maybe Not Valid Redis Address, host=%s port=%d", m_host.c_str(), (int)m_port);
		m_socket.close(ec);
		return false;
	}
	if (rst.IsNull() || rst.IsError())
	{
		LogError("RedisSync Auth Error, %s", m_auth.c_str());
		m_socket.close(ec);
		return false;
	}

	// 选择数据库
	if (m_index != 0)
	{
		std::vector<std::string> buff;
		buff.push_back("SELECT");
		buff.push_back(std::to_string(m_index));

		std::stringstream cmdbuff;
		FormatCommand(buff, cmdbuff);

		if (!SendCommand(cmdbuff.str()))
		{
			m_socket.close(ec);
			return false;
		}

		RedisResult rst;
		if (ReadReply(rst) != 1)
		{
			LogError("RedisSync Maybe Not Valid Redis Address, host=%s port=%d", m_host.c_str(), (int)m_port);
			m_socket.close(ec);
			return false;
		}
		if (rst.IsNull() || rst.IsError())
		{
			LogError("RedisSync Select Error, %d", m_index);
			m_socket.close(ec);
			return false;
		}
	}

	LogInfo("RedisSync Connect Success, host=%s port=%d index=%d", m_host.c_str(), (int)m_port, m_index);
	m_bconnected = true;
	
	ClearRecvBuff();

	return true;
}

bool RedisSync::CheckConnect()
{
	// 如果没有连接先尝试连接下
	if (!m_bconnected)
	{
		// 如果ip不为空 重新连接下
		if (!m_host.empty())
		{
			if (!Connect())
			{
				return false;
			}
		}
		else
		{
			LogError("Redis Not Connect");
			return false;
		}
	}
	return true;
}

bool RedisSync::DoCommand(const std::vector<std::string>& buff, RedisResult& rst)
{
	if (m_subscribe)
	{
		LogError("SubScribe Running");
		return false;
	}

	m_ops++;
	
	std::stringstream cmdbuff;
	FormatCommand(buff, cmdbuff);

	if (!SendCommandAndCheckConnect(cmdbuff.str()))
	{
		return false;
	}

	ClearRecvBuff();

	if (ReadReply(rst) != 1)
	{
		Close();
		return false;
	}
	if (rst.IsError())
	{
		LogError("Redis %s", rst.ToString().c_str());
	}
	return true;
}

bool RedisSync::SendCommand(const std::string& cmdbuff)
{
	_IOCostTest _iotest;
	boost::system::error_code ec;
	boost::asio::write(m_socket, boost::asio::buffer(cmdbuff), boost::asio::transfer_all(), ec);
	m_sendbytes += cmdbuff.size();
	m_sendcost += _iotest.elapsed_micro();
	m_sendcosttsc += _iotest.elapsed();
	if (ec)
	{
		LogError("RedisSync Write Error, %s", ec.message().c_str());
		return false;
	}
	return true;
}

bool RedisSync::SendCommandAndCheckConnect(const std::string& cmdbuff)
{
	if (!CheckConnect())
	{
		return false;
	}
	if (!SendCommand(cmdbuff))
	{
		// 尝试连接下
		if (!Connect())
		{
			return false;
		}
		if (!SendCommand(cmdbuff))
		{
			return false;
		}
	}
	return true;
}

int RedisSync::ReadToCRLF(char** buff, int mindatalen)
{
	do
	{
		int pos = m_recvpos + mindatalen;
		if ((int)m_recvbuff.size() >= pos + 2)
		{
			for (int i = pos; i < (int)m_recvbuff.size() - 1; ++i)
			{
				if (memcmp(&m_recvbuff[i], "\r\n", 2) == 0)
				{
					*buff = &m_recvbuff[m_recvpos];
					int size = i + 2 - m_recvpos;
					m_recvpos += size;
					return size;
				}
			}
		}

		boost::array<char, 512> inbuff;
		_IOCostTest _iotest;
		boost::system::error_code ec;
		size_t size = m_socket.read_some(boost::asio::buffer(inbuff), ec);
		m_recvcost += _iotest.elapsed_micro();
		m_recvcosttsc += _iotest.elapsed();
		if (ec)
		{
			if (ec == boost::asio::error::would_block)
			{
				// 非阻塞socket
				return 0;
			}
			LogError("RedisSync Read Error, %s", ec.message().c_str());
			return -1;
		}
		if (size > 0)
		{
			m_recvbuff.insert(m_recvbuff.end(), inbuff.begin(), inbuff.begin() + size);
			m_recvbytes += size;
			continue;
		}
		break;
	} while (true);

	return 0;
}

void RedisSync::ClearRecvBuff()
{
	m_recvbuff.clear();
	m_recvpos = 0;
}

void RedisSync::ResetRecvBuff()
{
	if ((int)m_recvbuff.size() > m_recvpos && m_recvpos != 0)
	{
		std::vector<char> buff = std::vector<char>(m_recvbuff.begin() + m_recvpos, m_recvbuff.end());
		m_recvbuff.swap(buff);
	}
	else
	{
		m_recvbuff.clear();
	}
	m_recvpos = 0;
}

int RedisSync::Del(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Del(const std::vector<std::string>& key)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");

	for (auto it = key.begin(); it != key.end(); ++it)
	{
		buff.push_back(*it);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Exists(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("EXISTS");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Expire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::ExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::PExpire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::PExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::TTL(const std::string& key, long long& value)
{
	std::vector<std::string> buff;
	buff.push_back("TTL");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		value = rst.ToLongLong();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::PTTL(const std::string& key, long long& value)
{
	std::vector<std::string> buff;
	buff.push_back("PTTL");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		value = rst.ToLongLong();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Set(const std::string& key, const std::string& value, unsigned int ex, unsigned int px, bool nx)
{
	std::vector<std::string> buff;
	buff.push_back("SET");
	buff.push_back(key);
	buff.push_back(value);
	if (ex != -1)
	{
		buff.push_back("EX");
		buff.push_back(std::to_string(ex));
	}
	else if (px != -1)
	{
		buff.push_back("PX");
		buff.push_back(std::to_string(px));
	}
	if (nx)
	{
		buff.push_back("NX");
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::Get(const std::string& key, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("GET");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::MSet(const std::map<std::string, std::string>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(it->second);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::MGet(const std::vector<std::string>& keys, RedisResult& rst)
{
	if (keys.empty())
	{
		return 0;
	}

	std::vector<std::string> buff;
	buff.push_back("MGET");

	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		buff.push_back(*it);
	}

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Incr(const std::string& key, long long& svalue)
{
	std::vector<std::string> buff;
	buff.push_back("INCR");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsInt())
	{
		svalue = rst.ToLongLong();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Incr(const std::string& key, int& svalue)
{
	long long svalue2 = 0;
	int rst = Incr(key, svalue2);
	if (rst == 1)
	{
		svalue = int(svalue2);
	}
	return rst;
}

int RedisSync::Incr(const std::string& key)
{
	long long svalue = 0;
	return Incr(key, svalue);
}

int RedisSync::Incrby(const std::string& key, int value, long long& svalue)
{
	std::vector<std::string> buff;
	buff.push_back("INCRBY");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsInt())
	{
		svalue = rst.ToLongLong();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Incrby(const std::string& key, int value, int& svalue)
{
	long long svalue2 = 0;
	int rst = Incrby(key, value, svalue2);
	if (rst == 1)
	{
		svalue = int(svalue2);
	}
	return rst;
}

int RedisSync::Incrby(const std::string& key, int value)
{
	long long svalue = 0;
	return Incrby(key, value, svalue);
}

int RedisSync::HSet(const std::string& key, const std::string& field, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("HSET");
	buff.push_back(key);
	buff.push_back(field);
	buff.push_back(value);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsInt())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HGet(const std::string& key, const std::string& field, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("HGET");
	buff.push_back(key);
	buff.push_back(field);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HMSet(const std::string& key, const std::map<std::string, std::string>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("HMSET");
	buff.push_back(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(it->second);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	return 1;
}

int RedisSync::HMGet(const std::string& key, const std::vector<std::string>& fields, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("HMGET");
	buff.push_back(key);

	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		buff.push_back(*it);
	}

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HKeys(const std::string& key, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("HKEYS");
	buff.push_back(key);

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HVals(const std::string& key, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("HVALS");
	buff.push_back(key);

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HGetAll(const std::string& key, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("HGETALL");
	buff.push_back(key);

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HIncrby(const std::string& key, const std::string& field, int value, long long& svalue)
{
	std::vector<std::string> buff;
	buff.push_back("HINCRBY");
	buff.push_back(key);
	buff.push_back(field);
	buff.push_back(std::to_string(value));

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsInt())
	{
		svalue = rst.ToLongLong();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HIncrby(const std::string& key, const std::string& field, int value, int& svalue)
{
	long long svalue2 = 0;
	int rst = HIncrby(key, field, value, svalue2);
	if (rst == 1)
	{
		svalue = int(svalue2);
	}
	return rst;
}

int RedisSync::HIncrby(const std::string& key, const std::string& field, int value)
{
	long long svalue = 0;
	return HIncrby(key, field, value, svalue);
}

int RedisSync::HLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HLEN");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HScan(const std::string& key, int cursor, const std::string& match, int count, int& rstcursor, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("HSCAN");
	buff.push_back(key);
	buff.push_back(std::to_string(cursor));
	if (!match.empty())
	{
		buff.push_back("MATCH");
		buff.push_back(match);
	}
	if (count > 0)
	{
		buff.push_back("COUNT");
		buff.push_back(std::to_string(count));
	}

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未获取到值
		return 0;
	}
	if (rst.IsArray() && rst.ToArray().size() == 2)
	{
		const RedisResult::Array& ar = rst.ToArray();
		rstcursor = ar[0].ToInt();
		RedisResult v = ar[1];
		rst = v;
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HExists(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HEXISTS");
	buff.push_back(key);
	buff.push_back(field);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HDel(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);
	buff.push_back(field);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::HDels(const std::string& key, const std::vector<std::string>& fields)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);

	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		buff.push_back(*it);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LPushs(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);

	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::RPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::RPushs(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);

	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LPop(const std::string& key)
{
	std::string rst;
	return LPop(key, rst);
}

int RedisSync::LPop(const std::string& key, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LPOP");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsNull())
	{
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::RPop(const std::string& key)
{
	std::string rst;
	return RPop(key, rst);
}

int RedisSync::RPop(const std::string& key, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("RPOP");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsNull())
	{
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LRange(const std::string& key, int start, int stop, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("LRANGE");
	buff.push_back(key);
	buff.push_back(std::to_string(start));
	buff.push_back(std::to_string(stop));

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsNull())
	{
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LRem(const std::string& key, int count, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LREM");
	buff.push_back(key);
	buff.push_back(std::to_string(count));
	buff.push_back(value);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LTrim(const std::string& key, int start, int stop)
{
	std::vector<std::string> buff;
	buff.push_back("LTRIM");
	buff.push_back(key);
	buff.push_back(std::to_string(start));
	buff.push_back(std::to_string(stop));

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		// 命令错误
		return 0;
	}
	if (rst.IsNull())
	{
		// 未设置成功
		return 0;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::LLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("LLEN");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::SAdd(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::SAdds(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);

	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::SRem(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::SRems(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);

	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::SMembers(const std::string& key, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("SMEMBERS");
	buff.push_back(key);

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsNull())
	{
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::SISMember(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SISMEMBER");
	buff.push_back(key);
	buff.push_back(value);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::SCard(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("SCARD");
	buff.push_back(key);

	RedisResult rst;
	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	LogError("UnKnown Error");
	return 0;
}

int RedisSync::Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	RedisResult rst;
	return Eval(script, keys, args, rst);
}

int RedisSync::Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst)
{
	std::vector<std::string> buff;
	buff.push_back("EVAL");
	buff.push_back(script);
	buff.push_back(std::to_string(keys.size()));

	for (const auto& it : keys)
	{
		buff.push_back(it);
	}
	for (const auto& it : args)
	{
		buff.push_back(it);
	}

	if (!DoCommand(buff, rst))
	{
		return -1;
	}
	if (rst.IsError())
	{
		return 0;
	}
	return 1;
}

RedisResultBind& RedisSyncPipeline::Command(const std::string& cmd)
{
	if (cmd.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
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

	std::vector<std::string> buff;
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

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Del(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Del(const std::vector<std::string>& key)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");
	for (auto it = key.begin(); it != key.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Exists(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("EXISTS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Expire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::ExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("EXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PExpire(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIRE");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PExpireAt(const std::string& key, long long value)
{
	std::vector<std::string> buff;
	buff.push_back("PEXPIREAT");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::TTL(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("TTL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::PTTL(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("PTTL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Set(const std::string& key, const std::string& value, unsigned int ex, unsigned int px, bool nx)
{
	std::vector<std::string> buff;
	buff.push_back("SET");
	buff.push_back(key);
	buff.push_back(value);
	if (ex != -1)
	{
		buff.push_back("EX");
		buff.push_back(std::to_string(ex));
	}
	else if (px != -1)
	{
		buff.push_back("PX");
		buff.push_back(std::to_string(px));
	}
	if (nx)
	{
		buff.push_back("NX");
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}


RedisResultBind& RedisSyncPipeline::Get(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("GET");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MSet(const std::map<std::string, std::string>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(it->second);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MGet(const std::vector<std::string>& keys)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	std::vector<std::string> buff;
	buff.push_back("MGET");
	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}


RedisResultBind& RedisSyncPipeline::Incr(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("INCR");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Incrby(const std::string& key, int value)
{
	std::vector<std::string> buff;
	buff.push_back("INCRBY");
	buff.push_back(key);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HSet(const std::string& key, const std::string& field, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("HSET");
	buff.push_back(key);
	buff.push_back(field);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HGet(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HGET");
	buff.push_back(key);
	buff.push_back(field);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MHGet(const std::vector<std::string>& keys, const std::string& field)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}
	
	std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
	bindptr->m_begin = m_binds.size() - 1;
	bindptr->m_end = bindptr->m_begin + keys.size();

	for (auto keyit = keys.begin(); keyit != keys.end(); ++keyit)
	{
		std::vector<std::string> buff;
		buff.push_back("HGET");
		buff.push_back(*keyit);
		buff.push_back(field);
		Redis::FormatCommand(buff, m_cmdbuff);

		m_binds.push_back(RedisResultBind());
		m_binds.back().callback = [&, bindptr](const RedisResult& rst)
		{
			bindptr->AddResult(rst);
		};
		m_binds2[m_binds.size() - 1] = bindptr;
	}

	return bindptr->m_bind;
}

RedisResultBind& RedisSyncPipeline::HMSet(const std::string& key, const std::map<std::string, std::string>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("HMSET");
	buff.push_back(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(it->second);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HMGet(const std::string& key, const std::vector<std::string>& fields)
{
	std::vector<std::string> buff;
	buff.push_back("HMGET");
	buff.push_back(key);
	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MHMGet(const std::vector<std::string>& keys, const std::vector<std::string>& fields)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}

	std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
	bindptr->m_begin = m_binds.size() - 1;
	bindptr->m_end = bindptr->m_begin + keys.size();

	for (auto keyit = keys.begin(); keyit != keys.end(); ++keyit)
	{
		std::vector<std::string> buff;
		buff.push_back("HMGET");
		buff.push_back(*keyit);
		for (auto it = fields.begin(); it != fields.end(); ++it)
		{
			buff.push_back(*it);
		}
		Redis::FormatCommand(buff, m_cmdbuff);

		m_binds.push_back(RedisResultBind());
		m_binds.back().callback = [&, bindptr](const RedisResult& rst)
		{
			bindptr->AddResult(rst);
		};
		m_binds2[m_binds.size() - 1] = bindptr;
	}
	
	return bindptr->m_bind;
}

RedisResultBind& RedisSyncPipeline::HKeys(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HKEYS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HVals(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HVALS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HGetAll(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HGETALL");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::MHGetAll(const std::vector<std::string>& keys)
{
	if (keys.empty())
	{
		static RedisResultBind empty;
		return empty;
	}

	std::shared_ptr<_RedisResultBind_> bindptr = std::make_shared<_RedisResultBind_>();
	bindptr->m_begin = m_binds.size() - 1;
	bindptr->m_end = bindptr->m_begin + keys.size();

	for (auto keyit = keys.begin(); keyit != keys.end(); ++keyit)
	{
		std::vector<std::string> buff;
		buff.push_back("HGETALL");
		buff.push_back(*keyit);
		
		Redis::FormatCommand(buff, m_cmdbuff);

		m_binds.push_back(RedisResultBind());
		m_binds.back().callback = [&, bindptr](const RedisResult& rst)
		{
			bindptr->AddResult(rst);
		};
		m_binds2[m_binds.size() - 1] = bindptr;
	}

	return bindptr->m_bind;
}

RedisResultBind& RedisSyncPipeline::HIncrby(const std::string& key, const std::string& field, int value)
{
	std::vector<std::string> buff;
	buff.push_back("HINCRBY");
	buff.push_back(key);
	buff.push_back(field);
	buff.push_back(std::to_string(value));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("HLEN");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HScan(const std::string& key, int cursor, const std::string& match, int count)
{
	std::vector<std::string> buff;
	buff.push_back("HSCAN");
	buff.push_back(key);
	buff.push_back(std::to_string(cursor));
	if (!match.empty())
	{
		buff.push_back("MATCH");
		buff.push_back(match);
	}
	if (count > 0)
	{
		buff.push_back("COUNT");
		buff.push_back(std::to_string(count));
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HExists(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HEXISTS");
	buff.push_back(key);
	buff.push_back(field);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HDel(const std::string& key, const std::string& field)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);
	buff.push_back(field);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::HDel(const std::string& key, const std::vector<std::string>& fields)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);
	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPushs(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPush(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPushs(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LPop(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("LPOP");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::RPop(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("RPOP");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LRange(const std::string& key, int start, int stop)
{
	std::vector<std::string> buff;
	buff.push_back("LRANGE");
	buff.push_back(key);
	buff.push_back(std::to_string(start));
	buff.push_back(std::to_string(stop));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LRem(const std::string& key, int count, std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("LREM");
	buff.push_back(key);
	buff.push_back(std::to_string(count));
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LTrim(const std::string& key, int start, int stop)
{
	std::vector<std::string> buff;
	buff.push_back("LTRIM");
	buff.push_back(key);
	buff.push_back(std::to_string(start));
	buff.push_back(std::to_string(stop));

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::LLen(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("LLEN");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SAdd(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SAdds(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SRem(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SRems(const std::string& key, const std::vector<std::string>& values)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);
	for (auto it = values.begin(); it != values.end(); ++it)
	{
		buff.push_back(*it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SMembers(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("SMEMBERS");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SISMember(const std::string& key, const std::string& value)
{
	std::vector<std::string> buff;
	buff.push_back("SISMEMBER");
	buff.push_back(key);
	buff.push_back(value);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::SCard(const std::string& key)
{
	std::vector<std::string> buff;
	buff.push_back("SCARD");
	buff.push_back(key);

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

RedisResultBind& RedisSyncPipeline::Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	std::vector<std::string> buff;
	buff.push_back("EVAL");
	buff.push_back(script);
	buff.push_back(std::to_string(keys.size()));

	for (const auto& it : keys)
	{
		buff.push_back(it);
	}
	for (const auto& it : args)
	{
		buff.push_back(it);
	}

	Redis::FormatCommand(buff, m_cmdbuff);

	m_binds.push_back(RedisResultBind());
	return m_binds.back();
}

bool RedisSyncPipeline::Do()
{
	int cmdcount = (int)m_binds.size();
	if (cmdcount == 0)
	{
		return true;
	}

	m_redis.m_ops += cmdcount;

	if (!m_redis.SendCommandAndCheckConnect(m_cmdbuff.str()))
	{
		m_cmdbuff.str("");
		m_binds.clear();
		m_binds2.clear();
		return false;
	}

	for (int i = 0; i < cmdcount; ++i)
	{
		RedisResult rst;
		if (m_redis.ReadReply(rst) != 1)
		{
			m_redis.Close();
			return false;
		}
		if (rst.IsError())
		{
			LogError("Redis %s", rst.ToString().c_str());
		}
		else
		{
			if (m_binds[i].callback)
			{
				m_binds[i].callback(rst);
			}

			// 自定义复合命令
			auto it = m_binds2.find(i);
			if (it != m_binds2.end())
			{
				if (it->second->IsEnd(i))
				{
					if (it->second->m_bind.callback)
					{
						it->second->m_bind.callback(it->second->m_result);
					}
				}
			}
		}
	}
	m_cmdbuff.str("");
	m_binds.clear();
	m_binds2.clear();
	return true;
}

bool RedisSyncPipeline::Do(RedisResult::Array& rst)
{
	int cmdcount = (int)m_binds.size();
	if (cmdcount == 0)
	{
		return true;
	}

	m_redis.m_ops += cmdcount;

	if (!m_redis.SendCommandAndCheckConnect(m_cmdbuff.str()))
	{
		m_cmdbuff.str("");
		m_binds.clear();
		m_binds2.clear();
		return false;
	}

	for (int i = 0; i < cmdcount; ++i)
	{
		rst.push_back(RedisResult());
		RedisResult& rst2 = rst.back();
		if (m_redis.ReadReply(rst2) != 1)
		{
			m_redis.Close();
			return false;
		}
		if (rst2.IsError())
		{
			LogError("Redis %s", rst2.ToString().c_str());
		}
		else
		{
			if (m_binds[i].callback)
			{
				m_binds[i].callback(rst2);
			}

			// 自定义复合命令
			auto it = m_binds2.find(i);
			if (it != m_binds2.end())
			{
				rst.pop_back();
				if (it->second->IsEnd(i))
				{
					rst.push_back(it->second->m_result);
					if (it->second->m_bind.callback)
					{
						it->second->m_bind.callback(it->second->m_result);
					}
				}
			}
		}
	}
	m_cmdbuff.str("");
	m_binds.clear();
	m_binds2.clear();
	return true;
}