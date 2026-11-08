# T05 — WP8: CompanySkylark dual-use (standalone app + registrable module)

## Goal

CompanySkylark becomes a **dual-use** package: standalone executable (own
`main.cpp`, guarded main) **and** a registrable engine module that a composed
host (TaskFoundry submission target, private deployment) can include.

## Preconditions

- CompanySkylark `--test` green; its security invariants respected
  (fail-closed, idempotency, append-only ledger, per-collection atomic
  writes).

## Work items

1. **Main guard**: move `CONSOLE_APP_MAIN` into `main.cpp` guarded by
   `#ifdef flagMAIN` calling `CompanyMain()`; keep the package's
   `mainconfig` (dual-main package — this is the sanctioned exception to the
   "no mainconfig in libraries" rule). Upstream macro is `INITBLOCK` /
   `INITBLOCK_(Name)` (Core/Defs.h:100–106), not `INITBLOCK(Name)`.
2. **Module registration**: `.icpp` unit with
   `INITBLOCK_(CompanySkylark) { register module }` — registration only
   records a descriptor/factory; configuration, credentials, stores resolve
   later by the host (no cross-package static-init ordering assumptions).
3. **Broker service on the Company side** (used by TaskFoundry T04):
   - `create-proposal-draft` (idempotent by content hash → returns Company
     proposal id; no compensation effects),
   - `get-proposal-status`,
   - read-only ops for review status. Everything else stays internal.
   - Auth: the composed-host caller is the host itself (module trust
     boundary) + scopes; when Company runs standalone, broker transport is
     unavailable and that is fine.
4. **Dependency rule update**: revise `CompanySkylark/AGENTS.md` "No
   ManagerSkylark coupling" → "no dependency on the ManagerSkylark **app**
   package; generic packages (WebStorage/WebAccounts/WebBroker/WebEngine
   primitives) are allowed". Keep data isolation: Company keeps its own
   store/credentials; nothing else reads them.
5. **Known Company gaps** (fix opportunistically while touching CompanyStore):
   `JsonStore` validates `schema_version` but not `platform_version`
   (CompanyModel.h:14 vs :463); JSON temp files created 0644 pre-umask
   (JsonStore.cpp:128–129) — tighten to 0600; `secret.key` initialized
   outside the lock/atomic-write path (CompanyStore.cpp:24–40); ID generator
   doc says 16 hex chars but produces 32 (JsonStore.cpp:407–412).
6. **Static-build retention check**: the `.icpp` registration must survive
   static linking (verify non-Blitz build; the INITBLOCK lives in the same TU
   as referenced symbols or use an explicit registration call from the host).

## Acceptance

- `./bin/CompanySkylark --test` PASSED (standalone unchanged).
- Composed-host fence test: Company module registered; TaskFoundry
  submission creates a real Company proposal draft; Company `--test` still
  green.
- Both repos build CLANG + GCC; CI green.

## Notes

- Do not move Company onto the engine's session/cookie layer yet — its own
  WebAuth-backed auth stays for standalone mode; module-mode auth wiring is
  a documented follow-up (see T08).
