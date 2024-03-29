﻿#include <mutex>
#include "RedisAsync.h"

static std::mutex s_mutexredisasync;
static std::list<RedisAsync*> s_redisasync;

RedisAsync::RedisAsync(bool subscribe)
	: m_socket(m_ioservice)
#if defined(WIN32)
	, m_context(boost::asio::ssl::context::sslv23_client)
#else
	, m_context(boost::asio::ssl::context::tlsv12_client)
#endif
	, m_sslsocket(m_ioservice, m_context)
	, m_subscribe(subscribe)
{
	m_context.set_default_verify_paths();
	m_context.set_verify_mode(boost::asio::ssl::verify_none);

	std::lock_guard<std::mutex> lock(s_mutexredisasync);
	s_redisasync.push_back(this);
}

RedisAsync::~RedisAsync()
{
	std::lock_guard<std::mutex> lock(s_mutexredisasync);
	auto it = std::find(s_redisasync.begin(), s_redisasync.end(), this);
	if (it != s_redisasync.end())
	{
		s_redisasync.erase(it);
	}
}

bool RedisAsync::InitRedis(const std::string& host, unsigned short port, const std::string& auth, int index, bool bssl)
{
	if (host.empty())
	{
		RedisLogError("RedisAsync ip is empty");
		return false;
	}

	m_host = host;
	m_port = port;
	m_auth = auth;
	m_index = index;
	m_bssl = bssl;

	if (!Connect())
	{
		m_host.clear();
		m_port = 0;
		m_auth.clear();
		m_index = 0;
		m_bssl = false;
		return false;
	}
	return true;
}

void RedisAsync::Close()
{
	boost::system::error_code ec;
	boost::asio::ip::tcp::socket& socket = m_bssl ? m_sslsocket.next_layer() : m_socket;
	if (socket.is_open())
	{
		if (m_bssl)
		{
			m_sslsocket.shutdown(ec);
		}
		else
		{
			m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
		}
		socket.close(ec);
	}
	m_bconnected = false;
	ClearRecvBuff();

	// 等待回调的 由于命令执行不确定性 全部回调失败
	std::list<QueueCmdPtr> temp;
	temp.swap(m_queuecommands);
	for (const auto& it : temp)
	{
		if (it->callback)
		{
			static RedisResult s_result;
			it->callback(false, s_result);
		}
		if (it->multicallback)
		{
			static std::vector<RedisResult> s_result;
			it->multicallback(false, s_result);
		}
	}
}

bool RedisAsync::SendCommand(const RedisCommand& cmd, const CallBack& callback)
{
	if (m_subscribe)
	{
		RedisLogError("RedisAsync is SubScribe Object");
		return false;
	}

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	QueueCmdPtr cmdptr(new QueueCmd());
	cmdptr->cmd.push_back(cmd);
	cmdptr->callback = callback;
	m_queuecommands.push_back(cmdptr);
	return true;
}

bool RedisAsync::SendCommand(RedisCommand&& cmd, const CallBack& callback)
{
	if (m_subscribe)
	{
		RedisLogError("RedisAsync is SubScribe Object");
		return false;
	}

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	QueueCmdPtr cmdptr(new QueueCmd());
	cmdptr->cmd.emplace_back(std::forward<RedisCommand>(cmd));
	cmdptr->callback = callback;
	m_queuecommands.push_back(cmdptr);
	return true;
}

bool RedisAsync::SendCommand(const std::vector<RedisCommand>& cmds, const MultiCallBack& callback)
{
	if (m_subscribe)
	{
		RedisLogError("RedisAsync is SubScribe Object");
		return false;
	}
	
	if (!SendAndCheckConnect(cmds))
	{
		return false;
	}

	QueueCmdPtr cmdptr(new QueueCmd());
	cmdptr->cmd = cmds;
	cmdptr->multicallback = callback;
	m_queuecommands.push_back(cmdptr);
	return true;
}

bool RedisAsync::SendCommand(std::vector<RedisCommand>&& cmds, const MultiCallBack& callback)
{
	if (m_subscribe)
	{
		RedisLogError("RedisAsync is SubScribe Object");
		return false;
	}

	if (!SendAndCheckConnect(cmds))
	{
		return false;
	}

	QueueCmdPtr cmdptr(new QueueCmd());
	cmdptr->cmd.swap(cmds);
	cmdptr->multicallback = callback;
	m_queuecommands.push_back(cmdptr);
	return true;
}

