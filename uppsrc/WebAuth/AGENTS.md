# WebAuth Package Guide

`uppsrc/WebAuth` is a shared, storage-agnostic session/bearer authentication
library: password hashing, session- and scoped-token resolution, and login/
settings view fragments, reusable by any app in this assembly (currently
the Company site in `uppsrc/Company` and ManagerSkylark's `SystemAuth`).

## File Map

| File | Role |
|------|------|
| `WebAuthModel.h/.cpp` | `WebAuthIdentity` (resolved actor), `WebAuthSession`/`WebAuthToken`/`WebAuthUser` records, `IWebAuthStore` persistence interface |
| `WebAuthCrypto.h/.cpp` | Core/SSL-backed PBKDF2-HMAC-SHA256 password hashing (dual-format verify), peppered token hashing, constant-time compare, secure random bytes |
| `WebAuthService.h/.cpp` | `WebAuthService`: login/logout, identity resolution, deny-by-default guards, token issuance |
| `WebAuthViews.h/.cpp` | `WebAuthViews`: login form, top-bar user menu, sidebar account footer, settings-page fragment |
| `WebAuthSelfTest.h/.cpp` | Standalone round-trip checks against a mock `IWebAuthStore`, for a host app's own `--test` suite to call into |

## Binding Rules

1. **No storage of its own**: `WebAuthService` never touches a filesystem,
   lock, or atomic write. All persistence goes through `IWebAuthStore`,
   implemented by the embedding app's own store. This package must stay
   agnostic to *how* users are stored -- today's adapters wrap "system
   users" style stores, but a future database-backed user-management system
   must be able to plug in via the same interface with no change here.
2. **No domain role/scope table**: an app's role-to-scope mapping is
   supplied via `WebAuthConfig::role_scopes`, never hardcoded in this
   package.
3. **No `WebFramework` dependency**: HTML fragments in `WebAuthViews` use
   `wf-*` CSS class names by convention only (visual compatibility, not a
   compile dependency), plus a small `wa-*` class set of their own, styled
   by `WebAuthViews::GetStyles()`. This keeps the dependency graph a DAG and
   keeps `WebAuth` usable by non-WebFramework-based callers.
4. **Dual-format password verification is load-bearing**: `WebAuthVerifyPassword`
   accepts both the canonical `pbkdf2_sha256$<iter>$<salt>$<hash>` encoding
   and the legacy `pbkdf2-sha256:<iter>:<salt>:<hash>` encoding so that
   already-persisted hashes from either migrated app keep verifying. Never
   rewrite a stored hash on read.
