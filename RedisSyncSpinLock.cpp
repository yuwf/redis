
#include <chrono>
#include <thread>
#include <regex>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include "RedisSyncSpinLock.h"

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

	static const std::string lua =
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
	static RedisScript script(lua);

	RedisResult rst;
	if (m_redis.Script(script, keys, args) == 1)
	{
		return rst.ToInt() == 1;
	}
	return false;
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

	static const std::string lua =
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
	static RedisScript script(lua);

	RedisResult rst;
	if (m_redis.Script(script, keys, args) == 1)
	{
		return rst.ToInt() == 1;
	}
	return false;
}

bool RedisSyncSpinLock::RecursiveUnLock(const std::string& key)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::ostringstream ss;
	ss << s_redisspinlockuuid << ":" << std::this_thread::get_id();

	std::vector<std::string> args;
	args.push_back(ss.str());		// ARGV[1]

	static const std::string lua =
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
	static RedisScript script(lua);

	RedisResult rst;
	if (m_redis.Script(script, keys, args) == 1)
	{
		return rst.ToInt() == 1;
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
				RedisLogError("********* %s Lock time more than %d(US) *********", m_key.c_str(), time);
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

	//static std::regex reg(R"((?<=[:/{_-])(\d+|[0-9a-zA-Z]{24,})(?=[:/}_-]|$))");
	static std::regex reg(R"(([:/{_-])(\d+|[0-9a-zA-Z]{24,}))");
	std::string k = std::regex_replace(key, reg, "*");

	// 先用共享锁 如果存在直接修改
	{
		READ_LOCK(mutex);
		auto it = ((const RedisSpinLockDataMap&)records).find(k); // 显示的调用const的find
		if (it != records.cend())
		{
			return it->second;
		}
	}

	// 不存在构造一个
	RedisSpinLockData* p = new RedisSpinLockData;
	// 直接用写锁
	{
		WRITE_LOCK(mutex);
		records.insert(std::make_pair(k, p));
	}
	return p;
}

std::string RedisSpinLockRecord::Snapshot(SnapshotType type, const std::string& metricsprefix, const std::map<std::string, std::string>& tags)
{
	RedisSpinLockDataMap lastdata;
	{
		READ_LOCK(mutex);
		lastdata = records;
	}

	static const int metirs_num = 7;
	static const char* metirs[metirs_num] = 
	{ 
		"redissyncspinlock_lockcount", "redissyncspinlock_faillockcount", 
		"redissyncspinlock_trylock", "redissyncspinlock_maxtrylock",
		"redissyncspinlock_locked", "redissyncspinlock_maxlocked",
		"redissyncspinlock_spincount"
	};

	std::ostringstream ss;
	if (type == Json)
	{
		ss << "[";
		int index = 0;
		for (const auto& it : lastdata)
		{
			RedisSpinLockData* p = it.second;
			int64_t value[metirs_num] = { p->lockcount, p->faillockcount, p->trylockTSC / TSCPerUS(), p->trylockMaxTSC / TSCPerUS(), p->lockedTSC / TSCPerUS(), p->lockedMaxTSC / TSCPerUS(), p->spincount };
			ss << ((++index) == 1 ? "[" : ",[");
			for (int i = 0; i < metirs_num; ++i)
			{
				ss << (i == 0 ? "{" : ",{");
				ss << "\"metrics\":\"" << metricsprefix << metirs[i] << "\",";
				ss << "\"key\":\"" << it.first << "\",";
				for (const auto& it : tags)
				{
					ss << "\"" << it.first << "\":\"" << it.second << "\",";
				}
				ss << "\"value\":" << value[i] << "";
				ss << "}";
			}
			ss << "]";
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
			RedisSpinLockData* p = it.second;
			int64_t value[metirs_num] = { p->lockcount, p->faillockcount, p->trylockTSC / TSCPerUS(), p->trylockMaxTSC / TSCPerUS(), p->lockedTSC / TSCPerUS(), p->lockedMaxTSC / TSCPerUS(), p->spincount };
			for (int i = 0; i < metirs_num; ++i)
			{
				ss << metricsprefix << metirs[i];
				ss << ",key=" << it.first << tag;
				ss << " value=" << value[i] << "i\n";
			}
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
			RedisSpinLockData* p = it.second;
			int64_t value[metirs_num] = { p->lockcount, p->faillockcount, p->trylockTSC / TSCPerUS(), p->trylockMaxTSC / TSCPerUS(), p->lockedTSC / TSCPerUS(), p->lockedMaxTSC / TSCPerUS(), p->spincount };
			for (int i = 0; i < metirs_num; ++i)
			{
				ss << metricsprefix << metirs[i];
				ss << "{key=\"" << it.first << "\"" << tag << "}";
				ss << " " << value[i] << "\n";
			}
		}
	}
	return ss.str();
}