void RedisAsync::UpdateReply()
{
	if (m_subscribe)
	{
		RedisLogError("RedisAsync is SubScribe Object");
		return;
	}
	if (m_queuecommands.empty())
	{
		return;
	}
	if (!CheckConnect())
	{
		return;
	}
	
	do 
	{
		RedisResult rst;
		int r = ReadReply(rst);
		if (r < 0)
		{
			RedisLogFatal("RedisAsync ReadReply Fail cmdsize=%d", m_queuecommands.size());
			Close();
			return;
		}
		else if (r == 0)
		{
			break;
		}

		QueueCmdPtr cmdptr = m_queuecommands.front(); // 这里cmdptr要copy 防止下面pop掉
		cmdptr->rst.emplace_back(std::move(rst)); // 下面rst不能再使用了
		size_t index = cmdptr->rst.size() - 1;
		if (cmdptr->rst[index].IsError() && cmdptr->cmd.size() > index)
		{
			RedisLogFatal("RedisAsync err=%s cmd=%s", cmdptr->rst[index].ToString().c_str(), cmdptr->cmd[index].ToString().c_str());
		}
		if (cmdptr->cmd.size() == cmdptr->rst.size())
		{
			m_queuecommands.pop_front(); // 先移除
			if (cmdptr->callback)
			{
				cmdptr->callback(true, cmdptr->rst[0]);
			}
			if (cmdptr->multicallback)
			{
				cmdptr->multicallback(true, cmdptr->rst);
			}
		}
	} while (!m_queuecommands.empty());

	// 收缩下接受数据的buff
	if (m_recvbuff.size() > 1024)
	{
		ResetRecvBuff();
	}
}

bool RedisAsync::SubScribe(const std::string& channel)
{
	if (!m_subscribe)
	{
		RedisLogError("RedisAsync not SubScribe Object");
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
		RedisLogError("RedisAsync not SubScribe Object");
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
		RedisLogError("RedisAsync not SubScribe Object");
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
		RedisLogError("RedisAsync not SubScribe Object");
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
		RedisLogError("RedisAsync not SubScribe Object");
		return -1;
	}
	if (CheckConnect())
	{
		return -1;
	}

	boost::asio::ip::tcp::socket& socket = m_bssl ? m_sslsocket.next_layer() : m_socket;
	if (block)
	{
		boost::system::error_code ec;
		bool non_block(false);
		socket.non_blocking(non_block, ec);
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
			RedisLogError("UnKnown Error");
			break;
		}
		auto rstarray = rst.ToArray();
		if (rstarray[0].ToString() == "message")
		{
			if (!rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsString())
			{
				RedisLogError("UnKnown Error");
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
				RedisLogError("UnKnown Error");
				break;
			}
			std::string channel = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_channel.find(channel);
			if (it != m_channel.end())
			{
				it->second = SubscribeRecv;
				RedisLogInfo("subscribe channel=%s", channel.c_str());
			}
			else
			{
				RedisLogError("not find local subscribe channel, %s", channel.c_str());
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
				RedisLogError("UnKnown Error");
				break;
			}
			std::string channel = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_channel.find(channel);
			if (it != m_channel.end())
			{
				RedisLogInfo("unsubscribe channel=%s", channel.c_str());
				m_channel.erase(it);
			}
			else
			{
				RedisLogError("not find local subscribe channel, %s", channel.c_str());
			}
		}
		else if (rstarray[0].ToString() == "psubscribe")
		{
			if (!rstarray[0].IsString() || !rstarray[1].IsString() || !rstarray[2].IsInt())
			{
				RedisLogError("UnKnown Error");
				break;
			}
			std::string pattern = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_pattern.find(pattern);
			if (it != m_pattern.end())
			{
				it->second = SubscribeRecv;
				RedisLogInfo("psubscribe pattern=%s", pattern.c_str());
			}
			else
			{
				RedisLogError("not find local subscribe pattern, %s", pattern.c_str());
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
				RedisLogError("UnKnown Error");
				break;
			}
			std::string pattern = rstarray[1].ToString();
			int rst = rstarray[2].ToInt();

			auto it = m_pattern.find(pattern);
			if (it != m_pattern.end())
			{
				RedisLogInfo("punsubscribe pattern=%s", pattern.c_str());
				m_pattern.erase(it);
			}
			else
			{
				RedisLogError("not find local subscribe pattern, %s", pattern.c_str());
			}
		}
		else
		{
			RedisLogError("UnKnown Error %s", rstarray[0].ToString().c_str());
			break;
		}

	} while (true);

	// 回复socket状态
	if (block)
	{
		boost::system::error_code ec;
		bool non_block(true);
		socket.non_blocking(non_block, ec);
	}

	// 收缩下接受数据的buff
	if (m_recvbuff.size() > 1024)
	{
		ResetRecvBuff();
	}

	return ret;
}

