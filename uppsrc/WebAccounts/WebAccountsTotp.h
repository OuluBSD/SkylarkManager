#ifndef _WebAccounts_WebAccountsTotp_h_
#define _WebAccounts_WebAccountsTotp_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// TOTP (RFC 6238) with HMAC-SHA256, 30-second step, 6 digits -- used for the
// meta-account app's second factor. Secret transport encoding is RFC 4648
// base32 (no padding), matching authenticator-app manual entry.
// ---------------------------------------------------------------------------

String TotpBase32Encode(const String& raw);
String TotpBase32Decode(const String& b32); // "" on invalid input

String TotpSecretGenerate();                       // 20 random bytes, base32

// Current time as true Unix seconds (Upp Time::Get() is seconds since year 1).
int64  TotpUnixNow();
String TotpCode(const String& secret_b32, int64 unix_time,
                int step_seconds = 30, int digits = 6);
bool   TotpVerify(const String& secret_b32, const String& code, int64 unix_time,
                  int window = 1, int step_seconds = 30, int digits = 6);

// Recovery codes: plain form "warc_<10hex>-<10hex>"; stored as peppered
// hashes (WebAuthPepperedHash with the caller's pepper).
Vector<String> RecoveryCodesGenerate(int count = 8);
String         RecoveryCodeHash(const String& pepper, const String& code);
bool           RecoveryCodeVerify(const String& pepper, const String& stored_hash,
                                  const String& code);

END_UPP_NAMESPACE

#endif
