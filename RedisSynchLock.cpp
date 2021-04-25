
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include "RedisSynchLock.h"

#include "LLog.h"
#define LogError LLOG_ERROR

thread_local std::unordered_map<std::string, RedisSynchLockData> g_default_redissynchlockdata;
bool g_record_redissynchlockdata = false;
thread_local bool g_record_redissynchlockdata_thread = false;

bool RedisSynchLock::ScopedLock(const std::string& key, unsigned int millisec)
{
	std::ostringstream ss;
	ss << getpid() << ":" << std::this_thread::get_id();

	int ret = m_redis.Set(key, ss.str(), -1, millisec, true);
	if (ret == 1)
	{
		return true;
	}
	return false;
}

bool RedisSynchLock::ScopedUnLock(const std::string& key)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::ostringstream ss;
	ss << getpid() << ":" << std::this_thread::get_id();

	std::vector<std::string> args;
	args.push_back(ss.str());		// ARGV[1]

	std::string script =
		R"lua(
			local exists = redis.call("EXISTS", KEYS[1])
			if exists == 0 then
				return 1
			end
			local s = redis.call("GET", KEYS[1])
			if s == ARGV[1] then
				redis.call("DEL", KEYS[1])
				return 1
			end
			return 0
		)lua";

	RedisResult rst;
	if (m_redis.Eval(script, keys, args, rst) == 1)
	{
		return rst.ToInt() == 1;
	}
	return false;
}

bool RedisSynchLock::RecursiveLock(const std::string& key)
{
	return false;
}

bool RedisSynchLock::RecursiveUnLock(const std::string& key)
{
	return false;
}

RedisSpinLocker::RedisSpinLocker(RedisSync& redis, const std::string& key, unsigned int millisec)
	: lock(redis)
{
	trace.key = key;
	trace.beginTSC = TSC();
	bool block = true;
	trace.spincount = 1;
	while (!lock.ScopedLock(trace.key, millisec))
	{
		int64_t time = (TSC() - trace.beginTSC) / TSCPerUS();
		if (time > millisec * 1000 ) // 
		{
			LogError("********* %s Lock time more than %d(US) *********", trace.key.c_str(), time);
			block = false;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		trace.spincount++;
	}
	trace.locking = block;
	trace.lockTSC = TSC();
}

RedisSpinLocker::~RedisSpinLocker()
{
	if (trace.locking)
	{
		lock.ScopedUnLock(trace.key);
	}
	
	if (g_record_redissynchlockdata || g_record_redissynchlockdata_thread)
	{
		int64_t unlock_tsc = TSC();

		RedisSynchLockData& data = g_default_redissynchlockdata[trace.key];
		data.lockcount++;
		data.trylockTSC += (trace.lockTSC - trace.beginTSC);
		if (data.trylockMaxTSC < (trace.lockTSC - trace.beginTSC))
		{
			data.trylockMaxTSC = (trace.lockTSC - trace.beginTSC);
		}
		if (trace.locking)
		{
			data.lockedTSC += (unlock_tsc - trace.lockTSC);
			if (data.lockedMaxTSC < (unlock_tsc - trace.lockTSC))
			{
				data.lockedMaxTSC = (unlock_tsc - trace.lockTSC);
			}
		}
		else
		{
			data.faillockcount++;
		}
		data.spincount += trace.spincount;
	}
}