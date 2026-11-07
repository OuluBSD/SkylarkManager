#include "WebAuth.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

NAMESPACE_UPP

namespace {

bool Pbkdf2Sha256(const String& password, const String& salt, int iterations, byte *out, int out_len)
{
	return PKCS5_PBKDF2_HMAC(~password, password.GetCount(), (const byte *)~salt, salt.GetCount(),
	                          iterations, EVP_sha256(), out_len, out) == 1;
}

// Canonical / CompanySkylark legacy format: "pbkdf2_sha256$<iter>$<salt_hex>$<hash_hex>"
bool VerifyDollarFormat(const String& password, const String& stored_hash)
{
	Vector<String> parts = Split(stored_hash, '$');
	if(parts.GetCount() != 4 || parts[0] != "pbkdf2_sha256") return false;
	int iterations = ScanInt(parts[1]);
	if(iterations < 1000 || iterations > 10000000 || AsString(iterations) != parts[1]) return false;
	String salt = ScanHexString(~parts[2], ~parts[2] + parts[2].GetCount());
	String expected = ScanHexString(~parts[3], ~parts[3] + parts[3].GetCount());
	if(salt.IsEmpty() || expected.IsEmpty() || expected.GetCount() > 64) return false;
	byte actual[64];
	if(!Pbkdf2Sha256(password, salt, iterations, actual, expected.GetCount())) return false;
	return CRYPTO_memcmp(actual, ~expected, expected.GetCount()) == 0;
}

// Legacy ManagerSkylark/SystemAuth format: "pbkdf2-sha256:<iter>:<salt_hex>:<hash_hex>"
bool VerifyColonFormat(const String& password, const String& stored_hash)
{
	Vector<String> parts = Split(stored_hash, ':');
	if(parts.GetCount() != 4 || parts[0] != "pbkdf2-sha256") return false;
	int iterations = ScanInt(parts[1]);
	if(iterations < 100000 || iterations > 2000000 || AsString(iterations) != parts[1]) return false;
	String salt = ScanHexString(~parts[2], ~parts[2] + parts[2].GetCount());
	String expected = ScanHexString(~parts[3], ~parts[3] + parts[3].GetCount());
	if(salt.IsEmpty() || expected.IsEmpty() || expected.GetCount() > 64) return false;
	byte actual[64];
	if(!Pbkdf2Sha256(password, salt, iterations, actual, expected.GetCount())) return false;
	return CRYPTO_memcmp(actual, ~expected, expected.GetCount()) == 0;
}

}

String WebAuthHashPassword(const String& password, int iterations)
{
	return WebAuthHashPassword(password, WebAuthRandomBytes(16, false), iterations);
}

String WebAuthHashPassword(const String& password, const String& salt, int iterations)
{
	byte hash[WEBAUTH_PASSWORD_HASH_LEN];
	Pbkdf2Sha256(password, salt, iterations, hash, sizeof(hash));
	return "pbkdf2_sha256$" + AsString(iterations) + "$" + ToLower(HexString(~salt, salt.GetCount()))
	       + "$" + ToLower(HexString(hash, sizeof(hash)));
}

bool WebAuthVerifyPassword(const String& password, const String& stored_hash)
{
	if(stored_hash.IsEmpty() || password.IsEmpty())
		return false;
	if(stored_hash.Find('$') >= 0)
		return VerifyDollarFormat(password, stored_hash);
	if(stored_hash.Find(':') >= 0)
		return VerifyColonFormat(password, stored_hash);
	return false;
}

String WebAuthPepperedHash(const String& secret, const String& raw_token)
{
	byte out[32];
	unsigned int out_len = 0;
	HMAC(EVP_sha256(), ~secret, secret.GetCount(), (const unsigned char *)~raw_token, raw_token.GetCount(), out, &out_len);
	return ToLower(HexString(out, out_len));
}

bool WebAuthConstantTimeEquals(const String& a, const String& b)
{
	if(a.GetCount() != b.GetCount())
		return false;
	return CRYPTO_memcmp(~a, ~b, a.GetCount()) == 0;
}

String WebAuthRandomBytes(int count, bool hex)
{
	ASSERT(count > 0);
	SecureBuffer<byte> random = SecureRandom(count);
	if(random.IsEmpty()) return String();
	if(hex)
		return ToLower(HexString(random.Begin(), count));
	return String((const char *)random.Begin(), count);
}

END_UPP_NAMESPACE