std::string RedisAsync::Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags)
{
	int64_t ops = 0;
	int64_t sendbytes = 0;
	int64_t recvbytes = 0;
	int64_t sendcost = 0; // 耗时 微妙
	int64_t recvcost = 0;
	{
		std::lock_guard<std::mutex> lock(s_mutexredisasync);
		for (auto it : s_redisasync)
		{
			ops += it->m_ops;
			sendbytes += it->m_sendbytes;
			recvbytes += it->m_recvbytes;
			sendcost += it->m_sendcost;
			recvcost += it->m_recvcost;
		}
	}

	static const int metirs_num = 5;
	static const char* metirs[metirs_num] = { "redisasync_ops", "redisasync_sendbytes", "redisasync_recvbytes", "redisasync_sendcost", "redisasync_recvcost" };
	int64_t value[metirs_num] = { ops, sendbytes, recvbytes, sendcost, recvcost };

	std::ostringstream ss;
	if (type == Json)
	{
		ss << "[";
		for (int i = 0; i < 5; ++i)
		{
			ss << (i == 0 ? "{" : ",{");
			ss << "\"metrics\":\"" << metricsprefix << metirs[i] << "\",";
			for (const auto& it : tags)
			{
				ss << "\"" << it.first << "\":\"" << it.second << "\",";
			}
			ss << "\"value\":" << value[i] << "";
			ss << "}";
		}
		ss << "]";
	}
	else if (type == Influx)
	{
		std::string tag;
		for (const auto& it : tags)
		{
			tag += ("," + it.first + "=" + it.second);
		}
		for (int i = 0; i < metirs_num; ++i)
		{
			ss << metricsprefix << metirs[i] << tag << " value=" << value[i] << "i\n";
		}
	}
	else if (type == Prometheus)
	{
		std::string tag;
		if (!tags.empty())
		{
			tag += "{";
			int index = 0;
			for (const auto& it : tags)
			{
				tag += ((++index) == 1 ? "" : ",");
				tag += (it.first + "=\"" + it.second + "\"");
			}
			tag += "}";
		}
		for (int i = 0; i < metirs_num; ++i)
		{
			ss << metricsprefix << metirs[i] << tag << " " << value[i] << "\n";
		}
	}
	return ss.str();
}

