#ifndef _REDISSYNC_H_
#define _REDISSYNC_H_

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <boost/asio.hpp>
#include <boost/any.hpp>

// ͬ�����ã���֧�ֶ��߳� yuwf
// ���˿����ܵ��⣬ÿ���������֧��ԭ���Բ���
// ��������ܺ������Ƕ�������һ��ʹ��

class RedisResult
{
public:
	typedef std::vector<RedisResult> Array;

	RedisResult();

	bool IsError() const;
	
	bool IsNull() const;
	bool IsInt() const;
	bool IsString() const;
	bool IsArray() const;

	int ToInt() const;
	long long ToLongLong() const;
	const std::string& ToString() const;
	const Array& ToArray() const;

	// String����ʹ�� ����ʹ��
	int StringToInt() const;
	float StringToFloat() const;
	double StringToDouble() const;

protected:
	friend class RedisSync;
	boost::any v;
	bool error;
};

class RedisSync
{
public:
	RedisSync();
	virtual ~RedisSync();
public:
	bool InitRedis(const std::string& ip, unsigned short port, const std::string& auth = "");
	void Close();

	// ����false��ʾ����ʧ�ܻ��������ȡʧ��
	bool Command(const std::string& cmd, RedisResult& rst);
	bool Command(RedisResult& rst, const char* cmd, ...);

	// �ܵ������� Command������false ������������������-1
	// Begin��Commit����ԳƵ��� ����ֵ��ʾ�����Ƿ�ִ�гɹ�
	bool PipelineBegin();
	bool PipelineCommit();
	bool PipelineCommit(RedisResult::Array& rst);

	// ��Ϊ������Ϣ��������Ϣ�ǲ��ԳƵģ�����ֻ�ܰѶ��Ĺ���д�ľ�����
	// ����ʹ�� �������ĺ�ִ���κ����ֱ�ӷ���false����-1
	// ����false��ʾ����ʧ�ܻ��������ȡʧ�� ����true��ʾ��������
	bool SubScribe(const std::string& channel);
	bool SubScribe(int cnt, ...);
	// ��ȡ���ĵ���Ϣ block��ʾ�Ƿ�����ֱ���յ�������Ϣ
	bool Message(std::string& channel, std::string& msg, bool block);
	// ȡ������
	bool UnSubScribe();

protected:
	bool _Connect();
	void _Close();
	bool _CheckConnect();

	// ����false��ʾ����ʧ�ܻ��������ȡʧ��
	bool _DoCommand(const std::vector<std::string>& buff, RedisResult& rst);
	// ��ʽ������
	void _FormatCommand(const std::vector<std::string>& buff, std::stringstream &cmdbuff);
	// ��������
	bool _SendCommand(const std::string& cmdbuff);

	// ��ȡ������ buff��ʾ��ȡ������ pos��ʾ��������λ��
	// ����ֵ -1 ��ʾ���������߽������� 0 ��ʾδ��ȡ�� 1 ��ʾ��ȡ����
	int _ReadReply(RedisResult& rst, std::vector<char>& buff, int& pos);

	// ����ֵ��ʾ���� -1��ʾ�����ȡʧ�� 0��ʾû�ж�ȡ�� pos��ʾbuff����ʼλ��
	int _ReadByCRLF(std::vector<char>& buff, int pos);
	int _ReadByLen(int len, std::vector<char>& buff, int pos);

	boost::asio::io_service m_ioservice;
	boost::asio::ip::tcp::socket m_socket;
	bool m_bconnected;	// �Ƿ�������

	std::string m_ip;
	unsigned short m_port;
	std::string m_auth;
	int m_readouttime;	// ��ȡ��ʱʱ�� ���� <0 ���޵ȴ� =0 ���ȴ� >0 ��n����

	// �����ܵ�ʹ��
	bool m_pipeline;
	std::stringstream m_pipecmdbuff;
	int m_pipecmdcount;

	// ����ʹ��
	bool m_subscribe;
	std::vector<char> m_submsgbuff;

private:
	// ��ֹ����
	RedisSync(const RedisSync&) = delete;
	RedisSync& operator=(const RedisSync&) = delete;

public:
	// ������ӿ�==================================================
	// ע ��
	// �ܵ������� ����Ỻ������ ����ĺ���������-1
	// �������ĺ�����ĺ���ֱ�ӷ���-1

	// DEL����
	// ����ֵ��ʾɾ���ĸ��� -1��ʾ���������������
	// cnt ��ʾ...�����ĸ��� �������ͱ�����C��ʽ���ַ���char*
	int Del(const std::string& key);
	int Del(int cnt, ...);

	// EXISTS������
	// ����1���� 0������ -1��ʾ���������������
	int Exists(const std::string& key);

	// �����������
	// ����1�ɹ� 0ʧ�� -1��ʾ�������������
	int Expire(const std::string& key, long long value);
	int ExpireAt(const std::string& key, long long value);
	int PExpire(const std::string& key, long long value);
	int PExpireAt(const std::string& key, long long value);
	int TTL(const std::string& key, long long& value);
	int PTTL(const std::string& key, long long& value);

	// SET���� 
	// ����1�ɹ� 0���ɹ� -1��ʾ�������������
	int Set(const std::string& key, const std::string& value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, int value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, float value, unsigned int ex = -1, bool nx = false);
	int Set(const std::string& key, double value, unsigned int ex = -1, bool nx = false);

