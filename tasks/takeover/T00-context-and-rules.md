# T00 — Context and rules (read first)

## What the engine is now

One Skylark process hosts multiple **fenced virtual websites**:
`WebEngineHost : SkylarkApp` opens one acceptor per engine port
(`uppsrc/WebEngine/Host.cpp`); `SKYLARK` catch-alls (`**`, `**:POST_RAW`,
`:PUT`, `:PATCH`, `:HEAD`, `:OPTIONS` in `Host.cpp`) forward unmatched
requests to `WebEngine::Gateway`, which resolves the site from a thread-local
listener port, enforces the mutation fence (exact `Origin`/`Referer`
authority + server-side CSRF token), resolves the engine-owned session, and
dispatches against the site's own `SiteRouter`. Stock Skylark routes always
win over the engine catch-alls; the stock session subsystem is never engaged
for engine traffic (cookie names differ, `SessionSet` never called).

Per-site isolation: `EngineSite` owns id/title/external_origin/data_root +
`SiteAccounts` (users, site-stamped sessions `was_*`, CSRF tokens) + its own
`SiteRouter`. Cookies are `wfs_<site>`, HttpOnly, SameSite=Strict. Data
crosses sites only via `WebBroker` (in-process transport, bounded diagnostics
ledger, TCP stub).

## Packages (all under `uppsrc/` unless noted)

| Package | Role |
|---------|------|
| `WebStorage` | site-scoped JSON-file/memory record stores (flock/LockFileEx + fsync + atomic rename, quarantine, optimistic revisions), `SchemaRegistry` |
| `WebBroker` | versioned cross-site call contracts, diagnostics, TCP stub |
| `WebAccounts` | per-site users, TOTP (RFC 6238 SHA256), recovery codes, `WebGrants` consent |
| `WebEngine` | engine host, routers, request fence, sessions |
| `MetaAccount` | meta-account webapp + broker service (authenticate/request-consent/check-grant/revoke-grant) |
| `ForgeCore` | `ForgeRef`/`ForgeIssue`/`ForgeResult`(+provenance)/`IForgeBackend` |
| `GitHubForge` | IForgeBackend over argv-only `gh api` |
| `SourceForgeForge` | IForgeBackend over Allura REST (injectable transport) |
| `PrivateHubWorkspace` | local workspace backend (contained paths, argv-only git, site-scoped issues) |
| `Science` | read-only corpus frontend + validated article sidecars |
| `upptst/WebEngineTests` | fence/self-test host — 116/116 checks, run it directly, exit 0 |

## U++ traps that already cost debugging time (do not rediscover)

- `StoreAsJson(jsonizeStruct)` → plain JSON. `StoreAsJson(ValueMap)` or
  re-wrapping `StoreAsJsonValue(x)` → Upp **typed debug JSON**
  (`{"key":{"type":..}}`) which parses back as an ARRAY. Plain for maps:
  `AsJSON(Value(map))`. Records with Vector members: explicit copy
  ctor/`operator=` with `clone(...)` (see `WebAccounts.h`).
- `ValueMap::operator[](int)` is a **key lookup** (int 0 → key "0")! Positional
  access is `GetValue(i)` / `GetKey(i)` (this bug broke schema validation).
- `StaticMutex` as a non-static member is uninitialized pre-C++20
  (`StaticPrimitive_::once`) — use plain `Mutex` members (we do).
- `TcpSocket::Connect` returns true on `EINPROGRESS`; a freshly accepted
  socket with default zero-wait timeout closes before request bytes arrive —
  engine acceptors set `request.Timeout(30000)` before `Dispatch` (Host.cpp).
- `callback1(fn)` cannot deduce params from a bare function pointer;
  `SiteRouter::Add` has a `void(*)(WebRequest&)` overload — use it.
- Upp `Time::Get()` is seconds since year 1 — use `TotpUnixNow()` for RFC 6238.
- Console test output: `setvbuf(stdout, NULL, _IONBF, 0)` at startup
  (buffered output is otherwise lost on `_Exit`/crash — cost us a debugging
  session).
- Upp `Format` does not guarantee printf `*`-width — avoid `%0*d`.
- CI on all platforms compiles `WebEngineTests` (MSVC included) — keep code
  portable; POSIX-only test fixtures are documented in `ci.yml`.

## Commands

```bash
cd /home/sblo/Dev/SkylarkManager
uppbuilder -m CLANG WebEngineTests -v && ./bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/WebEngineTests
uppbuilder -m CLANG ManagerSkylark -v && ./bin/ManagerSkylark --test
cd /home/sblo/Dev/CompanySkylark
uppbuilder -m CLANG -a ../SkylarkManager/uppsrc CompanySkylark -v && ./bin/CompanySkylark --test
# GCC before pushing (CI also builds MSVC; MSVC compile coverage via CI)
uppbuilder -m GCC WebEngineTests -v
```

## Boundaries still in force

- `CandidateWorkflow` compiled mutations return `candidate_writes_disabled`;
  the `#if 0` write paths stay disabled; tests assert the disabled state.
- CompanySkylark must not depend on the ManagerSkylark **app** package
  (generic packages are fine; T05 covers the rule text update).
- Upstream `ultimatepp` untouched; BazaarCore changes are surgical and
  additive (see `f73fb72` for the pattern; Core patches go through
  `patches/Core.patch` — note `--make-patches` cannot capture root-file Core
  changes, hand-extend the patch and verify with a dry-run against pristine
  upstream).
- Science corpus records are read-only in `uppsrc/Science` (flat
  `skylark/paper` shape; never feed that directory to `JsonFileStore` — it
  would quarantine the files).