bool RedisAsync::Connect()
{
	Close();

	// 支持域名
	boost::asio::ip::tcp::resolver nresolver(m_ioservice);
	boost::asio::ip::tcp::resolver::query nquery(m_host, std::to_string(m_port));
	boost::system::error_code ec;
	boost::asio::ip::tcp::resolver::iterator it = nresolver.resolve(nquery, ec);
	boost::asio::ip::tcp::resolver::iterator end_iterator;
	if (ec || it == end_iterator)
	{
		RedisLogError("RedisAsync Valid Redis Address, host=%s port=%d, %s", m_host.c_str(), (int)m_port, ec.message().c_str());
		return false;
	}

	boost::asio::ip::tcp::socket& socket = m_bssl ? m_sslsocket.next_layer() : m_socket;
	boost::asio::ip::address addr;
	bool bconnect = false;
	boost::system::error_code connect_ec; // 连接错误
	while (!bconnect && it != end_iterator)
	{
		socket.open(it->endpoint().protocol(), connect_ec);
		if (connect_ec)
		{
			it++;
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
		//nRet = setsockopt(socket.native(), SOL_SOCKET, SO_CONNECT_TIME, (const char*)&conntimeout, sizeof(conntimeout));
		nRet = setsockopt(socket.native(), SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendtimeout, sizeof(sendtimeout));
		nRet = setsockopt(socket.native(), SOL_SOCKET, SO_RCVTIMEO, (const char*)&recvtimeout, sizeof(recvtimeout));

		socket.set_option(boost::asio::ip::tcp::no_delay(true), ec);
		socket.set_option(boost::asio::socket_base::keep_alive(true), ec);

		// 先设置阻塞 连接成功之后再设置非阻塞
		bool non_block(false);
		socket.non_blocking(non_block, ec);

		addr = it->endpoint().address();
		socket.connect(*it, connect_ec);
		if (connect_ec)
		{
			socket.close(ec);
			it++;
			continue;
		}
		if (m_bssl)
		{
			m_sslsocket.handshake(boost::asio::ssl::stream_base::client, connect_ec);
			if (connect_ec)
			{
				socket.close(ec);
				it++;
				continue;
			}
		}
		bconnect = true;
	}

	if (!bconnect)
	{
		RedisLogError("RedisAsync Connect Fail, host=%s[%s] port=%d, %s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, connect_ec.message().c_str());
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
			Close();
			return false;
		}

		RedisResult rst;
		if (ReadReply(rst) != 1)
		{
			RedisLogError("RedisAsync Maybe Not Valid Redis Address, host=%s[%s] port=%d ssl=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_bssl ? "true" : "false");
			Close();
			return false;
		}
		if (rst.IsNull() || rst.IsError())
		{
			RedisLogError("RedisAsync Auth Error, host=%s[%s] port=%d auth=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_auth.c_str());
			Close();
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
			Close();
			return false;
		}

		RedisResult rst;
		if (ReadReply(rst) != 1)
		{
			RedisLogError("RedisAsync Maybe Not Valid Redis Address, host=%s[%s] port=%d ssl=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_bssl ? "true" : "false");
			Close();
			return false;
		}
		if (rst.IsNull() || rst.IsError())
		{
			RedisLogError("RedisAsync Select Error, %d", m_index);
			Close();
			return false;
		}
	}

	RedisLogInfo("RedisAsync Connect Success, host=%s[%s] port=%d index=%d ssl=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_index, m_bssl ? "true" : "false");
	m_bconnected = true;
	ClearRecvBuff();

	// 设置为非阻塞
	bool non_block(true);
	socket.non_blocking(non_block, ec);

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
			RedisLogError("RedisAsync Not Connect");
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
	std::size_t sendsize = 0;
	const std::size_t buffsize = sendbuff.size();
	while (sendsize < buffsize)
	{
		std::size_t size = 0;
		if (m_bssl)
		{
			size = m_sslsocket.write_some(sendbuff.data(), ec);
		}
		else
		{
			size = m_socket.write_some(sendbuff.data(), ec);
		}
		if (ec)
		{
			RedisLogError("RedisAsync Write Error, %s", ec.message().c_str());
			return false;
		}
		sendsize += size;
		sendbuff.consume(size);
	}
	m_sendbytes += sendsize;
	int64_t end = TSC();
	m_sendcost += (end - begin) / TSCPerUS();
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
	std::size_t sendsize = 0;
	const std::size_t buffsize = sendbuff.size();
	while (sendsize < buffsize)
	{
		std::size_t size = 0;
		if (m_bssl)
		{
			size = m_sslsocket.write_some(sendbuff.data(), ec);
		}
		else
		{
			size = m_socket.write_some(sendbuff.data(), ec);
		}
		if (ec)
		{
			RedisLogError("RedisAsync Write Error, %s", ec.message().c_str());
			return false;
		}
		sendsize += size;
		sendbuff.consume(size);
	}
	m_sendbytes += sendsize;
	int64_t end = TSC();
	m_sendcost += (end - begin) / TSCPerUS();
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

		int64_t begin = TSC();
		boost::system::error_code ec;
		size_t size = 0;
		if (m_bssl)
		{
			size = m_sslsocket.read_some(boost::asio::buffer(m_inbuff), ec);
		}
		else
		{
			size = m_socket.read_some(boost::asio::buffer(m_inbuff), ec);
		}
		int64_t end = TSC();
		m_recvcost += ((end - begin) / TSCPerUS());
		m_recvbytes += size;
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
			RedisLogError("RedisAsync Read Error, %s", ec.message().c_str());
			return -1;
		}
		if (size > 0)
		{
			m_recvbuff.insert(m_recvbuff.end(), m_inbuff.begin(), m_inbuff.begin() + size);
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
