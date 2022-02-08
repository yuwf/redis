
#include <chrono>
#include <thread>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/thread.hpp>

#include "RedisSyncSpinLock.h"
#include "Clock.h"

static std::string s_redisspinlockuuid = boost::uuids::to_string(boost::uuids::random_generator()());

bool RedisSyncSpinLock::ScopedLock(const std::string& key, unsigned int maxlockmsec)
{
	std::ostringstream ss;
	ss << s_redisspinlockuuid << ":" << std::this_thread::get_id();

	int ret = m_redis.Set(key, ss.str(), -1, maxlockmsec, true);
	if (ret == 1)
	{
		return true;
	}
	return false;
}

bool RedisSyncSpinLock::ScopedUnLock(const std::string& key)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::ostringstream ss;
	ss << s_redisspinlockuuid << ":" << std::this_thread::get_id();

	std::vector<std::string> args;
	args.push_back(ss.str());		// ARGV[1]

	static const std::string script =
		R"lua(
			local v = redis.call("GET", KEYS[1])
			if not v then
				--
			elseif v == ARGV[1] then
				redis.call("DEL", KEYS[1])
			else
				return 0
			end
			return 1
		)lua";

	static boost::shared_mutex m; // 保护scriptsha1
	static std::string scriptsha1;
	return DoScirpt(keys, args, script, scriptsha1, m);
}

bool RedisSyncSpinLock::RecursiveLock(const std::string& key, unsigned int maxlockmsec)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::ostringstream ss;
	ss << s_redisspinlockuuid << ":" << std::this_thread::get_id();

	std::vector<std::string> args;
	args.push_back(ss.str());					// ARGV[1]
	args.push_back(std::to_string(maxlockmsec));	// ARGV[2]

	static const std::string script =
		R"lua(
			if (redis.call('EXISTS', KEYS[1]) == 0) then
				redis.call("HMSET", KEYS[1], "l:v", ARGV[1], "l:n", 1)
				redis.call("PEXPIRE", KEYS[1], ARGV[2])
				return 1
			end

			local v = redis.call("HGET", KEYS[1], "l:v")
			if not v then
				redis.call("HMSET", KEYS[1], "l:v", ARGV[1], "l:n", 1)
			elseif v == ARGV[1] then
				redis.call("HINCRBY", KEYS[1], "l:n", 1)
			else
				return 0
			end
			redis.call("PEXPIRE", KEYS[1], ARGV[2])
			return 1
		)lua";

	static boost::shared_mutex m; // 保护scriptsha1
	static std::string scriptsha1;
	return DoScirpt(keys, args, script, scriptsha1, m);
}

bool RedisSyncSpinLock::RecursiveUnLock(const std::string& key)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::ostringstream ss;
	ss << s_redisspinlockuuid << ":" << std::this_thread::get_id();

	std::vector<std::string> args;
	args.push_back(ss.str());		// ARGV[1]

	static const std::string script =
		R"lua(
			if (redis.call('EXISTS', KEYS[1]) == 0) then
				return 1
			end

			local v = redis.call("HGET", KEYS[1], "l:v")
			if not v then
				return 1
			elseif v == ARGV[1] then
				local n = redis.call("HINCRBY", KEYS[1], "l:n", -1)
				if tonumber(n) <= 0 then
					redis.call("DEL", KEYS[1])
				end
				return 1
			end

			return 0
		)lua";

	static boost::shared_mutex m; // 保护scriptsha1
	static std::string scriptsha1;
	return DoScirpt(keys, args, script, scriptsha1, m);
}

