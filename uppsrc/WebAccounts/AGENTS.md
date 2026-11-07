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
| `WebAccounts.h/.cpp` | `WebAccountUser`, `WebAccountSession` (site-stamped), `SiteAccounts` (per-site user/session/CSRF management), `WebAccountsSelfTest` helpers |

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
6. **TOTP-ready structure**: user records carry `totp_secret`/`totp_enrolled`
   fields reserved for the meta-account app (WP2); they are unused in WP1 and
   never consulted for authorization yet.
