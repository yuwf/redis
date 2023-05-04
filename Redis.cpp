#include "Redis.h"

int Redis::ReadReply(RedisResult& rst)
{
	m_readlen = 0;
	int r = _ReadReply(rst);
	if (r == 0 && m_readlen > 0)
	{
		if (!ReadRollback(m_readlen))
		{
			return -2;
		}
	}
	return r;
}

int Redis::_ReadReply(RedisResult& rst)
{
	char* buff = NULL;
	int len = ReadToCRLF(&buff,1);
	if (len <= 0)
	{
		return len;
	}
	m_readlen += len;

	len -= 2; // 后面的\r\n

	char type = buff[0];
	switch (type)
	{
		case '+':
		{
			rst = std::string(&buff[1], &buff[len]);
			break;
		}
		case '-':
		{
			rst = std::string(&buff[1], &buff[len]);
			rst.SetError(true);
			break;
		}
		case ':':
		{
			buff[len] = '\0'; // 修改
			rst = atoll(&buff[1]);
			buff[len] = '\r'; // 恢复
			break;
		}
		case '$':
		{
			buff[len] = '\0'; // 修改
			int strlen = atoi(&buff[1]);
			buff[len] = '\r'; // 恢复

			if (strlen < 0)
			{
				// nil
			}
			else
			{
				char* buff2 = NULL;
				int len = ReadToCRLF(&buff2, strlen);
				if (len <= 0)
				{
					return len;
				}
				m_readlen += len;

				len -= 2; // \r\n

				if (len < strlen)
				{
					return -2;
				}
				if (len == 0)
				{
					rst = std::string();
				}
				else
				{
					rst = std::string(&buff2[0], strlen);
				}
			}
			break;
		}
		case '*':
		{
			buff[len] = '\0'; // 修改
			char* p = &buff[1];
			int size = atoi(p);
			buff[len] = '\r'; // 恢复

			RedisResult::Array ar;
			for (int i = 0; i < size; ++i)
			{
				RedisResult rst2;
				int r = _ReadReply(rst2);
				if ( r <= 0 )
				{
					return r;
				}
				ar.emplace_back(std::move(rst2));
			}
			rst = std::move(ar);
			break;
		}
		default:
		{
			return -2;
		}
	}
	
	return 1;
}

void RedisCommand::FromString(const std::string& cmd)
{
	std::string temp = cmd;
	int protect = 0;	// 1 单引号保护  2 双引号保护
	int size = (int)temp.size();
	for (int i = 0; i < size; ++i)
	{
		if (protect == 1)
		{
			if (temp[i] == '\'' && temp[i - 1] != '\\')
			{
				temp[i] = '\0';
				protect = 0;
			}
			continue;
		}
		if (protect == 2)
		{
			if (temp[i] == '\"' && temp[i - 1] != '\\')
			{
				temp[i] = '\0';
				protect = 0;
			}
			continue;
		}
		if (temp[i] == ' ' || temp[i] == '\t')
		{
			temp[i] = '\0';
		}
		if (temp[i] == '\'')
		{
			if (!(i > 0 && temp[i - 1] == '\\'))
			{
				protect = 1; // 进入单引号
				temp[i] = '\0';
			}
		}
		if (temp[i] == '\"' )
		{
			if (!(i > 0 && temp[i - 1] == '\\'))
			{
				protect = 2; // 进入双引号
				temp[i] = '\0';
			}
		}
	}

	for (int i = 0; i < size;)
	{
		if (temp[i] == '\0')
		{
			++i;
			continue;
		}
		buff.push_back(&temp[i]);
		i += (int)buff.back().size();
	}
}

std::string RedisCommand::ToString() const
{
	std::stringstream ss;
	for (size_t i = 0; i < buff.size(); ++i)
	{
		ss << (i == 0 ? "" : " ") << buff[i];
	}
	return std::move(ss.str());
}

// sha1的算法实现 参考https://github.com/xoreaxeaxeax/movfuscator/tree/master/validation/crypto-algorithms
namespace sha1
{
	/****************************** MACROS ******************************/
	#define SHA1_BLOCK_SIZE 20              // SHA1 outputs a 20 byte digest

	/**************************** DATA TYPES ****************************/
	typedef unsigned char BYTE;             // 8-bit byte
	typedef unsigned int  WORD;             // 32-bit word, change to "long" for 16-bit machines

	typedef struct {
		BYTE data[64];
		WORD datalen;
		unsigned long long bitlen;
		WORD state[5];
		WORD k[4];
	} SHA1_CTX;

	/****************************** MACROS ******************************/
	#define ROTLEFT(a, b) ((a << b) | (a >> (32 - b)))