bool RedisSyncSpinLock::DoScirpt(const std::vector<std::string>& keys, const std::vector<std::string>& args, const std::string& script, std::string& scriptsha1, boost::shared_mutex& m)
{
	{
		boost::shared_lock<boost::shared_mutex> l(m);
		RedisResult rst;
		if (scriptsha1.empty() || m_redis.Evalsha(scriptsha1, keys, args, rst) == 0)
		{
			// 脚本为空或者执行错误,下面执行加载和重试
		}
		else
		{
			return rst.ToInt() == 1;
		}
	}

	// 加载脚本
	{
		boost::unique_lock<boost::shared_mutex> l(m);
		if (m_redis.ScriptLoad(script, scriptsha1) != 1)
		{
			return false;
		}
	}
	// 重试
	{
		boost::shared_lock<boost::shared_mutex> l(m);
		RedisResult rst;
		if (m_redis.Evalsha(scriptsha1, keys, args, rst) == 1)
		{
			return rst.ToInt() == 1;
		}
	}
	return false;
}

RedisSyncSpinLocker::RedisSyncSpinLocker(RedisSync& redis, const std::string& key, unsigned int maxlockmsec, unsigned int waitmsec)
	: m_lock(redis)
	, m_key(key)
	, m_beginTSC(TSC())
{
	do 
	{
		m_spinCount++;
		if (m_lock.ScopedLock(m_key, maxlockmsec))
		{
			m_locking = true;
			m_lockTSC = TSC();
			break;
		}
		else
		{
			int64_t tsc = TSC();
			int64_t time = (tsc - m_beginTSC) / TSCPerUS();
			if (time > waitmsec * 1000) // 
			{
				m_failTSC = tsc;
				LogError("********* %s Lock time more than %d(US) *********", m_key.c_str(), time);
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	} while (true);
}

RedisSyncSpinLocker::~RedisSyncSpinLocker()
{
	if (m_locking)
	{
		m_lock.ScopedUnLock(m_key);
	}

	int64_t unlock_tsc = TSC();
	RedisSpinLockData* p = g_redisspinlockrecord.Reg(m_key);
	if (p)
	{
		p->lockcount++;
		if (m_locking)
		{
			int64_t tsc = m_lockTSC - m_beginTSC;
			p->trylockTSC += tsc;
			if (p->trylockMaxTSC < tsc)
			{
				p->trylockMaxTSC = tsc;
			}
			tsc = unlock_tsc - m_lockTSC;
			p->lockedTSC += tsc;
			if (p->lockedMaxTSC < tsc)
			{
				p->lockedMaxTSC = tsc;
			}
		}
		else
		{
			p->faillockcount++;
			int64_t tsc = m_failTSC - m_beginTSC;
			p->trylockTSC += tsc;
			if (p->trylockMaxTSC < tsc)
			{
				p->trylockMaxTSC = tsc;
			}
		}
		p->spincount += m_spinCount;
	}
}

RedisSpinLockRecord g_redisspinlockrecord;

RedisSpinLockData* RedisSpinLockRecord::Reg(const std::string& key)
{
	if (!brecord)
	{
		return NULL;
	}

	// 先用共享锁 如果存在直接修改
	{
		boost::shared_lock<boost::shared_mutex> lock(mutex);
		auto it = ((const RedisSpinLockDataMap&)records).find(key); // 显示的调用const的find
		if (it != records.end())
		{
			return it->second;
		}
	}

	// 不存在构造一个
	RedisSpinLockData* p = new RedisSpinLockData;
	// 直接用写锁
	{
		boost::unique_lock<boost::shared_mutex> lock(mutex);
		records.insert(std::make_pair(key, p));
	}
	return p;
}

std::string RedisSpinLockRecord::Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags)
{
	RedisSpinLockDataMap lastdata;
	{
		boost::unique_lock<boost::shared_mutex> lock(mutex);
		lastdata = records;
	}
	std::ostringstream ss;
	if (type == Json)
	{
		ss << "[";
		int index = 0;
		for (const auto& it : lastdata)
		{
			ss << ((++index) == 1 ? "{" : ",{");
			for (const auto& t : tags)
			{
				ss << "\"" << t.first << "\":\"" << t.second << "\",";
			}
			ss << "\"key\":\"" << it.first << "\",";
			ss << "\"" << metricsprefix << "_lockcount\":" << it.second->lockcount << ",";
			ss << "\"" << metricsprefix << "_faillockcount\":" << it.second->faillockcount << ",";
			ss << "\"" << metricsprefix << "_trylock\":" << (it.second->trylockTSC / TSCPerUS()) << ",";
			ss << "\"" << metricsprefix << "_maxtrylock\":" << (it.second->trylockMaxTSC / TSCPerUS()) << ",";
			ss << "\"" << metricsprefix << "_locked\":" << (it.second->lockedTSC / TSCPerUS()) << ",";
			ss << "\"" << metricsprefix << "_maxlocked\":" << (it.second->lockedMaxTSC / TSCPerUS()) << ",";
			ss << "\"" << metricsprefix << "_spincount\":" << it.second->spincount;
			ss << "}";
		}
		ss << "]";
	}
	else if (type == Influx)
	{
		std::string tag;
		for (const auto& t : tags)
		{
			tag += ("," + t.first + "=" + t.second);
		}
		for (const auto& it : lastdata)
		{
			ss << metricsprefix << "_lockcount";
			ss << ",key=" << it.first << tag;
			ss << " value=" << it.second->lockcount << "i\n";

			ss << metricsprefix << "_faillockcount";
			ss << ",key=" << it.first << tag;
			ss << " value=" << it.second->faillockcount << "i\n";

			ss << metricsprefix << "_trylock";
			ss << ",key=" << it.first << tag;
			ss << " value=" << (it.second->trylockTSC / TSCPerUS()) << "i\n";

			ss << metricsprefix << "_maxtrylock";
			ss << ",key=" << it.first << tag;
			ss << " value=" << (it.second->trylockMaxTSC / TSCPerUS()) << "i\n";

			ss << metricsprefix << "_locked";
			ss << ",key=" << it.first << tag;
			ss << " value=" << (it.second->lockedTSC / TSCPerUS()) << "i\n";

			ss << metricsprefix << "_maxlocked";
			ss << ",key=" << it.first << tag;
			ss << " value=" << (it.second->lockedMaxTSC / TSCPerUS()) << "i\n";

			ss << metricsprefix << "_spincount";
			ss << ",key=" << it.first << tag;
			ss << " value=" << it.second->spincount << "i\n";
		}
	}
	else if (type == Prometheus)
	{
		std::string tag;
		for (const auto& t : tags)
		{
			tag += ("," + t.first + "=\"" + t.second + "\"");
		}
		for (const auto& it : lastdata)
		{
			ss << metricsprefix << "_lockcount";
			ss << "{key=\"" << it.first << "\"" << tag << "}";
			ss << " " << it.second->lockcount << "\n";

			ss << metricsprefix << "_faillockcount";
			ss << "{key=\"" << it.first << "\"" << tag << "}";
			ss << " " << it.second->faillockcount << "\n";

			ss << metricsprefix << "_trylock";
			ss << "{key=\"" << it.first << "\"" << tag << "}";
			ss << " " << (it.second->trylockTSC / TSCPerUS()) << "\n";

			ss << metricsprefix << "_maxtrylock";
			ss << "{key=\"" << it.first << "\"" << tag << "}";
			ss << " " << (it.second->trylockMaxTSC / TSCPerUS()) << "\n";

			ss << metricsprefix << "_locked";
			ss << "{key=\"" << it.first << "\"" << tag << "}";
			ss << " " << (it.second->lockedTSC / TSCPerUS()) << "\n";

			ss << metricsprefix << "_maxlocked";
			ss << "{key=\"" << it.first << "\"" << tag << "}";
			ss << " " << (it.second->lockedMaxTSC / TSCPerUS()) << "\n";

			ss << metricsprefix << "_spincount";
			ss << "{key=\"" << it.first << "\"" << tag << "}";
			ss << " " << it.second->spincount << "\n";
		}
	}
	return ss.str();
}
