#include <stdio.h>
#include <stdarg.h>
#include <boost/array.hpp>
#include "RedisAsync.h"
#include "SpeedTest.h"

RedisAsync::RedisAsync(bool subscribe)
	: m_socket(m_ioservice)
	, m_subscribe(subscribe)
{
}

RedisAsync::~RedisAsync()
{

}

bool RedisAsync::InitRedis(const std::string& host, unsigned short port, const std::string& auth, int index)
{
	if (host.empty())
	{
		LogError("RedisAsync ip is empty");
		return false;
	}

	m_host = host;
	m_port = port;
	m_auth = auth;
	m_index = index;

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

void RedisAsync::Close()
{
	boost::system::error_code ec;
	if (m_socket.is_open())
	{
		m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
		m_socket.close(ec);
	}
	m_bconnected = false;
	ClearRecvBuff();

	// 等待回调的 由于命令执行不确定性 全部回调失败
	std::list<QueueCmdPtr> temp;
	temp.swap(m_queuecommands);
	for (const auto& it : temp)
	{
		if (m_globalcallback)
		{
			m_globalcallback(false, it);
		}
		else if (it->type == 0)
		{
			if (it->callback)
			{
				static RedisResult s_result;
				it->callback(false, s_result);
			}
		}
		else if (it->type == 1)
		{
			if (it->multicallback)
			{
				static std::vector<RedisResult> s_result;
				it->multicallback(false, s_result);
			}
		}
	}
}

bool RedisAsync::Command(const std::string& str, const CallBack& callback)
{
	if (str.empty())
	{
		return false;
	}

	RedisCommand cmd;
	cmd.FromString(str);

	return SendCommand(cmd, callback);
}

bool RedisAsync::Command(const CallBack& callback, const char* format, ...)
{
	char str[1024] = { 0 };
	va_list ap;
	va_start(ap, format);
	vsnprintf(str, 1024 - 1, format, ap);
	va_end(ap);

	RedisCommand cmd;
	cmd.FromString(str);

	return SendCommand(cmd, callback);
}

bool RedisAsync::Command(const std::vector<std::string>& strs, const MultiCallBack& callback)
{
	if (strs.empty())
	{
		return false;
	}

	std::vector<RedisCommand> cmds;
	for (const auto& it : strs)
	{
		cmds.push_back(RedisCommand());
		cmds.back().FromString(it);
	}

	return SendCommand(cmds, callback);
}

bool RedisAsync::SendCommand(const RedisCommand& cmd, const CallBack& callback)
{
	if (m_subscribe)
	{
		LogError("RedisAsync is SubScribe Object");
		return false;
	}

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	QueueCmdPtr cmdptr(new QueueCmd(0));
	cmdptr->cmd.push_back(cmd);
	cmdptr->callback = callback;
	m_queuecommands.push_back(cmdptr);
	return true;
}

bool RedisAsync::SendCommand(const std::vector<RedisCommand>& cmds, const MultiCallBack& callback)
{
	if (m_subscribe)
	{
		LogError("RedisAsync is SubScribe Object");
		return false;
	}
	
	if (!SendAndCheckConnect(cmds))
	{
		return false;
	}

	QueueCmdPtr cmdptr(new QueueCmd(1));
	cmdptr->cmd = cmds;
	cmdptr->multicallback = callback;
	m_queuecommands.push_back(cmdptr);
	return true;
}

void RedisAsync::UpdateReply()
{
	if (m_subscribe)
	{
		LogError("RedisAsync is SubScribe Object");
		return;
	}
	if (!CheckConnect())
	{
		return;
	}

	do 
	{
		if (m_queuecommands.empty())
		{
			return;
		}
		RedisResult rst;
		int r = ReadReply(rst);
		if (r < 0)
		{
			Close();
			return;
		}
		else if (r == 0)
		{
			return;
		}

		QueueCmdPtr cmdptr = m_queuecommands.front(); // 这里cmdptr要copy 防止下面pop掉
		cmdptr->rst.emplace_back(rst);
		if (cmdptr->type == 0)
		{
			m_queuecommands.pop_front(); // 先移除 回调中可能会修改m_commands
			if (m_globalcallback)
			{
				m_globalcallback(!cmdptr->rst[0].IsError(), cmdptr);
			}
			else if (cmdptr->callback)
			{
				cmdptr->callback(!cmdptr->rst[0].IsError(), cmdptr->rst[0]);
			}
		}
		else if (cmdptr->type == 1 && cmdptr->cmd.size() == cmdptr->rst.size())
		{
			m_queuecommands.pop_front(); // 先移除
			if (m_globalcallback)
			{
				m_globalcallback(true, cmdptr);
			}
			else if (cmdptr->multicallback)
			{
				cmdptr->multicallback(true, cmdptr->rst);
			}
		}
		else
		{
			LogError("UnKnown Error");
			break;
		}
	} while (true);
}

bool RedisAsync::SubScribe(const std::string& channel)
{
	if (!m_subscribe)
	{
		LogError("RedisAsync not SubScribe Object");
		return false;
	}

	RedisCommand cmd;
	cmd.Add("SUBSCRIBE");
	cmd.Add(channel);

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	m_channel[channel] = SubscribeSend;
	return true;
}

bool RedisAsync::UnSubScribe(const std::string& channel)
{
	if (!m_subscribe)
	{
		LogError("RedisAsync not SubScribe Object");
		return false;
	}

	RedisCommand cmd;
	cmd.Add("UNSUBSCRIBE");
	if (!channel.empty())
	{
		cmd.Add(channel);
	}

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	if (!channel.empty())
	{
		auto it = m_channel.find(channel);
		if (it != m_channel.end())
		{
			it->second = UnSubscribeSend;
		}
	}
	else
	{
		for (auto& it : m_channel)
		{
			it.second = UnSubscribeSend;
		}
	}
	return true;
}

bool RedisAsync::PSubScribe(const std::string& pattern)
{
	if (!m_subscribe)
	{
		LogError("RedisAsync not SubScribe Object");
		return false;
	}

	RedisCommand cmd;
	cmd.Add("PSUBSCRIBE");
	cmd.Add(pattern);

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	m_pattern[pattern] = SubscribeSend;
	return true;
}

bool RedisAsync::PUnSubScribe(const std::string& pattern)
{
	if (!m_subscribe)
	{
		LogError("RedisAsync not SubScribe Object");
		return false;
	}

	RedisCommand cmd;
	cmd.Add("PUNSUBSCRIBE");
	if (!pattern.empty())
	{
		cmd.Add(pattern);
	}

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	if (!pattern.empty())
	{
		auto it = m_pattern.find(pattern);
		if (it != m_pattern.end())
		{
			it->second = UnSubscribeSend;
		}
	}
	else
	{
		for (auto& it : m_pattern)
		{
			it.second = UnSubscribeSend;
		}
	}
	return true;
}

int RedisAsync::Message(std::string& channel, std::string& msg, bool block)
{
	if (!m_subscribe)
	{
		LogError("RedisAsync not SubScribe Object");
		return -1;
	}
	if (CheckConnect())
	{
		return -1;
	}

	if (block)
	{
		boost::system::error_code ec;
		bool non_block(false);
		m_socket.non_blocking(non_block, ec);
	}

	RedisResult rst;
	int ret = -1;
	do
	{
		int r = ReadReply(rst);
		if (r < 0)
		{
			Close();
			if (CheckConnect())
			{
				continue; // 连接在读取一次
			}
			else
			{
				return -1; // 未连接成功直接退出
			}
		}
		else if (r == 0)
		{
			if (block) // 设置成了阻塞 一定要读取到数据
			{
				continue;
			}
			ret = 0;
			break;
		}

		// 判断数据类型
		if (!rst.IsArray())
		{
			LogError("UnKnown Error");
			break;
		}
		auto rstarray = rst.ToArray();
		if (rstarray[0].ToString() == "message")
		{
			if (!rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsString())
			{
				LogError("UnKnown Error");
				return -1;
			}
			channel = rstarray[1].ToString();
			msg = rstarray[2].ToString();
			ret = 1;
			break;
		}
		else if (rstarray[0].ToString() == "subscribe")
		{
			if (!rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsInt())
			{
				LogError("UnKnown Error");
				break;
			}
			std::string channel = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_channel.find(channel);
			if (it != m_channel.end())
			{
				it->second = SubscribeRecv;
				LogInfo("subscribe channel=%s", channel.c_str());
			}
			else
			{
				LogError("not find local subscribe channel, %s", channel.c_str());
			}
		}
		else if (rstarray[0].ToString() == "unsubscribe")
		{
			if (rstarray[1].IsNull()) // 若没有订阅 发全部取消，此值会返回空
			{
				continue;
			}
			if (!rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsInt())
			{
				LogError("UnKnown Error");
				break;
			}
			std::string channel = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_channel.find(channel);
			if (it != m_channel.end())
			{
				LogInfo("unsubscribe channel=%s", channel.c_str());
				m_channel.erase(it);
			}
			else
			{
				LogError("not find local subscribe channel, %s", channel.c_str());
			}
		}
		else if (rstarray[0].ToString() == "psubscribe")
		{
			if (!rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsInt())
			{
				LogError("UnKnown Error");
				break;
			}
			std::string pattern = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_pattern.find(pattern);
			if (it != m_pattern.end())
			{
				it->second = SubscribeRecv;
				LogInfo("psubscribe pattern=%s", pattern.c_str());
			}
			else
			{
				LogError("not find local subscribe pattern, %s", pattern.c_str());
			}
		}
		else if (rstarray[0].ToString() == "punsubscribe")
		{
			if (rstarray[1].IsNull()) // 若没有订阅 发全部取消，此值会返回空
			{
				continue;
			}
			if (!rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsInt())
			{
				LogError("UnKnown Error");
				break;
			}
			std::string pattern = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_pattern.find(pattern);
			if (it != m_pattern.end())
			{
				LogInfo("punsubscribe pattern=%s", pattern.c_str());
				m_pattern.erase(it);
			}
			else
			{
				LogError("not find local subscribe pattern, %s", pattern.c_str());
			}
		}
		else
		{
			LogError("UnKnown Error %s", rstarray[0].ToString().c_str());
			break;
		}

	} while (true);

	// 回复socket状态
	if (block)
	{
		boost::system::error_code ec;
		bool non_block(true);
		m_socket.non_blocking(non_block, ec);
	}

	// 收缩下接受数据的buff
	if (m_recvbuff.size() > 1024)
	{
		ResetRecvBuff();
	}

	return ret;
}

bool RedisAsync::Connect()
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

		struct timeval conntimeout;
		conntimeout.tv_sec = 8;
		conntimeout.tv_usec = 0;
		struct timeval sendtimeout;
		sendtimeout.tv_sec = 4;
		sendtimeout.tv_usec = 0;
		struct timeval recvtimeout;
		recvtimeout.tv_sec = 4;
		recvtimeout.tv_usec = 0;

		int nRet = 0;
		// 设置超时 连接超时好像一直失败 在linux下没有SO_CONNECT_TIME
		//nRet = setsockopt(m_socket.native(), SOL_SOCKET, SO_CONNECT_TIME, (const char*)&conntimeout, sizeof(conntimeout));
		nRet = setsockopt(m_socket.native(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendtimeout, sizeof(sendtimeout));
		nRet = setsockopt(m_socket.native(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvtimeout, sizeof(recvtimeout));

		m_socket.set_option(boost::asio::ip::tcp::no_delay(true), ec);
		m_socket.set_option(boost::asio::socket_base::keep_alive(true), ec);

		// 先设置阻塞 连接成功之后再设置非阻塞
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
		LogError("RedisAsync Connect Fail, host=%s port=%d, %s", m_host.c_str(), (int)m_port, ec.message().c_str());
		return false;
	}

	// 密码或者测试连接
	{
		RedisCommand cmd;
		if (!m_auth.empty())
		{
			cmd.Add("AUTH");
			cmd.Add(m_auth);
		}
		else
		{
			cmd.Add("PING");
		}

		if (!Send(cmd))
		{
			m_socket.close(ec);
			return false;
		}

		RedisResult rst;
		if (ReadReply(rst) != 1)
		{
			LogError("RedisAsync Maybe Not Valid Redis Address, host=%s port=%d", m_host.c_str(), (int)m_port);
			m_socket.close(ec);
			return false;
		}
		if (rst.IsNull() || rst.IsError())
		{
			LogError("RedisAsync Auth Error, %s", m_auth.c_str());
			m_socket.close(ec);
			return false;
		}
	}

	// 选择数据库
	if (m_index != 0)
	{
		RedisCommand cmd;
		cmd.Add("SELECT");
		cmd.Add(m_index);

		if (!Send(cmd))
		{
			m_socket.close(ec);
			return false;
		}

		RedisResult rst;
		if (ReadReply(rst) != 1)
		{
			LogError("RedisAsync Maybe Not Valid Redis Address, host=%s port=%d", m_host.c_str(), (int)m_port);
			m_socket.close(ec);
			return false;
		}
		if (rst.IsNull() || rst.IsError())
		{
			LogError("RedisAsync Select Error, %d", m_index);
			m_socket.close(ec);
			return false;
		}
	}

	ClearRecvBuff();

	LogInfo("RedisAsync Connect Success, host=%s port=%d index=%d", m_host.c_str(), (int)m_port, m_index);
	m_bconnected = true;

	// 设置为非阻塞
	bool non_block(true);
	m_socket.non_blocking(non_block, ec);

	// 重新订阅
	if (m_subscribe)
	{
		for (auto it = m_channel.begin(); it != m_channel.end(); )
		{
			if (it->second == SubscribeSend || it->second == SubscribeRecv)
			{
				SubScribe(it->first);
				++it;
			}
			else
			{
				m_channel.erase(it++);
			}
		}
		for (auto it = m_pattern.begin(); it != m_pattern.end(); )
		{
			if (it->second == SubscribeSend || it->second == SubscribeRecv)
			{
				PSubScribe(it->first);
				++it;
			}
			else
			{
				m_pattern.erase(it++);
			}
		}
	}

	return true;
}

bool RedisAsync::CheckConnect()
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
			LogError("RedisAsync Not Connect");
			return false;
		}
	}
	return true;
}

