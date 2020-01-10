#include <stdio.h>
#include <stdarg.h>
#include <boost/array.hpp>
#include "RedisSync.h"

#define LogError
#define LogInfo

RedisSync::RedisSync() : m_socket(m_ioservice)
{
	m_bconnected = false;
	m_recvpos = 0;
	m_port = 0;
	m_pipeline = false;
	m_pipecmdcount = 0;
	m_subscribe = false;
}

RedisSync::~RedisSync()
{

}

bool RedisSync::InitRedis(const std::string& host, unsigned short port, const std::string& auth)
{
	if(host.empty())
	{
		LogError("Redis ip is empty");
		return false;
	}

	m_host = host;
	m_port = port;
	m_auth = auth;

	m_pipeline = false;
	m_pipecmdbuff.str("");
	m_pipecmdcount = 0;

	m_subscribe = false;

	if (!Connect())
	{
		m_host.clear();
		m_port = 0;
		m_auth.clear();
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

bool RedisSync::PipelineBegin()
{
	if (m_subscribe)
	{
		LogError("SubScribe Running");
		return false;
	}

	if (!CheckConnect())
	{
		return false;
	}

	m_pipeline = true;
	m_pipecmdbuff.str("");
	m_pipecmdcount = 0;
	return true;
}

bool RedisSync::PipelineCommit()
{
	RedisResult::Array rst;
	return PipelineCommit(rst);
}

bool RedisSync::PipelineCommit(RedisResult::Array& rst)
{
	if (m_subscribe)
	{
		LogError("SubScribe Running");
		return false;
	}

	if (!m_pipeline)
	{
		LogError("Not Open Pipeline");
		return false;
	}
	
	std::string cmdbuff = move(m_pipecmdbuff.str());
	int cmdcount = m_pipecmdcount;

	m_pipeline = false;
	m_pipecmdbuff.str("");
	m_pipecmdcount = 0;

	if (cmdcount == 0)
	{
		return true;
	}

	if (!SendCommand(cmdbuff))
	{
		return false;
	}

	ClearRecvBuff();

	for (int i = 0; i < cmdcount; ++i)
	{
		rst.push_back(RedisResult());
		RedisResult& rst2 = rst.back();
		if (ReadReply(rst2) != 1)
		{
			Close();
			return false;
		}
		if (rst2.IsError())
		{
			LogError("Redis %s", rst2.ToString().c_str());
		}
	}

	return true;
}

bool RedisSync::SubScribe(const std::string& channel)
{
	if (m_pipeline)
	{
		LogError("Pipeline Running");
		return false;
	}
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
	if (m_pipeline)
	{
		LogError("Pipeline Running");
		return false;
	}
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

	if (!SendCommand(cmdbuff.str()))
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
		return -1;
	}

	boost::system::error_code ec;
	boost::asio::ip::tcp::socket::non_blocking_io non_block(!block);
	m_socket.io_control(non_block, ec);

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
	boost::asio::ip::tcp::socket::non_blocking_io non_block(false);
	m_socket.io_control(non_block, ec);

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
		boost::asio::ip::tcp::socket::non_blocking_io non_block(false);
		m_socket.io_control(non_block, ec);

		m_socket.connect(*endpoint_iterator, ec);
		if (ec)
		{
			m_socket.close(ec);
			endpoint_iterator++;
			continue;
		}
	}

	if (ec)
	{
		LogError("RedisSync Connect Fail, host=%s port=%d", m_host.c_str(), (int)m_port);
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
	
	boost::asio::write(m_socket, boost::asio::buffer(cmdbuff.str()), boost::asio::transfer_all(), ec);
	if (ec)
	{
		m_socket.close(ec);
		LogError("RedisSync Write Error, %s", ec.message().c_str());
		return false;
	}

	RedisResult rst;
	if (ReadReply(rst) != 1)
	{
		m_socket.close(ec);
		LogError("RedisSync Maybe Not Valid Redis Address, host=%s port=%d", m_host.c_str(), (int)m_port);
		return false;
	}
	if (rst.IsNull() || rst.IsError())
	{
		m_socket.close(ec);
		LogError("RedisSync Auth Error, %s", m_auth.c_str());
		return false;
	}

	LogInfo("RedisSync Connect Success, host=%s port=%d", m_host.c_str(), (int)m_port);
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

	if (m_pipeline)
	{
		// 开启了管道 命令写入管道buff
		FormatCommand(buff, m_pipecmdbuff);
		m_pipecmdcount++;
		return false;
	}
	
	std::stringstream cmdbuff;
	FormatCommand(buff, cmdbuff);

	if (!SendCommand(cmdbuff.str()))
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
	if (!CheckConnect())
	{
		return false;
	}

	boost::system::error_code ec;
	boost::asio::write(m_socket, boost::asio::buffer(cmdbuff), boost::asio::transfer_all(), ec);
	if (ec)
	{
		// 尝试连接下
		if (!Connect())
		{
			return false;
		}
		boost::asio::write(m_socket, boost::asio::buffer(cmdbuff), boost::asio::transfer_all(), ec);
		if (ec)
		{
			LogError("RedisSync Write Error, %s", ec.message().c_str());
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
		boost::system::error_code ec;
		size_t size = m_socket.read_some(boost::asio::buffer(inbuff), ec);
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

int RedisSync::Dels(int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("DEL");

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::Set(const std::string& key, const std::string& value, unsigned int ex, bool nx)
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

int RedisSync::Set(const std::string& key, int value, unsigned int ex, bool nx)
{
	return Set(key, std::to_string(value), ex, nx);
}

int RedisSync::Set(const std::string& key, float value, unsigned int ex, bool nx)
{
	return Set(key, std::to_string(value), ex, nx);
}

int RedisSync::Set(const std::string& key, double value, unsigned int ex, bool nx)
{
	return Set(key, std::to_string(value), ex, nx);
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

int RedisSync::Get(const std::string& key, int& value)
{
	std::string rst;
	int r = Get(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atoi(rst.c_str());
	return 1;
}

int RedisSync::Get(const std::string& key, float& value)
{
	std::string rst;
	int r = Get(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::Get(const std::string& key, double& value)
{
	std::string rst;
	int r = Get(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
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

int RedisSync::MSet(const std::map<std::string, int>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
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

int RedisSync::MSet(const std::map<std::string, float>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
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

int RedisSync::MSet(const std::map<std::string, double>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
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

int RedisSync::MSet(int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("MSET");

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::HSet(const std::string& key, const std::string& field, int value)
{
	return HSet(key, field, std::to_string(value));
}

int RedisSync::HSet(const std::string& key, const std::string& field, float value)
{
	return HSet(key, field, std::to_string(value));
}

int RedisSync::HSet(const std::string& key, const std::string& field, double value)
{
	return HSet(key, field, std::to_string(value));
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

int RedisSync::HGet(const std::string& key, const std::string& field, int& value)
{
	std::string rst;
	int r = HGet(key, field, rst);
	if (r != 1)
	{
		return r;
	}
	value = atoi(rst.c_str());
	return 1;
}

int RedisSync::HGet(const std::string& key, const std::string& field, float& value)
{
	std::string rst;
	int r = HGet(key, field, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::HGet(const std::string& key, const std::string& field, double& value)
{
	std::string rst;
	int r = HGet(key, field, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
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

int RedisSync::HMSet(const std::string& key, const std::map<std::string, int>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("HMSET");
	buff.push_back(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
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

int RedisSync::HMSet(const std::string& key, const std::map<std::string, float>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("HMSET");
	buff.push_back(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
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

int RedisSync::HMSet(const std::string& key, const std::map<std::string, double>& kvs)
{
	std::vector<std::string> buff;
	buff.push_back("HMSET");
	buff.push_back(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		buff.push_back(it->first);
		buff.push_back(std::to_string(it->second));
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

int RedisSync::HMSet(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("HMSET");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::HDels(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("HDEL");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::LPush(const std::string& key, int value)
{
	return LPush(key, std::to_string(value));
}

int RedisSync::LPush(const std::string& key, float value)
{
	return LPush(key, std::to_string(value));
}

int RedisSync::LPush(const std::string& key, double value)
{
	return LPush(key, std::to_string(value));
}

int RedisSync::LPushs(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("LPUSH");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::RPush(const std::string& key, int value)
{
	return RPush(key, std::to_string(value));
}

int RedisSync::RPush(const std::string& key, float value)
{
	return RPush(key, std::to_string(value));
}

int RedisSync::RPush(const std::string& key, double value)
{
	return RPush(key, std::to_string(value));
}

int RedisSync::RPushs(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("RPUSH");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::LPop(const std::string& key, int& value)
{
	std::string rst;
	int r = LPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atoi(rst.c_str());
	return 1;
}

int RedisSync::LPop(const std::string& key, float& value)
{
	std::string rst;
	int r = LPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::LPop(const std::string& key, double& value)
{
	std::string rst;
	int r = LPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
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

int RedisSync::RPop(const std::string& key, int& value)
{
	std::string rst;
	int r = RPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atoi(rst.c_str());
	return 1;
}

int RedisSync::RPop(const std::string& key, float& value)
{
	std::string rst;
	int r = RPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = (float)atof(rst.c_str());
	return 1;
}

int RedisSync::RPop(const std::string& key, double& value)
{
	std::string rst;
	int r = RPop(key, rst);
	if (r != 1)
	{
		return r;
	}
	value = atof(rst.c_str());
	return 1;
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

int RedisSync::SAdd(const std::string& key, int value)
{
	return SAdd(key, std::to_string(value));
}

int RedisSync::SAdd(const std::string& key, float value)
{
	return SAdd(key, std::to_string(value));
}

int RedisSync::SAdd(const std::string& key, double value)
{
	return SAdd(key, std::to_string(value));
}

int RedisSync::SAdds(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("SADD");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::SRem(const std::string& key, int value)
{
	return SRem(key, std::to_string(value));
}

int RedisSync::SRem(const std::string& key, float value)
{
	return SRem(key, std::to_string(value));
}

int RedisSync::SRem(const std::string& key, double value)
{
	return SRem(key, std::to_string(value));
}

int RedisSync::SRems(const std::string& key, int cnt, ...)
{
	std::vector<std::string> buff;
	buff.push_back("SREM");
	buff.push_back(key);

	va_list ap;
	va_start(ap, cnt);
	for (int i = 0; i < cnt; ++i)
	{
		buff.push_back(va_arg(ap, const char*));
	}
	va_end(ap);

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

int RedisSync::Scard(const std::string& key)
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