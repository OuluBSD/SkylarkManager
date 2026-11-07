#ifndef _WebAuth_WebAuthCrypto_h_
#define _WebAuth_WebAuthCrypto_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// Password hashing and token primitives, backed by Core/SSL (OpenSSL)
// PBKDF2-HMAC-SHA256. PBKDF2-HMAC-SHA256 is a deterministic standard, so
// verifying a hash computed by a different implementation (e.g. a hand-
// rolled one) against the same password/salt/iterations still succeeds --
// only the stored *encoding* differs between the legacy formats below, not
// the underlying hash bytes.
// ---------------------------------------------------------------------------

#define WEBAUTH_PBKDF2_ITERATIONS 150000
#define WEBAUTH_PASSWORD_HASH_LEN 32

// Canonical format: "pbkdf2_sha256$<iterations>$<salt_hex>$<hash_hex>"
String WebAuthHashPassword(const String& password, int iterations = WEBAUTH_PBKDF2_ITERATIONS);

// Explicit-salt overload for deterministic/reproducible hashes (e.g. fixed
// test fixture seeding, where a random salt would make output non-reproducible).
String WebAuthHashPassword(const String& password, const String& salt, int iterations);

// Verifies against the canonical format above (identical to CompanySkylark's
// pre-migration format, so existing hashes keep verifying unchanged) or the
// legacy ManagerSkylark/SystemAuth format
// "pbkdf2-sha256:<iterations>:<salt_hex>:<hash_hex>". Never rewrites the
// stored hash; callers re-hash explicitly if they want to upgrade a verified
// legacy hash to the canonical format.
bool WebAuthVerifyPassword(const String& password, const String& stored_hash);

// Hex-encoded HMAC-SHA256(secret, raw_token) -- used for peppered session
// and bearer token hashes.
String WebAuthPepperedHash(const String& secret, const String& raw_token);

bool   WebAuthConstantTimeEquals(const String& a, const String& b);

// Cryptographically secure random byte string (hex-encoded when hex=true).
String WebAuthRandomBytes(int count, bool hex = true);

END_UPP_NAMESPACE

#endif
