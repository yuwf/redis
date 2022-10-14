
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/thread.hpp>

#include "RedisSyncSubscribeLock.h"

static std::string s_redisasynclockuuid = boost::uuids::to_string(boost::uuids::random_generator()());

RedisSyncSubscribeLock::RedisSyncSubscribeLock(RedisSync& redis)
	: m_redis(redis)
	, m_subscriberedis(true)
	, m_channel(boost::uuids::to_string(boost::uuids::random_generator()()))
{

}

bool RedisSyncSubscribeLock::Init()
{
	if (!m_subscriberedis.InitRedis(m_redis.Host(), m_redis.Port(), m_redis.Auth(), m_redis.Index(), m_redis.SSL()))
	{
		RedisLogError("RedisAsyncLockSubscirbe Init Error");
		return false;
	}

	m_subscriberedis.SubScribe(m_channel);
	return true;
}

void RedisSyncSubscribeLock::Update()
{
	int64_t tsc = TSC();
	bool bcheck = (tsc - m_lasttsc) / TSCPerUS() > 1000000;
	std::set<std::string> checkkey; // 已检查的ke列表

	std::string channel, lockid;
	while (m_subscriberedis.Message(channel, lockid, false) == 1)
	{
		auto it = m_waitlocks.find(lockid);
		if (it != m_waitlocks.end())
		{
			if (ScopedLock(it->second.key, it->second.lockid, it->second.maxlockmsec))
			{
				// 加锁成功
				WaitLock waitlock = it->second;
				m_waitlocks.erase(it);

				waitlock.callback();

				// 解锁
				ScopedUnLock(waitlock.key, waitlock.lockid);

				// 解锁后 会执行检查key的逻辑 这里标记下可以不用检查了
				if (bcheck)
				{
					checkkey.insert(waitlock.key);
				}
			}
			else
			{
				// 没有加锁成功，可能再收订阅通知的过程中有别的加锁了，继续等待
			}
		}
		else
		{
			// 本地为空了
			//LogError("RedisSyncSubscribeLock Local Lock is Empty, channel=%s, lockid=%s", channel.c_str(), lockid.c_str());
		}
	}

	// 1秒检查一次
	if (bcheck)
	{
		// 超时重新加锁
		std::unordered_map<std::string, WaitLock> waitlocaks;
		waitlocaks.swap(m_waitlocks);
		m_waitlocks.reserve(waitlocaks.size());
		for (auto& it : waitlocaks)
		{
			WaitLock& waitlock = it.second;
			if ((tsc - waitlock.beginTSC) / TSCPerUS() > waitlock.waitmsec * 1000)
			{
				if (waitlock.failcallback)
				{
					waitlock.failcallback();
				}
				// 删除等待锁
				std::string waitkey = waitlock.key + ":wait";
				std::string waitlock_ = m_channel + ":" + waitlock.lockid;
				m_redis.Command(std::string("ZREM"), waitkey, waitlock_);
				continue;
			}
			if (checkkey.find(waitlock.key) == checkkey.end())
			{
				checkkey.insert(waitlock.key);
				ScopedCheckLock(waitlock.key);
			}

			// 继续等待
			m_waitlocks[it.first] = it.second;
		}
	}
}

void RedisSyncSubscribeLock::ScopedLock(const std::string& key, std::function<void()> callback, std::function<void()> failcallback, unsigned int maxlockmsec, unsigned int waitmsec)
{
	if (!callback)
	{
		RedisLogError("RedisSyncSubscribeLock callback is Empty, key=%s", key.c_str());
		return;
	}
	std::string lockid = std::to_string(m_increment++);
	if (ScopedLock(key, lockid, maxlockmsec))
	{
		// 加锁成功，直接调用回到
		callback();

		// 解锁
		ScopedUnLock(key, lockid);
	}
	else
	{
		// 加锁失败,Redis已经放到等待列表中了，本地也放列表中等待订阅事件或者重试
		auto it = m_waitlocks.find(lockid);
		if (it != m_waitlocks.end())
		{
			// 这种情况理论上不会出现
			RedisLogError("RedisSyncSubscribeLock the same uuid, channel=%s lockid=%s", m_channel.c_str(), lockid.c_str());
			
			auto failcallback = it->second.failcallback;
			m_waitlocks.erase(it);
			if (failcallback)
			{
				failcallback();
			}
		}
		WaitLock waitlock;
		waitlock.key = key;
		waitlock.lockid = lockid;
		waitlock.maxlockmsec = maxlockmsec;
		waitlock.waitmsec = waitmsec;
		waitlock.beginTSC = TSC();
		waitlock.callback = callback;
		waitlock.failcallback = failcallback;
		m_waitlocks[lockid] = waitlock;
	}
}

