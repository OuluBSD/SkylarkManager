# WebAccounts Package Guide

`uppsrc/WebAccounts` is the instance-scoped account layer for fenced
websites: each site owns its users, sessions, and CSRF tokens. There are no
global statics; a `SiteAccounts` object is bound to exactly one site and one
data root. Password hashing and token randomness come from the shared
`WebAuth` package crypto primitives (`WebAuth/WebAuthCrypto.h`); persistence
uses `WebStorage`.

## File Map

| File | Role |
|------|------|
| `WebAccounts.h/.cpp` | `WebAccountUser`, `WebAccountSession` (site-stamped), `SiteAccounts` (per-site user/session/CSRF/TOTP management), `WebAccountsSelfTest` helpers |
| `WebAccountsTotp.h/.cpp` | TOTP (RFC 6238, HMAC-SHA256, base32 secrets) and single-use recovery codes |
| `WebAccountsGrants.h/.cpp` | `WebAccountGrant`/`WebGrants`: explicit, revocable consent grants from a meta-account user to client webapps |

## Invariants

1. **No global state**: accounts, sessions, and secrets live in one
   `SiteAccounts` instance; two sites never share users, sessions, or secrets
   even inside one process.
2. **Sessions are site-stamped**: every session record stores its site id and
   `ResolveSession` rejects records whose stamp does not match, so a cookie
   copied across sites or ports cannot authenticate anywhere else.
3. **Server-side CSRF**: each session carries a random CSRF token; browser
   mutations must present it (`__wf_csrf__` field or `X-WF-CSRF` header).
   Exact-origin enforcement lives in WebEngine (transport layer); this token
   is the second fence.
4. **Deny by default**: disabled users fail verification; expired sessions
   fail resolution; guest is the fallback identity everywhere.
5. **No raw tokens persisted**: session ids are random; the store keeps the
   id itself (it is a bearer-equivalent secret) — rotate by logout/login.
   Password hashes use PBKDF2 from WebAuth; legacy hash formats keep
   verifying via `WebAuthVerifyPassword`.
6. **TOTP is real and load-bearing for enrolled accounts**: enrollment is
   pending until a live code confirms it; login of an enrolled account
   requires password + live code (or one single-use recovery code). Codes
   are RFC 6238 HMAC-SHA256 (30 s step, ±1 window); secrets are base32;
   recovery codes are stored only as peppered hashes.
   **Upp time caveat**: `Time::Get()` is seconds since year 1; always use
   `TotpUnixNow()` for RFC 6238 counters.
7. **Consent grants are the cross-site permission primitive**: client webapps
   obtain scopes only via granted, non-expired `WebAccountGrant` records,
   requested idempotently through the broker, decided by the subject on the
   meta site, and revocable at any time.
