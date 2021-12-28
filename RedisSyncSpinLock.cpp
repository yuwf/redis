
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/thread.hpp>

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
			int64_t time = (TSC() - m_beginTSC) / TSCPerUS();
			if (time > waitmsec * 1000) // 
			{
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
	
	if (g_record_redisspinlockdata || g_record_redisspinlockdata_thread)
	{
		int64_t unlock_tsc = TSC();

		RedisSpinLockData& data = g_default_redisspinlockdata[m_key];
		data.lockcount++;
		data.trylockTSC += (m_lockTSC - m_beginTSC);
		if (data.trylockMaxTSC < (m_lockTSC - m_beginTSC))
		{
			data.trylockMaxTSC = (m_lockTSC - m_beginTSC);
		}
		if (m_locking)
		{
			data.lockedTSC += (unlock_tsc - m_lockTSC);
			if (data.lockedMaxTSC < (unlock_tsc - m_lockTSC))
			{
				data.lockedMaxTSC = (unlock_tsc - m_lockTSC);
			}
		}
		else
		{
			data.faillockcount++;
		}
		data.spincount += m_spinCount;
	}
}

thread_local RedisSpinLockRecord g_default_redisspinlockdata;
bool g_record_redisspinlockdata = false;
thread_local bool g_record_redisspinlockdata_thread = false;