bool RedisAsync::Send(const RedisCommand& cmd)
{
	m_ops++;
	boost::asio::streambuf sendbuff;
	std::ostream ss(&sendbuff);
	cmd.ToStream(ss);

	int64_t begin = TSC();
	boost::system::error_code ec;
	std::size_t size = boost::asio::write(m_socket, sendbuff.data(), boost::asio::transfer_all(), ec);
	m_sendbytes += sendbuff.size();
	int64_t end = TSC();
	m_sendcost += (end - begin) / TSCPerUS();
	m_sendcosttsc += (end - begin);
	if (ec)
	{
		LogError("RedisAsync Write Error, %s", ec.message().c_str());
		return false;
	}
	return true;
}

bool RedisAsync::Send(const std::vector<RedisCommand>& cmds)
{
	m_ops += cmds.size();
	boost::asio::streambuf sendbuff;
	std::ostream ss(&sendbuff);
	for (const auto& it : cmds)
	{
		it.ToStream(ss);
	}

	int64_t begin = TSC();
	boost::system::error_code ec;
	boost::asio::write(m_socket, sendbuff.data(), boost::asio::transfer_all(), ec);
	m_sendbytes += sendbuff.size();
	int64_t end = TSC();
	m_sendcost += (end - begin) / TSCPerUS();
	m_sendcosttsc += (end - begin);
	if (ec)
	{
		LogError("RedisAsync Write Error, %s", ec.message().c_str());
		return false;
	}
	return true;
}