bool RedisSyncSubscribeLock::ScopedLock(const std::string& key, const std::string& lockid, unsigned int maxlockmsec)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::vector<std::string> args;
	args.push_back(lockid);						// ARGV[1]
	args.push_back(std::to_string(maxlockmsec));// ARGV[2]
	args.push_back(m_channel);					// ARGV[3]

	static const std::string lua =
		R"lua(
			local waitkey = KEYS[1] .. ":wait"
			local waitlock = ARGV[3] .. ":" .. ARGV[1]
			if redis.call("SET", KEYS[1], ARGV[1], "PX", ARGV[2], "NX") then
				redis.call("ZREM", waitkey, waitlock)	--加锁成功，移除下可能存在的等待
				return 1
			end

			--添加到等待列表中
			if redis.call("ZRANK", waitkey, waitlock) then
				--已经存在了 重试的时候会出现
			else
				--local score = redis.call("TIME") 不可用 time属于随机命令，随机命令之后无法在使用写入命令
				local score = redis.call("INCRBY", "_lock_wait_score_", 1)
				redis.call("ZADD", waitkey, score, waitlock)
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

bool RedisSyncSubscribeLock::ScopedCheckLock(const std::string& key)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::vector<std::string> args;

	static const std::string lua =
		R"lua(
			local v = redis.call("GET", KEYS[1])
			if v then
				return
			end

			--锁为空了 触发等待解锁订阅通知
			local waitkey = KEYS[1] .. ":wait"
			while 1 do
				local waitlocks = redis.call('ZRANGE', waitkey, 0, 0)
				if #waitlocks == 0 then
					break
				end
				local waitlock = waitlocks[1]
				local splitpos = string.find(waitlock, ':')
				if splitpos then
					local channel = string.sub(waitlock, 0, splitpos-1)
					local lockid = string.sub(waitlock, splitpos+1, -1)
					if redis.call("PUBLISH", channel, lockid) > 0 then
						break
					else
						redis.call("ZREM", waitkey, waitlock)
					end
				else
					redis.call("ZREM", waitkey, waitlock)
				end
			end
		)lua";
	static RedisScript script(lua);

	RedisResult rst;
	if (m_redis.Script(script, keys, args) == 1)
	{
		return rst.ToInt() == 1;
	}
	return false;
}

bool RedisSyncSubscribeLock::ScopedUnLock(const std::string& key, const std::string& lockid)
{
	std::vector<std::string> keys;
	keys.push_back(key);

	std::vector<std::string> args;
	args.push_back(lockid);		// ARGV[1]

	static const std::string lua =
		R"lua(
			local v = redis.call("GET", KEYS[1])
			local rst = 0
			if not v then
				--
			elseif v == ARGV[1] then
				redis.call("DEL", KEYS[1])
			else
				return 0
			end

			--锁已释放 触发等待解锁订阅通知
			local waitkey = KEYS[1] .. ":wait"
			while 1 do
				local waitlocks = redis.call('ZRANGE', waitkey, 0, 0)
				if #waitlocks == 0 then
					break
				end
				local waitlock = waitlocks[1]
				local splitpos = string.find(waitlock, ':')
				if splitpos then
					local channel = string.sub(waitlock, 0, splitpos-1)
					local lockid = string.sub(waitlock, splitpos+1, -1)
					if redis.call("PUBLISH", channel, lockid) > 0 then
						break
					else
						redis.call("ZREM", waitkey, waitlock)
					end
				else
					redis.call("ZREM", waitkey, waitlock)
				end
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
