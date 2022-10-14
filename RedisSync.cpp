﻿#include <stdio.h>
#include <stdarg.h>
#include <mutex>
#include <boost/array.hpp>
#include "RedisSync.h"

static std::mutex s_mutexredissync;
static std::list<RedisSync*> s_redissync;

RedisSync::RedisSync(bool subscribe)
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

	std::lock_guard<std::mutex> lock(s_mutexredissync);
	s_redissync.push_back(this);
}

RedisSync::~RedisSync()
{
	std::lock_guard<std::mutex> lock(s_mutexredissync);
	auto it = std::find(s_redissync.begin(), s_redissync.end(), this);
	if (it != s_redissync.end())
	{
		s_redissync.erase(it);
	}
}

bool RedisSync::InitRedis(const std::string& host, unsigned short port, const std::string& auth, int index, bool bssl)
{
	if(host.empty())
	{
		RedisLogError("RedisSync ip is empty");
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

void RedisSync::Close()
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
}

bool RedisSync::DoCommand(const RedisCommand& cmd)
{
	RedisResult rst;
	return DoCommand(cmd, rst);
}

bool RedisSync::DoCommand(const RedisCommand& cmd, RedisResult& rst)
{
	if (m_subscribe)
	{
		RedisLogError("RedisSync is SubScribe Object");
		return false;
	}

	if (!SendAndCheckConnect(cmd))
	{
		return false;
	}

	ClearRecvBuff();

	if (ReadReply(rst) != 1)
	{
		RedisLogError("RedisSync ReadReply Fail cmd=%s", cmd.ToString().c_str());
		Close();
		return false;
	}
	if (rst.IsError())
	{
		RedisLogFatal("RedisSync err=%s cmd=%s", rst.ToString().c_str(), cmd.ToString().c_str());
	}
	return true;
}

bool RedisSync::DoCommand(const std::vector<RedisCommand>& cmds, std::vector<RedisResult>& rst)
{
	if (m_subscribe)
	{
		RedisLogError("RedisSync is SubScribe Object");
		return false;
	}

	int cmdcount = (int)cmds.size();
	if (cmdcount == 0)
	{
		return true;
	}

	if (!SendAndCheckConnect(cmds))
	{
		return false;
	}

	for (int i = 0; i < cmdcount; ++i)
	{
		rst.push_back(RedisResult());
		RedisResult& rst2 = rst.back();
		if (ReadReply(rst2) != 1)
		{
			RedisLogError("RedisSync ReadReply Fail cmd=%s", cmds[i].ToString().c_str());
			Close();
			return false;
		}
		if (rst2.IsError())
		{
			RedisLogFatal("RedisSync err=%s cmd=%s", rst2.ToString().c_str(), cmds[i].ToString().c_str());
		}
		else
		{
		}
	}
	return true;
}

std::string RedisSync::Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags)
{
	int64_t ops = 0;
	int64_t sendbytes = 0;
	int64_t recvbytes = 0;
	int64_t sendcost = 0; // 耗时 微妙
	int64_t recvcost = 0;
	{
		std::lock_guard<std::mutex> lock(s_mutexredissync);
		for (auto it : s_redissync)
		{
			ops += it->m_ops;
			sendbytes += it->m_sendbytes;
			recvbytes += it->m_recvbytes;
			sendcost += it->m_sendcost;
			recvcost += it->m_recvcost;
		}
	}

	static const int metirs_num = 5;
	static const char* metirs[metirs_num] = { "redissync_ops", "redissync_sendbytes", "redissync_recvbytes", "redissync_sendcost", "redissync_recvcost" };
	int64_t value[metirs_num] = { ops, sendbytes, recvbytes, sendcost, recvcost };
	
	std::ostringstream ss;
	if (type == Json)
	{
		ss << "[";
		for (int i = 0; i < metirs_num; ++i)
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

bool RedisSync::SubScribe(const std::string& channel)
{
	if (!m_subscribe)
	{
		RedisLogError("RedisSync not SubScribe Object");
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

bool RedisSync::UnSubScribe(const std::string& channel)
{
	if (!m_subscribe)
	{
		RedisLogError("RedisSync not SubScribe Object");
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

bool RedisSync::PSubScribe(const std::string& pattern)
{
	if (!m_subscribe)
	{
		RedisLogError("RedisSync not SubScribe Object");
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

bool RedisSync::PUnSubScribe(const std::string& pattern)
{
	if (!m_subscribe)
	{
		RedisLogError("RedisSync not SubScribe Object");
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

int RedisSync::Message(std::string& channel, std::string& msg, bool block)
{
	if (!m_subscribe)
	{
		RedisLogError("RedisSync not SubScribe Object");
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
			RedisLogError("RedisSync ReadReply Fail Message");
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

int RedisSync::Publish(const std::string& channel, const std::string& msg)
{
	RedisCommand cmd;
	cmd.Add("PUBLISH");
	cmd.Add(channel);
	cmd.Add(msg);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

bool RedisSync::Connect()
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
		RedisLogError("RedisSync Valid Redis Address, host=%s port=%d, %s", m_host.c_str(), (int)m_port, ec.message().c_str());
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

		// 默认阻塞
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
		RedisLogError("RedisSync Connect Fail, host=%s[%s] port=%d, %s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, connect_ec.message().c_str());
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
			RedisLogError("RedisSync Maybe Not Valid Redis Address, host=%s[%s] port=%d ssl=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_bssl ? "true" : "false");
			Close();
			return false;
		}
		if (rst.IsNull() || rst.IsError())
		{
			RedisLogError("RedisSync Auth Error, host=%s[%s] port=%d auth=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_auth.c_str());
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
			RedisLogError("RedisSync Maybe Not Valid Redis Address, host=%s[%s] port=%d ssl=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_bssl ? "true" : "false");
			Close();
			return false;
		}
		if (rst.IsNull() || rst.IsError())
		{
			RedisLogError("RedisSync Select Error, %d", m_index);
			Close();
			return false;
		}
	}

	ClearRecvBuff();

	RedisLogInfo("RedisSync Connect Success, host=%s[%s] port=%d index=%d ssl=%s", m_host.c_str(), addr.to_string(ec).c_str(), (int)m_port, m_index, m_bssl ? "true" : "false");
	m_bconnected = true;

	// 重新订阅
	if (m_subscribe)
	{
		// 订阅默认不阻塞
		bool non_block(true);
		socket.non_blocking(non_block, ec);

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
			RedisLogError("RedisSync Not Connect");
			return false;
		}
	}
	return true;
}

bool RedisSync::Send(const RedisCommand& cmd)
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
			RedisLogError("RedisSync Write Error, %s", ec.message().c_str());
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

bool RedisSync::Send(const std::vector<RedisCommand>& cmds)
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
			RedisLogError("RedisSync Write Error, %s", ec.message().c_str());
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
		int64_t begin = TSC();
		boost::system::error_code ec;
		size_t size = 0;
		if (m_bssl)
		{
			size = m_sslsocket.read_some(boost::asio::buffer(inbuff), ec);
		}
		else
		{
			size = m_socket.read_some(boost::asio::buffer(inbuff), ec);
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
			RedisLogError("RedisSync Read Error, %s", ec.message().c_str());
			return -1;
		}
		if (size > 0)
		{
			m_recvbuff.insert(m_recvbuff.end(), inbuff.begin(), inbuff.begin() + size);
		}
	} while (true);
}

bool RedisSync::ReadRollback(int len)
{
	m_recvpos -= len;
	if (m_recvpos <= 0)
	{
		m_recvpos = 0;
		return false;
	}
	return true;
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
	RedisCommand cmd;
	cmd.Add("DEL");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Dels(const std::vector<std::string>& key)
{
	if (key.empty())
	{
		return 0;
	}
	RedisCommand cmd;
	cmd.Add("DEL");

	for (auto it = key.begin(); it != key.end(); ++it)
	{
		cmd.Add(*it);
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Dump(const std::string& key, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("DUMP");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Exists(const std::string& key)
{
	RedisCommand cmd;
	cmd.Add("EXISTS");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt(); // 0 或者 1
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Expire(const std::string& key, long long value)
{
	RedisCommand cmd;
	cmd.Add("EXPIRE");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::ExpireAt(const std::string& key, long long value)
{
	RedisCommand cmd;
	cmd.Add("EXPIREAT");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::PExpire(const std::string& key, long long value)
{
	RedisCommand cmd;
	cmd.Add("PEXPIRE");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::PExpireAt(const std::string& key, long long value)
{
	RedisCommand cmd;
	cmd.Add("PEXPIREAT");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Persist(const std::string& key)
{
	RedisCommand cmd;
	cmd.Add("PERSIST");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::TTL(const std::string& key, long long& value)
{
	RedisCommand cmd;
	cmd.Add("TTL");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		value = rst.ToLongLong();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::PTTL(const std::string& key, long long& value)
{
	RedisCommand cmd;
	cmd.Add("PTTL");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		value = rst.ToLongLong();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Keys(const std::string& pattern, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("KEYS");
	cmd.Add(pattern);

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Move(const std::string& key, int index)
{
	RedisCommand cmd;
	cmd.Add("MOVE");
	cmd.Add(key);
	cmd.Add(index);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::RandomKey(std::string& key)
{
	RedisCommand cmd;
	cmd.Add("RANDOMKEY");

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		key = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Rename(const std::string& key, const std::string& newkey)
{
	RedisCommand cmd;
	cmd.Add("RENAME");
	cmd.Add(key);
	cmd.Add(newkey);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString() && rst.ToString() == "OK")
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::RenameNX(const std::string& key, const std::string& newkey)
{
	RedisCommand cmd;
	cmd.Add("RENAMENX");
	cmd.Add(key);
	cmd.Add(newkey);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Scan(int cursor, const std::string& match, int count, int& rstcursor, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("SCAN");
	cmd.Add(cursor);
	if (!match.empty())
	{
		cmd.Add("MATCH");
		cmd.Add(match);
	}
	if (count > 0)
	{
		cmd.Add("COUNT");
		cmd.Add(count);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
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
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Type(const std::string& key, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("TYPE");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Get(const std::string& key, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("GET");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Incr(const std::string& key, long long& svalue)
{
	RedisCommand cmd;
	cmd.Add("INCR");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		svalue = rst.ToLongLong();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
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

int RedisSync::Incrby(const std::string& key, long long value, long long& svalue)
{
	RedisCommand cmd;
	cmd.Add("INCRBY");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		svalue = rst.ToLongLong();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Incrby(const std::string& key, long long value)
{
	long long svalue = 0;
	return Incrby(key, value, svalue);
}

int RedisSync::MGet(const std::vector<std::string>& keys, RedisResult& rst)
{
	if (keys.empty())
	{
		return 0;
	}

	RedisCommand cmd;
	cmd.Add("MGET");

	for (auto it = keys.begin(); it != keys.end(); ++it)
	{
		cmd.Add(*it);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::MSet(const std::map<std::string, std::string>& kvs)
{
	RedisCommand cmd;
	cmd.Add("MSET");
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		cmd.Add(it->first);
		cmd.Add(it->second);
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString() && rst.ToString() == "OK")
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Set(const std::string& key, const std::string& value, unsigned int ex, unsigned int px, bool nx)
{
	RedisCommand cmd;
	cmd.Add("SET");
	cmd.Add(key);
	cmd.Add(value);
	if (ex != -1)
	{
		cmd.Add("EX");
		cmd.Add(ex);
	}
	else if (px != -1)
	{
		cmd.Add("PX");
		cmd.Add(px);
	}
	if (nx)
	{
		cmd.Add("NX");
	}

	// 如果 key 已经存储其他值， SET 就覆写旧值，且无视类型
	// 返回OK 或者nil
	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString() && rst.ToString() == "OK")
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HDel(const std::string& key, const std::string& field)
{
	RedisCommand cmd;
	cmd.Add("HDEL");
	cmd.Add(key);
	cmd.Add(field);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HDels(const std::string& key, const std::vector<std::string>& fields)
{
	if (fields.empty())
	{
		return 0;
	}
	RedisCommand cmd;
	cmd.Add("HDEL");
	cmd.Add(key);

	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		cmd.Add(*it);
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HExists(const std::string& key, const std::string& field)
{
	RedisCommand cmd;
	cmd.Add("HEXISTS");
	cmd.Add(key);
	cmd.Add(field);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt(); // 0 或者 1
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HSet(const std::string& key, const std::string& field, const std::string& value)
{
	RedisCommand cmd;
	cmd.Add("HSET");
	cmd.Add(key);
	cmd.Add(field);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HGet(const std::string& key, const std::string& field, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("HGET");
	cmd.Add(key);
	cmd.Add(field);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HMSet(const std::string& key, const std::map<std::string, std::string>& kvs)
{
	RedisCommand cmd;
	cmd.Add("HMSET");
	cmd.Add(key);
	for (auto it = kvs.begin(); it != kvs.end(); ++it)
	{
		cmd.Add(it->first);
		cmd.Add(it->second);
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString() && rst.ToString() == "OK")
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HMGet(const std::string& key, const std::vector<std::string>& fields, RedisResult& rst)
{
	if (fields.empty())
	{
		return 0;
	}
	RedisCommand cmd;
	cmd.Add("HMGET");
	cmd.Add(key);

	for (auto it = fields.begin(); it != fields.end(); ++it)
	{
		cmd.Add(*it);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HKeys(const std::string& key, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("HKEYS");
	cmd.Add(key);

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HVals(const std::string& key, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("HVALS");
	cmd.Add(key);

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HGetAll(const std::string& key, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("HGETALL");
	cmd.Add(key);

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HIncrby(const std::string& key, const std::string& field, long long value, long long& svalue)
{
	RedisCommand cmd;
	cmd.Add("HINCRBY");
	cmd.Add(key);
	cmd.Add(field);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		svalue = rst.ToLongLong();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HIncrby(const std::string& key, const std::string& field, long long value)
{
	long long svalue = 0;
	return HIncrby(key, field, value, svalue);
}

int RedisSync::HLen(const std::string& key)
{
	RedisCommand cmd;
	cmd.Add("HLEN");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::HScan(const std::string& key, int cursor, const std::string& match, int count, int& rstcursor, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("HSCAN");
	cmd.Add(key);
	cmd.Add(cursor);
	if (!match.empty())
	{
		cmd.Add("MATCH");
		cmd.Add(match);
	}
	if (count > 0)
	{
		cmd.Add("COUNT");
		cmd.Add(count);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
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
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::LLen(const std::string& key)
{
	RedisCommand cmd;
	cmd.Add("LLEN");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::LPop(const std::string& key)
{
	std::string rst;
	return LPop(key, rst);
}

int RedisSync::LPop(const std::string& key, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("LPOP");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::LPush(const std::string& key, const std::string& value)
{
	RedisCommand cmd;
	cmd.Add("LPUSH");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::LPushs(const std::string& key, const std::vector<std::string>& values)
{
	if (values.empty())
	{
		return 0;
	}
	RedisCommand cmd;
	cmd.Add("LPUSH");
	cmd.Add(key);

	for (auto it = values.begin(); it != values.end(); ++it)
	{
		cmd.Add(*it);
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::LRange(const std::string& key, int start, int stop, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("LRANGE");
	cmd.Add(key);
	cmd.Add(start);
	cmd.Add(stop);

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::LRem(const std::string& key, int count, const std::string& value)
{
	RedisCommand cmd;
	cmd.Add("LREM");
	cmd.Add(key);
	cmd.Add(count);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::LTrim(const std::string& key, int start, int stop)
{
	RedisCommand cmd;
	cmd.Add("LTRIM");
	cmd.Add(key);
	cmd.Add(start);
	cmd.Add(stop);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString() && rst.ToString() == "OK")
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::RPop(const std::string& key)
{
	std::string rst;
	return RPop(key, rst);
}

int RedisSync::RPop(const std::string& key, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("RPOP");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::RPush(const std::string& key, const std::string& value)
{
	RedisCommand cmd;
	cmd.Add("RPUSH");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::RPushs(const std::string& key, const std::vector<std::string>& values)
{
	if (values.empty())
	{
		return 0;
	}
	RedisCommand cmd;
	cmd.Add("RPUSH");
	cmd.Add(key);

	for (auto it = values.begin(); it != values.end(); ++it)
	{
		cmd.Add(*it);
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SCard(const std::string& key)
{
	RedisCommand cmd;
	cmd.Add("SCARD");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SDiff(const std::vector<std::string>& keys, RedisResult& rst)
{
	if (keys.empty())
	{
		return 0;
	}

	RedisCommand cmd;
	cmd.Add("SDIFF");
	for (const auto& it : keys)
	{
		cmd.Add(it);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Sinter(const std::vector<std::string>& keys, RedisResult& rst)
{
	if (keys.empty())
	{
		return 0;
	}

	RedisCommand cmd;
	cmd.Add("SINTER");
	for (const auto& it : keys)
	{
		cmd.Add(it);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SISMember(const std::string& key, const std::string& value)
{
	RedisCommand cmd;
	cmd.Add("SISMEMBER");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt(); // 0 或者 1
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SMembers(const std::string& key, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("SMEMBERS");
	cmd.Add(key);

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SPop(const std::string& key, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("SPOP");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SRandMember(const std::string& key, std::string& value)
{
	RedisCommand cmd;
	cmd.Add("SRANDMEMBER");
	cmd.Add(key);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		value = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SRem(const std::string& key, const std::string& value)
{
	RedisCommand cmd;
	cmd.Add("SREM");
	cmd.Add(key);
	cmd.Add(value);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SRems(const std::string& key, const std::vector<std::string>& values)
{
	if (values.empty())
	{
		return 0;
	}
	RedisCommand cmd;
	cmd.Add("SREM");
	cmd.Add(key);

	for (auto it = values.begin(); it != values.end(); ++it)
	{
		cmd.Add(*it);
	}

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt();
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::SUnion(const std::vector<std::string>& keys, RedisResult& rst)
{
	if (keys.empty())
	{
		return 0;
	}

	RedisCommand cmd;
	cmd.Add("SUNION");
	for (const auto& it : keys)
	{
		cmd.Add(it);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsArray())
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}


int RedisSync::Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	RedisResult rst;
	return Eval(script, keys, args, rst);
}

int RedisSync::Eval(const std::string& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("EVAL");
	cmd.Add(script);
	cmd.Add(keys.size());

	for (const auto& it : keys)
	{
		cmd.Add(it);
	}
	for (const auto& it : args)
	{
		cmd.Add(it);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	return 1;
}

int RedisSync::Evalsha(const std::string& scriptsha1, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	RedisResult rst;
	return Evalsha(scriptsha1, keys, args, rst);
}

int RedisSync::Evalsha(const std::string& scriptsha1, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst)
{
	RedisCommand cmd;
	cmd.Add("EVALSHA");
	cmd.Add(scriptsha1);
	cmd.Add(keys.size());

	for (const auto& it : keys)
	{
		cmd.Add(it);
	}
	for (const auto& it : args)
	{
		cmd.Add(it);
	}

	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	return 1;
}

int RedisSync::ScriptExists(const std::string& scriptsha)
{
	RedisCommand cmd;
	cmd.Add("SCRIPT");
	cmd.Add("EXISTS");
	cmd.Add(scriptsha);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsInt())
	{
		return rst.ToInt(); // 0 或者 1
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::ScriptFlush()
{
	RedisCommand cmd;
	cmd.Add("SCRIPT");
	cmd.Add("FLUSH");

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString() && rst.ToString() == "OK")
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::ScriptKill()
{
	RedisCommand cmd;
	cmd.Add("SCRIPT");
	cmd.Add("KILL");

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString() && rst.ToString() == "OK")
	{
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::ScriptLoad(const std::string& script, std::string& sha1)
{
	RedisCommand cmd;
	cmd.Add("SCRIPT");
	cmd.Add("LOAD");
	cmd.Add(script);

	RedisResult rst;
	if (!DoCommand(cmd, rst))
	{
		return -1;
	}
	if (rst.IsError() || rst.IsNull())
	{
		// 命令错误或者结果为空
		return 0;
	}
	if (rst.IsString())
	{
		sha1 = rst.ToString();
		return 1;
	}
	RedisLogError("UnKnown Error");
	return -1;
}

int RedisSync::Script(RedisScript& script, const std::vector<std::string>& keys, const std::vector<std::string>& args)
{
	RedisResult rst;
	return Script(script, keys, args, rst);
}

int RedisSync::Script(RedisScript& script, const std::vector<std::string>& keys, const std::vector<std::string>& args, RedisResult& rst)
{
	if (script.scriptsha1[0] == 0 || Evalsha(script.scriptsha1, keys, args, rst) == 0)
	{
		// 脚本为空或者执行错误,下面执行加载和重试
	}
	else
	{
		return rst.ToInt();
	}
	// 加载脚本
	std::string scriptsha1;
	if (ScriptLoad(script.script, scriptsha1) != 1)
	{
		return 0;
	}
	if (scriptsha1.length() < sizeof(script.scriptsha1)/sizeof(script.scriptsha1[0]))
	{
		strcpy(script.scriptsha1, scriptsha1.c_str());
	}
	else
	{
		RedisLogError("DoScript scriptsha1 too big, %s", scriptsha1.c_str());
	}
	// 重试
	if (Evalsha(scriptsha1, keys, args, rst) == 1)
	{
		return rst.ToInt() == 1;
	}
	return 0;
}