int RedisAsync::ReadToCRLF(char** buff, int mindatalen)
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
		int64_t begin = TSC();
		boost::system::error_code ec;
		size_t size = m_socket.read_some(boost::asio::buffer(inbuff), ec);
		int64_t end = TSC();
		m_recvcost += ((end - begin) / TSCPerUS());
		m_recvcosttsc += (end - begin);
		if (ec)
		{
			if (ec == boost::asio::error::would_block)
			{
				// 非阻塞socket
				return 0;
			}
			else if (ec == boost::asio::error::timed_out)
			{
				if (m_subscribe) // 订阅的读取超时了，定义为未读取到
				{
					return 0;
				}
			}
			LogError("RedisAsync Read Error, %s", ec.message().c_str());
			return -1;
		}
		if (size > 0)
		{
			m_recvbuff.insert(m_recvbuff.end(), inbuff.begin(), inbuff.begin() + size);
			m_recvbytes += size;
		}
	} while (true);
}

bool RedisAsync::ReadRollback(int len)
{
	m_recvpos -= len;
	if (m_recvpos <= 0)
	{
		m_recvpos = 0;
		return false;
	}
	return true;
}

void RedisAsync::ClearRecvBuff()
{
	m_recvbuff.clear();
	m_recvpos = 0;
}

void RedisAsync::ResetRecvBuff()
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
