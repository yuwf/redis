#ifndef _REDISTEST_H_
#define _REDISTEST_H_

// by git@github.com:yuwf/redis.git

#include <assert.h>
#include "RedisSyncPipeline.h"


void RedisTest_Sync(RedisSync& redis)
{
	RedisLogInfo("RedisTest_Sync Begin");

	// key命令
	if (redis.Set("__test_string__", 9527) != 1) { RedisLogError("Set"); return; }
	
	int index = redis.Index();
	if (redis.Move("__test_string__", (index + 1) % 16) != 1) { RedisLogError("Move"); return; }
	if (!redis.Command("SELECT", (index + 1) % 16)) { RedisLogError("SELECT"); return; }
	if (redis.Exists("__test_string__") != 1) { RedisLogError("Exists"); return; }
	if (redis.Move("__test_string__", index) != 1) { RedisLogError("Move"); return; }
	if (!redis.Command("SELECT", index)) { RedisLogError("SELECT"); return; }

	long long expire = 0;
	if (redis.TTL("__test_string__", expire) != 1 || expire != -1) { RedisLogError("TTL"); return; }
	if (redis.Expire("__test_string__", 10000) != 1) { RedisLogError("Expire"); return; }
	if (redis.PTTL("__test_string__", expire) != 1 || expire <= 0) { RedisLogError("PTTL"); return; }
	if (redis.Persist("__test_string__") != 1) { RedisLogError("Persist"); return; }
	if (redis.PTTL("__test_string__", expire) != 1 || expire != -1) { RedisLogError("PTTL"); return; }
	
	std::string randomkey;
	if (redis.RandomKey(randomkey) != 1 || randomkey.empty()) { RedisLogError("RandomKey"); return; }
	if (redis.Rename("__test_string__", "__test_set_m_") != 1) { RedisLogError("Rename"); return; }
	if (redis.RenameNX("__test_set_m_", "__test_string__") != 1) { RedisLogError("RenameNX"); return; }

	std::string type;
	if (redis.Type("__test_string__", type) != 1 || type != "string") { RedisLogError("Type"); return; }
	std::string dump;
	if (redis.Dump("__test_string__", dump) != 1) { RedisLogError("Dump"); return; }
	if (redis.Del("__test_string__") != 1) { RedisLogError("Del"); return; }

	// 字符串命令
	long long rst = 0;
	if (redis.Set("__test_string__", 9527) != 1) { RedisLogError("Set"); return; }
	if (redis.Get("__test_string__", rst) != 1 || rst != 9527) { RedisLogError("Get"); return; }
	if (redis.Incr("__test_string__", rst) != 1 || rst != 9528) { RedisLogError("Incr"); return; }
	if (redis.Incrby("__test_string__", 2, rst) != 1 || rst != 9530) { RedisLogError("Incrby"); return; }
	if (redis.Exists("__test_string__") != 1) { RedisLogError("Exists"); return; }
	redis.Del("__test_string__");

	std::vector<std::string> msetk = { "__test_mset_1__", "__test_mset_2__", "__test_mset_3__" };
	std::map<std::string, int> msetkv = { {"__test_mset_1__", 9527}, {"__test_mset_2__", 9527 }, {"__test_mset_3__", 9527} };
	if (redis.MSet(msetkv) != 1) { RedisLogError("MSet"); return; }
	std::vector<int> mgetv;
	if (redis.MGet(msetk, mgetv) != 1 || mgetv.size() != 3 || mgetv[0] != 9527 || mgetv[1] != 9527 || mgetv[2] != 9527) { RedisLogError("MGet"); return; }
	redis.Dels(msetk);

	// dict
	if (redis.HSet("__test_hset__", "f", 9527) != 1) { RedisLogError("HSet"); return; }
	if (redis.HExists("__test_hset__", "f") != 1) { RedisLogError("HExists"); return; }
	rst = 0;
	if (redis.HGet("__test_hset__", "f", rst) != 1 || rst != 9527) { RedisLogError("HGet"); return; }
	if (redis.HIncrby("__test_hset__", "f", 3, rst) != 1 || rst != 9530) { RedisLogError("HIncrby"); return; }
	if (redis.HExists("__test_hset__", "f") != 1) { RedisLogError("HExists"); return; }
	if (redis.HDel("__test_hset__", "f") != 1) { RedisLogError("HDel"); return; }
	redis.Del("__test_hset__");

	std::vector<std::string> hsetf = { "f1", "f2", "f3" };
	std::map<std::string, int> hsetfv = { { "f1", 9527 },{ "f2", 9527 },{ "f3", 9527 } };
	std::vector<int> hmgetv;
	if (redis.HMSet("__test_hset__", hsetfv) != 1) { RedisLogError("HMSet"); return; }
	if (redis.HMGet("__test_hset__", hsetf, hmgetv) != 1 || hmgetv.size() != 3 || hmgetv[0] != 9527 || hmgetv[1] != 9527 || hmgetv[2] != 9527) { RedisLogError("HMGet"); return; }
	std::vector<std::string> hkeys;
	if (redis.HKeys("__test_hset__", hkeys) != 1 || hkeys.size() != 3 || hkeys[0] != "f1" || hkeys[1] != "f2" || hkeys[2] != "f3") { RedisLogError("HKeys"); return; }
	std::vector<int> hvals;
	if (redis.HVals("__test_hset__", hvals) != 1 || hvals.size() != 3 || hvals[0] != 9527 || hvals[1] != 9527 || hvals[2] != 9527) { RedisLogError("HVals"); return; }
	std::map<std::string, int> hgetall;
	if (redis.HGetAll("__test_hset__", hgetall) != 1) { RedisLogError("HGetAll"); return; }
	if (redis.HLen("__test_hset__") != 3) { RedisLogError("HLen"); return; }
		
	std::map<std::string, int> hscan;
	int hcursor = 0;
	do 
	{
		if (redis.HScan("__test_hset__", 0, "f*", 2, hcursor, hscan) != 1) { RedisLogError("HScan"); return; }
	} while (hcursor != 0);
	if (hscan.size() != 3 || hscan["f1"] != 9527 || hscan["f2"] != 9527 || hscan["f3"] != 9527) { RedisLogError("HScan2"); return; }
	if (redis.HDels("__test_hset__", hsetf) != 3) { RedisLogError("HDels"); return; }

	redis.Del("__test_hset__");

	// list
	redis.Del("__test_list__");
	if (redis.LPush("__test_list__", 9527) != 1) { RedisLogError("LPush"); return; }
	std::vector<int> listaddv = { 9527,9527,9527 };
	if (redis.LPushs("__test_list__", listaddv) != 4) { RedisLogError("LPushs"); return; }
	if (redis.LLen("__test_list__") != 4) { RedisLogError("LLen"); return; }
	int listrmv = 0;
	if (redis.LPop("__test_list__", listrmv) != 1 || listrmv != 9527) { RedisLogError("LPop"); return; }
	std::vector<int> listv;
	if (redis.LRange("__test_list__", 0, -1, listv) != 1 || listv.size() != 3 || listv[0] != 9527 || listv[1] != 9527 || listv[2] != 9527) { RedisLogError("LRange"); return; }
	if (redis.LRem("__test_list__", 2, 9527) != 2) { RedisLogError("LRem"); return; }
	if (redis.RPush("__test_list__", 9527) != 2) { RedisLogError("RPush"); return; }
	if (redis.RPushs("__test_list__", listaddv) != 5) { RedisLogError("RPushs"); return; }
	if (redis.LTrim("__test_list__", 0, 2) != 1) { RedisLogError("LTrim"); return; }
	if (redis.RPop("__test_list__", listrmv) != 1 || listrmv != 9527) { RedisLogError("RPop"); return; }
	redis.Del("__test_list__");

	// set
	redis.Del("__test_set__");
	redis.Del("__test_set_1__");
	if(redis.SAdd("__test_set__", 9527) != 1) { RedisLogError("SAdd"); return; }
	std::vector<int> setaddv = { 9528,9529,9530 };
	if (redis.SAdds("__test_set__", setaddv) != 3) { RedisLogError("SAdds"); return; }
	if (redis.SCard("__test_set__") != 4) { RedisLogError("SCard"); return; }
	if (redis.SRem("__test_set__", 9527) != 1) { RedisLogError("SRem"); return; }
	std::vector<int> setv;
	if (redis.SISMember("__test_set__", 9530) != 1) { RedisLogError("SISMember"); return; }
	if (redis.SMembers("__test_set__", setv) != 1 || setv.size() != 3 || setv[0] != 9528 || setv[1] != 9529 || setv[2] != 9530) { RedisLogError("SMembers"); return; }
	if (redis.SAdds("__test_set_1__", setaddv) != 3) { RedisLogError("SAdds"); return; }
	if (redis.SAdd("__test_set__", 9527) != 1) { RedisLogError("SAdd"); return; }
	std::vector<std::string> setkeys = { "__test_set__", "__test_set_1__" };
	std::vector<int> setdiff;
	if (redis.SDiff(setkeys, setdiff) != 1 || setdiff.size() != 1 || setdiff[0] != 9527) { RedisLogError("SDiff"); return; }
	setdiff.clear();
	if (redis.Sinter(setkeys, setdiff) != 1 || setdiff.size() != 3 || setdiff[0] != 9528 || setdiff[1] != 9529 || setdiff[2] != 9530) { RedisLogError("Sinter"); return; }
	setdiff.clear();
	if (redis.SUnion(setkeys, setdiff) != 1 || setdiff.size() != 4 || setdiff[0] != 9527 || setdiff[1] != 9528 || setdiff[2] != 9529 || setdiff[3] != 9530) { RedisLogError("SUnion"); return; }
	int setpopv = 0, setrandv = 0;
	if (redis.SPop("__test_set__", setpopv) != 1) { RedisLogError("SPop"); return; }
	if (redis.SRandMember("__test_set__", setrandv) != 1) { RedisLogError("SRandMember"); return; }
	redis.Del("__test_set__");
	redis.Del("__test_set_1__");

	RedisLogInfo("RedisTest_Sync Finish");
}