	/*********************** FUNCTION DEFINITIONS ***********************/
	void sha1_transform(SHA1_CTX* ctx, const BYTE data[])
	{
		WORD a, b, c, d, e, i, j, t, m[80];

		for (i = 0, j = 0; i < 16; ++i, j += 4)
			m[i] = (data[j] << 24) + (data[j + 1] << 16) + (data[j + 2] << 8) + (data[j + 3]);
		for (; i < 80; ++i) {
			m[i] = (m[i - 3] ^ m[i - 8] ^ m[i - 14] ^ m[i - 16]);
			m[i] = (m[i] << 1) | (m[i] >> 31);
		}

		a = ctx->state[0];
		b = ctx->state[1];
		c = ctx->state[2];
		d = ctx->state[3];
		e = ctx->state[4];

		for (i = 0; i < 20; ++i) {
			t = ROTLEFT(a, 5) + ((b & c) ^ (~b & d)) + e + ctx->k[0] + m[i];
			e = d;
			d = c;
			c = ROTLEFT(b, 30);
			b = a;
			a = t;
		}
		for (; i < 40; ++i) {
			t = ROTLEFT(a, 5) + (b ^ c ^ d) + e + ctx->k[1] + m[i];
			e = d;
			d = c;
			c = ROTLEFT(b, 30);
			b = a;
			a = t;
		}
		for (; i < 60; ++i) {
			t = ROTLEFT(a, 5) + ((b & c) ^ (b & d) ^ (c & d)) + e + ctx->k[2] + m[i];
			e = d;
			d = c;
			c = ROTLEFT(b, 30);
			b = a;
			a = t;
		}
		for (; i < 80; ++i) {
			t = ROTLEFT(a, 5) + (b ^ c ^ d) + e + ctx->k[3] + m[i];
			e = d;
			d = c;
			c = ROTLEFT(b, 30);
			b = a;
			a = t;
		}

		ctx->state[0] += a;
		ctx->state[1] += b;
		ctx->state[2] += c;
		ctx->state[3] += d;
		ctx->state[4] += e;
	}

	void sha1_init(SHA1_CTX* ctx)
	{
		ctx->datalen = 0;
		ctx->bitlen = 0;
		ctx->state[0] = 0x67452301;
		ctx->state[1] = 0xEFCDAB89;
		ctx->state[2] = 0x98BADCFE;
		ctx->state[3] = 0x10325476;
		ctx->state[4] = 0xc3d2e1f0;
		ctx->k[0] = 0x5a827999;
		ctx->k[1] = 0x6ed9eba1;
		ctx->k[2] = 0x8f1bbcdc;
		ctx->k[3] = 0xca62c1d6;
	}

	void sha1_update(SHA1_CTX* ctx, const BYTE data[], size_t len)
	{
		size_t i;

		for (i = 0; i < len; ++i) {
			ctx->data[ctx->datalen] = data[i];
			ctx->datalen++;
			if (ctx->datalen == 64) {
				sha1_transform(ctx, ctx->data);
				ctx->bitlen += 512;
				ctx->datalen = 0;
			}
		}
	}

	void sha1_final(SHA1_CTX* ctx, BYTE hash[])
	{
		WORD i;

		i = ctx->datalen;

		// Pad whatever data is left in the buffer.
		if (ctx->datalen < 56) {
			ctx->data[i++] = 0x80;
			while (i < 56)
				ctx->data[i++] = 0x00;
		}
		else {
			ctx->data[i++] = 0x80;
			while (i < 64)
				ctx->data[i++] = 0x00;
			sha1_transform(ctx, ctx->data);
			memset(ctx->data, 0, 56);
		}

		// Append to the padding the total message's length in bits and transform.
		ctx->bitlen += ctx->datalen * 8;
		ctx->data[63] = ctx->bitlen;
		ctx->data[62] = ctx->bitlen >> 8;
		ctx->data[61] = ctx->bitlen >> 16;
		ctx->data[60] = ctx->bitlen >> 24;
		ctx->data[59] = ctx->bitlen >> 32;
		ctx->data[58] = ctx->bitlen >> 40;
		ctx->data[57] = ctx->bitlen >> 48;
		ctx->data[56] = ctx->bitlen >> 56;
		sha1_transform(ctx, ctx->data);

		// Since this implementation uses little endian byte ordering and MD uses big endian,
		// reverse all the bytes when copying the final state to the output hash.
		for (i = 0; i < 4; ++i) {
			hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
			hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
			hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
			hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
			hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
		}
	}
}

std::string RedisScript::CalcSha1(const std::string& s)
{
	char digest[128] = {0};

	sha1::SHA1_CTX ctx;
	unsigned char hash[20] = {0};
	const char* cset = "0123456789abcdef";

	sha1::sha1_init(&ctx);
	sha1::sha1_update(&ctx, (const unsigned char*)s.data(), s.length());
	sha1::sha1_final(&ctx, hash);

	for (int i = 0; i < 20; i++)
	{
		digest[i * 2] = cset[((hash[i] & 0xF0) >> 4)];
		digest[i * 2 + 1] = cset[(hash[i] & 0xF)];
	}
	return std::move(std::string(digest));
}