	// GET����
	// ����1�ɹ� 0���ɹ� -1��ʾ�������������
	int Get(const std::string& key, std::string& value);
	int Get(const std::string& key, int& value);
	int Get(const std::string& key, float& value);
	int Get(const std::string& key, double& value);

	// MSET����
	// ����1�ɹ� 0���ɹ� -1��ʾ�������������
	// cnt ��ʾ...�����ĸ���/2 �������ͱ�����C��ʽ���ַ���char*
	int MSet(const std::map<std::string, std::string>& kvs);
	int MSet(const std::map<std::string, int>& kvs);
	int MSet(const std::map<std::string, float>& kvs);
	int MSet(const std::map<std::string, double>& kvs);
	int MSet(int cnt, ...);

	// MGET����
	// ����1�ɹ� 0���ɹ� -1��ʾ�������������
	// ���ɹ� rstΪ���� Ԫ��Ϊstring����null
	int MGet(const std::vector<std::string>& keys, RedisResult& rst);

	// HSET����
	// ����1�ɹ� 0���ɹ� -1��ʾ�������������
	int HSet(const std::string& key, const std::string& field, const std::string& value);
	int HSet(const std::string& key, const std::string& field, int value);
	int HSet(const std::string& key, const std::string& field, float value);
	int HSet(const std::string& key, const std::string& field, double value);

	// HGET����
	// ����1�ɹ� 0���ɹ� -1��ʾ�������������
	int HGet(const std::string& key, const std::string& field, std::string& value);
	int HGet(const std::string& key, const std::string& field, int& value);
	int HGet(const std::string& key, const std::string& field, float& value);
	int HGet(const std::string& key, const std::string& field, double& value);

	// HLEN����
	// �����б��� 0���ɹ� -1��ʾ�������������
	int HLen(const std::string& key);

	// HEXISTS����
	// ����1���� 0������ -1��ʾ�������������
	int HExists(const std::string& key, const std::string& field);

	// HEDL����
	// ����ֵ��ʾɾ���ĸ��� -1��ʾ�������������
	// cnt ��ʾ...�����ĸ��� �������ͱ�����C��ʽ���ַ���char*
	int HDel(const std::string& key, const std::string& field);
	int HDel(const std::string& key, int cnt, ...);

	// LPUSH����
	// �ɹ������б��� 0���ɹ� -1��ʾ�������������
	// cnt ��ʾ...�����ĸ��� �������ͱ�����C��ʽ���ַ���char*
	int LPush(const std::string& key, const std::string& value);
	int LPush(const std::string& key, int value);
	int LPush(const std::string& key, float value);
	int LPush(const std::string& key, double value);
	int LPush(const std::string& key, int cnt, ...);

	// RPUSH����
	// �ɹ������б��� 0���ɹ� -1��ʾ�������������
	// cnt ��ʾ...�����ĸ��� �������ͱ�����C��ʽ���ַ���char*
	int RPush(const std::string& key, const std::string& value);
	int RPush(const std::string& key, int value);
	int RPush(const std::string& key, float value);
	int RPush(const std::string& key, double value);
	int RPush(const std::string& key, int cnt, ...);

	// LPOP����
	// 1�ɹ� 0���ɹ� -1��ʾ�������������
	// value��ʾ�Ƴ���Ԫ��
	int LPop(const std::string& key);
	int LPop(const std::string& key, std::string& value);
	int LPop(const std::string& key, int& value);
	int LPop(const std::string& key, float& value);
	int LPop(const std::string& key, double& value);

	// RPOP����
	// 1�ɹ� 0���ɹ� -1��ʾ�������������
	// value��ʾ�Ƴ���Ԫ��
	int RPop(const std::string& key);
	int RPop(const std::string& key, std::string& value);
	int RPop(const std::string& key, int& value);
	int RPop(const std::string& key, float& value);
	int RPop(const std::string& key, double& value);

	// LRANGE����
	// 1�ɹ� 0���ɹ� -1��ʾ�������������
	// ���ɹ� rstΪ���� Ԫ��Ϊstring
	int LRange(const std::string& key, int start, int stop, RedisResult& rst);

	// LREM����
	// �ɹ������Ƴ�Ԫ�صĸ��� 0���ɹ� -1��ʾ�������������
	// ���ɹ� rstΪ���� Ԫ��Ϊstring
	int LRem(const std::string& key, int count, std::string& value);

	// LLEN����
	// �����б��� 0���ɹ� -1��ʾ�������������
	int LLen(const std::string& key);

	// SADD����
	// �ɹ�������ӵ����� 0���ɹ� -1��ʾ�������������
	// cnt ��ʾ...�����ĸ��� �������ͱ�����C��ʽ���ַ���char*
	int SAdd(const std::string& key, const std::string& value);
	int SAdd(const std::string& key, int value);
	int SAdd(const std::string& key, float value);
	int SAdd(const std::string& key, double value);
	int SAdd(const std::string& key, int cnt, ...);

	// SREM����
	// �ɹ������Ƴ�Ԫ�ص����� 0���ɹ� -1��ʾ�������������
	// cnt ��ʾ...�����ĸ��� �������ͱ�����C��ʽ���ַ���char*
	int SRem(const std::string& key, const std::string& value);
	int SRem(const std::string& key, int value);
	int SRem(const std::string& key, float value);
	int SRem(const std::string& key, double value);
	int SRem(const std::string& key, int cnt, ...);
};

#endif