void RedisTest_Pipeline(RedisSync& redis)
{
	RedisLogInfo("RedisTest_Pipeline Begin");

	bool bdo = false;
	int ibind = 0;
	string strbind;
	// key命令
	RedisSyncPipeline pipeline(redis);
	pipeline.Set("__test_string__", 9527).Bind(strbind); strbind = "";
	bdo = pipeline.Do();
	if (!bdo || strbind != "OK"){ RedisLogError("Set"); return; }
	
	int index = redis.Index();
	pipeline.Move("__test_string__", (index + 1) % 16).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("Move"); return; }

	pipeline.Command("SELECT", (index + 1) % 16);
	bdo = pipeline.Do(); 
	if (!bdo) { RedisLogError("SELECT"); return; }

	pipeline.Exists("__test_string__");
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("Exists"); return; }

	pipeline.Move("__test_string__", index).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("Move"); return; }

	pipeline.Command("SELECT", index);
	bdo = pipeline.Do();
	if (!bdo) { RedisLogError("SELECT"); return; }

	long long expire = 0;
	pipeline.TTL("__test_string__").Bind(expire);
	bdo = pipeline.Do();
	if (!bdo || expire != -1) { RedisLogError("TTL"); return; }

	pipeline.Expire("__test_string__", 10000).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("Expire"); return; }
	pipeline.PTTL("__test_string__").Bind(expire);
	bdo = pipeline.Do();
	if (!bdo || expire <= 0) { RedisLogError("PTTL"); return; }

	pipeline.Persist("__test_string__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("Persist"); return; }

	pipeline.PTTL("__test_string__").Bind(expire);
	bdo = pipeline.Do();
	if (!bdo || expire != -1) { RedisLogError("PTTL"); return; }

	std::string randomkey;
	pipeline.RandomKey().Bind(randomkey);
	bdo = pipeline.Do();
	if (!bdo || randomkey.empty()) { RedisLogError("RandomKey"); return; }

	pipeline.Del("__test_set_m_");
	pipeline.Rename("__test_string__", "__test_set_m_").Bind(strbind);
	bdo = pipeline.Do();
	if (!bdo || strbind != "OK") { RedisLogError("Rename"); return; }

	pipeline.RenameNX("__test_set_m_", "__test_string__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("RenameNX"); return; }

	std::string type;
	pipeline.Type("__test_string__").Bind(type);
	bdo = pipeline.Do();
	if (!bdo || strbind != "OK") { RedisLogError("Type"); return; }

	std::string dump;
	pipeline.Dump("__test_string__").Bind(dump);
	bdo = pipeline.Do();
	if (!bdo) { RedisLogError("Dump"); return; }

	pipeline.Del("__test_string__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("Del"); return; }

	// 字符串命令
	long long rst = 0;
	pipeline.Set("__test_string__", 9527).Bind(strbind); strbind = "";
	bdo = pipeline.Do();
	if (!bdo || strbind != "OK") { RedisLogError("Set"); return; }

	pipeline.Get("__test_string__").Bind(rst);
	bdo = pipeline.Do();
	if (!bdo || rst != 9527) { RedisLogError("Get"); return; }

	pipeline.Incr("__test_string__").Bind(rst);
	bdo = pipeline.Do();
	if (!bdo || rst != 9528) { RedisLogError("Incr"); return; }

	pipeline.Incrby("__test_string__", 2).Bind(rst);
	bdo = pipeline.Do();
	if (!bdo || rst != 9530) { RedisLogError("Incrby"); return; }

	redis.Del("__test_string__");

	std::vector<std::string> msetk = { "__test_mset_1__", "__test_mset_2__", "__test_mset_3__" };
	std::map<std::string, int> msetkv = { { "__test_mset_1__", 9527 },{ "__test_mset_2__", 9527 },{ "__test_mset_3__", 9527 } };

	pipeline.MSet(msetkv).Bind(strbind); strbind = "";
	bdo = pipeline.Do();
	if (!bdo || strbind != "OK") { RedisLogError("MSet"); return; }

	std::vector<int> mgetv;
	pipeline.MGet(msetk).BindList(mgetv);
	bdo = pipeline.Do();
	if (!bdo || mgetv.size() != 3 || mgetv[0] != 9527 || mgetv[1] != 9527 || mgetv[2] != 9527) { RedisLogError("MGet"); return; }

	redis.Dels(msetk);

	// dict
	pipeline.HSet("__test_hset__", "f", 9527).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("HSet"); return; }

	pipeline.HExists("__test_hset__", "f").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("HExists"); return; }

	pipeline.HGet("__test_hset__", "f").Bind(rst);
	bdo = pipeline.Do();
	if (!bdo || rst != 9527) { RedisLogError("HGet"); return; }

	pipeline.HIncrby("__test_hset__", "f", 3).Bind(rst);
	bdo = pipeline.Do();
	if (!bdo || rst != 9530) { RedisLogError("HIncrby"); return; }

	pipeline.HDel("__test_hset__", "f").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("HDel"); return; }

	redis.Del("__test_hset__");

	std::vector<std::string> hsetf = { "f1", "f2", "f3" };
	std::map<std::string, int> hsetfv = { { "f1", 9527 },{ "f2", 9527 },{ "f3", 9527 } };

	pipeline.HMSet("__test_hset__", hsetfv).Bind(strbind); strbind = "";
	bdo = pipeline.Do();
	if (!bdo || strbind != "OK") { RedisLogError("HMSet"); return; }

	std::vector<int> hmgetv;
	pipeline.HMGet("__test_hset__", hsetf).BindList(hmgetv);
	bdo = pipeline.Do();
	if (!bdo || hmgetv.size() != 3 || hmgetv[0] != 9527 || hmgetv[1] != 9527 || hmgetv[2] != 9527) { RedisLogError("HMGet"); return; }

	std::vector<std::string> hkeys;
	pipeline.HKeys("__test_hset__").BindList(hkeys);
	bdo = pipeline.Do();
	if (!bdo || hkeys.size() != 3 || hkeys[0] != "f1" || hkeys[1] != "f2" || hkeys[2] != "f3") { RedisLogError("HKeys"); return; }

	std::vector<int> hvals;
	pipeline.HVals("__test_hset__").BindList(hvals);
	bdo = pipeline.Do();
	if (!bdo || hvals.size() != 3 || hvals[0] != 9527 || hvals[1] != 9527 || hvals[2] != 9527) { RedisLogError("HVals"); return; }

	std::map<std::string, int> hgetall;
	pipeline.HGetAll("__test_hset__").BindMap(hgetall);
	bdo = pipeline.Do();
	if (!bdo || hgetall.size() != 3 || hgetall["f1"] != 9527 || hgetall["f2"] != 9527 || hgetall["f3"] != 9527) { RedisLogError("HGetAll"); return; }
	
	pipeline.HLen("__test_hset__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 3) { RedisLogError("HLen"); return; }

	std::map<std::string, int> hscan;
	int hcursor = 0;
	do
	{
		pipeline.HScan("__test_hset__", 0, "f*", 2).ScanBindMap(hcursor, hscan);
		bdo = pipeline.Do();
		if (!bdo) { RedisLogError("HScan"); return; }
	} while (hcursor != 0);
	if (hscan.size() != 3 || hscan["f1"] != 9527 || hscan["f2"] != 9527 || hscan["f3"] != 9527) { RedisLogError("HScan2"); return; }

	pipeline.HDels("__test_hset__", hsetf).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 3) { RedisLogError("HDels"); return; }

	pipeline.Del("__test_hset__");

	// list
	pipeline.Del("__test_list__");
	pipeline.Do();

	pipeline.LPush("__test_list__", 9527).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("LPush"); return; }

	std::vector<int> listaddv = { 9527,9527,9527 };
	pipeline.LPushs("__test_list__", listaddv).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 4) { RedisLogError("LPushs"); return; }

	pipeline.LLen("__test_list__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 4) { RedisLogError("LLen"); return; }

	pipeline.LPop("__test_list__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 9527) { RedisLogError("LPop"); return; }

	std::vector<int> listv;
	pipeline.LRange("__test_list__", 0, -1).BindList(listv);
	bdo = pipeline.Do();
	if (!bdo || listv.size() != 3 || listv[0] != 9527 || listv[1] != 9527 || listv[2] != 9527) { RedisLogError("LRange"); return; }

	pipeline.LRem("__test_list__", 2, 9527).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 2) { RedisLogError("LRem"); return; }

	pipeline.RPush("__test_list__", 9527).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 2) { RedisLogError("RPush"); return; }

	pipeline.RPushs("__test_list__", listaddv).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 5) { RedisLogError("RPushs"); return; }

	pipeline.LTrim("__test_list__", 0, 2).Bind(strbind); strbind = "";
	bdo = pipeline.Do();
	if (!bdo || strbind != "OK") { RedisLogError("LTrim"); return; }
	
	pipeline.RPop("__test_list__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 9527) { RedisLogError("RPop"); return; }

	pipeline.Del("__test_list__");

	// set
	pipeline.Del("__test_set__");
	pipeline.Del("__test_set_1__");
	pipeline.Do();

	pipeline.SAdd("__test_set__", 9527).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("SAdd"); return; }

	std::vector<int> setaddv = { 9528,9529,9530 };
	pipeline.SAdds("__test_set__", setaddv).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 3) { RedisLogError("SAdds"); return; }

	pipeline.SCard("__test_set__").Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 4) { RedisLogError("SCard"); return; }

	pipeline.SISMember("__test_set__", 9527).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("SISMember"); return; }

	pipeline.SRem("__test_set__", 9527).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("SRem"); return; }

	std::vector<int> setv;
	pipeline.SMembers("__test_set__").BindList(setv);
	bdo = pipeline.Do();
	if (!bdo || setv.size() != 3 || setv[0] != 9528 || setv[1] != 9529 || setv[2] != 9530) { RedisLogError("SISMember"); return; }

	pipeline.SAdds("__test_set_1__", setaddv);
	pipeline.SAdd("__test_set__", 9527);

	std::vector<std::string> setkeys = { "__test_set__", "__test_set_1__" };
	std::vector<int> setdiff;
	pipeline.SDiff(setkeys).BindList(setdiff);
	bdo = pipeline.Do();
	if (!bdo || setdiff.size() != 1 || setdiff[0] != 9527) { RedisLogError("SDiff"); return; }

	pipeline.Sinter(setkeys).BindList(setdiff); setdiff.clear();
	bdo = pipeline.Do();
	if (!bdo || setdiff.size() != 3 || setdiff[0] != 9528 || setdiff[1] != 9529 || setdiff[2] != 9530) { RedisLogError("Sinter"); return; }

	pipeline.SUnion(setkeys).BindList(setdiff); setdiff.clear();
	bdo = pipeline.Do();
	if (!bdo || setdiff.size() != 4 || setdiff[0] != 9527 || setdiff[1] != 9528 || setdiff[2] != 9529 || setdiff[3] != 9530) { RedisLogError("SUnion"); return; }

	int setpopv = 0, setrandv = 0;
	pipeline.SPop("__test_set__").Bind(setpopv);
	bdo = pipeline.Do();
	if (!bdo || setpopv == 0) { RedisLogError("SPop"); return; }

	pipeline.SRandMember("__test_set__").Bind(setrandv);
	bdo = pipeline.Do();
	if (!bdo || setrandv == 0) { RedisLogError("SRandMember"); return; }

	pipeline.Del("__test_set__");
	pipeline.Del("__test_set_1__");
	pipeline.Do();


	// script
	const std::string lua =
		R"lua(
			local v = redis.call("HGET", KEYS[1], "v")
			if not v then
				redis.call("HSET", KEYS[1], "v", ARGV[1])
				return 1
			elseif v == "9527" then
				redis.call("HINCRBY", KEYS[1], "v", 1)
				return 2
			end
			return 0
		)lua";
	RedisScript script(lua);
	std::vector<std::string> keys, args;
	keys.push_back("__test_script__");
	args.push_back("9527");

	pipeline.Script(script, keys, args).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 1) { RedisLogError("DoScript1"); return; }

	pipeline.Script(script, keys, args).Bind(ibind); ibind = 0;
	bdo = pipeline.Do();
	if (!bdo || ibind != 2) { RedisLogError("DoScript2"); return; }

	pipeline.Del("__test_script__");
	pipeline.Do();

	RedisLogInfo("RedisTest_Pipeline Finish");
}

#endif