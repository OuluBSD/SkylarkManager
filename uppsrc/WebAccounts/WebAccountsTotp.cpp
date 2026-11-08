#include "WebAccountsTotp.h"
#include <WebAuth/WebAuthCrypto.h>

NAMESPACE_UPP

static const char *B32_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

String TotpBase32Encode(const String& raw)
{
	static const char *alpha = B32_ALPHABET;
	StringBuffer out;
	int bits = 0, value = 0;
	for(int i = 0; i < raw.GetCount(); i++) {
		value = (value << 8) | (byte)raw[i];
		bits += 8;
		while(bits >= 5) {
			out << alpha[(value >> (bits - 5)) & 31];
			bits -= 5;
		}
	}
	if(bits > 0)
		out << alpha[(value << (5 - bits)) & 31];
	return String(out);
}

String TotpBase32Decode(const String& b32)
{
	Vector<int> rev;
	rev.SetCount(256, -1);
	for(int i = 0; i < 32; i++)
		rev[(byte)B32_ALPHABET[i]] = i;
	StringBuffer out;
	int bits = 0, value = 0;
	for(int i = 0; i < b32.GetCount(); i++) {
		int c = ToUpper(b32[i]);
		if(c == '=' || c == ' ' || c == '-')
			continue;
		int v = rev[c];
		if(v < 0)
			return String();
		value = (value << 5) | v;
		bits += 5;
		if(bits >= 8) {
			out << (char)((value >> (bits - 8)) & 0xFF);
			bits -= 8;
		}
	}
	return String(out);
}

String TotpSecretGenerate()
{
	return TotpBase32Encode(WebAuthRandomBytes(20, false));
}

int64 TotpUnixNow()
{
	// Upp counts seconds since year 1; Unix epoch starts 1970-01-01.
	return GetUtcTime().Get() - 62135596800LL;
}

static String HmacSha256Hex(const String& key, const String& data)
{
	// WebAuthPepperedHash is hex(HMAC-SHA256(key, data)); unhex it.
	String hex = WebAuthPepperedHash(key, data);
	String raw = ScanHexString(~hex, ~hex + hex.GetCount());
	return raw;
}

String TotpCode(const String& secret_b32, int64 unix_time, int step_seconds, int digits)
{
	String key = TotpBase32Decode(secret_b32);
	if(key.IsEmpty())
		return String();
	int64 counter = unix_time / step_seconds;
	byte msg[8];
	for(int i = 0; i < 8; i++)
		msg[i] = (byte)((counter >> (56 - 8 * i)) & 0xFF);
	String mac = HmacSha256Hex(key, String((const char *)msg, 8));
	if(mac.GetCount() < 20)
		return String();
	// RFC 4226: offset is the low nibble of the LAST MAC byte (31 for SHA256).
	int offset = mac[mac.GetCount() - 1] & 0x0F;
	int bin = ((mac[offset] & 0x7F) << 24) | (((byte)mac[offset + 1]) << 16)
	        | (((byte)mac[offset + 2]) << 8) | (byte)mac[offset + 3];
	int mod = 1;
	for(int i = 0; i < digits; i++)
		mod *= 10;
	String code = AsString(bin % mod);
	while(code.GetCount() < digits)
		code = "0" + code;
	return code;
}

bool TotpVerify(const String& secret_b32, const String& code, int64 unix_time,
                int window, int step_seconds, int digits)
{
	if(code.GetCount() != digits || secret_b32.IsEmpty())
		return false;
	for(int i = -window; i <= window; i++) {
		String want = TotpCode(secret_b32, unix_time + (int64)i * step_seconds, step_seconds, digits);
		if(want.GetCount() && WebAuthConstantTimeEquals(want, code))
			return true;
	}
	return false;
}

Vector<String> RecoveryCodesGenerate(int count)
{
	Vector<String> out;
	for(int i = 0; i < count; i++) {
		String a = WebAuthRandomBytes(5, true);
		String b = WebAuthRandomBytes(5, true);
		out.Add("warc_" + a + "-" + b);
	}
	return out;
}

String RecoveryCodeHash(const String& pepper, const String& code)
{
	return WebAuthPepperedHash(pepper, code);
}

bool RecoveryCodeVerify(const String& pepper, const String& stored_hash, const String& code)
{
	if(stored_hash.IsEmpty() || code.IsEmpty())
		return false;
	String want = RecoveryCodeHash(pepper, code);
	return WebAuthConstantTimeEquals(want, stored_hash);
}

END_UPP_NAMESPACE
