# AGENTS - Company Package Guide

**Scope**: `uppsrc/Company`, the Company module: its store, workflow, broker
surface, web site and platform v2 API. It is composed into ManagerSkylark as
one engine site (`company`, port 18219 by default, `--company-port`).

## Status and changed rules

The Company code was moved here from the private `CompanySkylark` repository
and is now one composed webapp. Rules that changed in the move:

- **The standalone CompanySkylark app is gone.** No binary, no `--test`
  entry point, no CLI. Its routes live on as the `company` engine site.
- **CompanyAuth is gone.** Login, sessions and CSRF come from WebEngine and
  WebAccounts. Company keeps only its own helpers (`CompanySite.cpp`).
- **CompanyComposed is replaced** by `CompanyComposed.h/.cpp`
  (`CompanyComposedModule`), the host-facing module: it opens the store and
  exposes `Store()` for the site and `Handle()` for the broker.
- **CompanyGitHub is not ported.** Review diffs come from `GitHubForge`, not
  from Company. Nothing in this package calls GitHub.
- **The host installs the site and the broker.** `EngineComposition.cpp` calls
  `InstallCompanySite` and registers the `company` broker service. Company
  does not bind its own port.
- **Passwords live in WebAccounts.** Company keeps a `UserRecord` (username,
  roles, enabled flag, a password hash mirror) for each person. User create
  and password change write both.
- **Not ported:** the `--bootstrap-admin` CLI, the old `csst_` session token
  (the bearer login returns a WebAccounts session id), and the old actor type
  on agent token records.

## Invariants

1. **One record per entity.** Each collection is a `WebStorage` `JsonFileStore`
   under the Company data root. Records are keyed by their own id, or by a
   derived id when the id is not a safe file name.
2. **Lookups by other keys use index records** (`ix-<kind>-<hash>`): usernames,
   token hashes. Index records are written in the same transaction as the
   record they point to. Listings skip them (`CompanyStore::IsIndexId`).
3. **Atomic multi-record writes use `JsonFileStore::Txn`.** One transaction holds
   the collection's in-process mutex and file lock for its whole body. Never
   call a `CompanyStore` method on the same collection from inside a
   transaction; it would wait on the lock the transaction holds.
4. **Append-only collections** (ledger, audit) are written with create-only puts
   and never deleted. A ledger entry's record id is derived from its
   idempotency key (`DerivedId("lgr", key)`). If that id exists, the append
   reports a duplicate and writes nothing, so a retry never credits twice.
5. **Dedup of event keys** lives in the record: `processed_event_keys` is checked
   and appended in the same transaction as the mutation. A repeated key returns
   "event already processed" and changes nothing.
6. **Fail closed.** A collection with a recorded read corruption refuses writes
   (`IsCollectionCorrupted`). The server secret (`secret.key` under the store
   root, written with restricted permissions on first start, at least 32 bytes) must load, or startup
   is refused. The secret keys `CompanyTokenHash`.
7. **No caches.** Every read goes to disk.
8. **Scope before data.** Each broker op and site route checks its scope
   (or role) first and denies by default. Review and compensation are never
   decided or paid through the broker; a draft is never decided by it either.

## Auth model

Every request resolves to a `WebAuthIdentity` in `CompanyIdentity()`
(`CompanySite.cpp`), in this order:

1. **Bearer agent token.** `Authorization: Bearer <raw>`. The hash
   `CompanyTokenHash(secret, raw)` finds an `AgentTokenRecord`. A revoked or
   expired token is a guest. Identity: `user_id` = token owner, `scopes` =
   token scopes, actor type `automation` when the owner has the
   `automation_contributor` role, else `agent`.
2. **Bearer WebAccounts session.** The session id returned by
   `api/auth/login` is resolved with `ResolveSession`. The WebAccounts user must
   be enabled and have a matching enabled Company user (matched by username).
3. **Browser session.** The engine-resolved site session (`r.HasUser()`) maps to
   the Company user with the same username. The user must be enabled.

Anything else is a guest. Human scopes come from the roles, not from storage.

### Role to scope table

`CompanyRoleScopes()` in `CompanySite.cpp` (copied from the old CompanySkylark
`CompanyAuth` table):

| Role | Scopes |
|------|--------|
| `contributor`, `automation_contributor` | `proposals:read`, `proposals:write`, `work:read`, `work:write`, `reviews:read`, `ledger:read` |
| `reviewer` | `proposals:read`, `reviews:read`, `reviews:write`, `work:read`, `ledger:read` |
| `admin` | every scope in `CompanyAllScopes()` (`CompanyModel.h`), including `admin` |
| `client`, `employee` | none in this table |

Agent tokens carry any subset of `CompanyAllScopes()`: `proposals:read`,
`proposals:write`, `work:read`, `work:write`, `reviews:read`, `reviews:write`,
`ledger:read`, `callback:write`, `trials:write`, `admin`.

### Passwords and CSRF

- WebAccounts holds the login password. `api/users/create` and
  `admin/users/create` create both the WebAccounts user and the Company user with
  the same password. A password change verifies the old one, writes WebAccounts,
  and mirrors the hash into the Company record.
- The engine enforces CSRF and origin checks for cookie sessions. Bearer POSTs
  carry no cookie and are not CSRF-checked.

## Site routes

Paths are relative to the `company` site root. Browser pages are GET; forms are
POST to the literal path.

- **Pages:** `/` (home, the welcome view), `login` (GET; redirects to the
  engine sign-in `session/login` for guests, to `dashboard` for signed-in users;
  replaces the old `login/submit`),
  `logout` (POST), `dashboard`, `proposals`, `proposals/new`,
  `proposals/detail/*`, `work`, `work/detail/*`, `account`,
  `account/settings`, `review/queue`, `review/detail/*`, `review/operator/*`,
  `admin/users`, `admin/tokens`, `admin/policy`, `admin/audit`,
  `admin/platform`, `admin/repos`.
- **Form POSTs:** `proposals/create`, `proposals/revise`, `work/bind`,
  `work/request-mechanical`, `work/request-operator`, `work/resume`,
  `account/settings/password`, `review/decide`, `review/operator/decide`,
  `admin/users/roles`, `admin/users/create`, `admin/users/rate`,
  `admin/tokens/create`, `admin/tokens/revoke`, `admin/policy/save`,
  `admin/repos/save`, `admin/compensate`.
- **Health and manifest:** `api/health`, `api/framework/spec`,
  `api/cli/manifest` (keeps the old `skylark-command-manifest/1` shape, not the
  engine manifest).
- **Auth API:** `api/auth/status` (GET), `api/auth/login` (POST, returns the
  session id as the bearer token), `api/auth/logout` (POST).
- **Proposals, work, reviews, ledger, audit, users, tokens, config:** the `api/`
  paths matching the pages above, for example `api/proposals`,
  `api/proposals/create`, `api/proposals/*`, `api/work/bind`,
  `api/reviews/operator/decide`, `api/ledger`, `api/ledger/balance`,
  `api/ledger/correction`, `api/audit`, `api/users`, `api/users/create`,
  `api/tokens/create`, `api/tokens/revoke`, `api/config`, `api/config/update`,
  `api/config/allowlist`.
- **Contracts and projects:** `api/contracts[/*]` and its
  `create`, `negotiate`, `accept`, `complete`, `cancel` actions;
  `api/projects[/*]` and its `create`, `assign`, `start`, `accept-pr`,
  `request-completion`, `ready`, `deliver` actions.
- **Callbacks:** `api/webhook/review-callback` (POST, needs an automation
  identity with `callback:write`).

## Platform v2 API

`PlatformWorkflow.h/.cpp` implements the control plane under `api/v2/`:

- `commands/ingest` (POST) and `commands` (GET): command listener. A command is
  parsed and authorized into an immutable record. Run commands need the
  commenting login to map to a local admin.
- `reviews/dispatch`, `reviews/evidence` (POST), `reviews` and `reviews/*` (GET):
  review dispatch bound to a head SHA. Evidence without a SHA is `incomplete`,
  evidence for another SHA is `stale`; neither advances the lifecycle.
- `trials/dispatch`, `trials/results` (POST), `trials` (GET): external trial
  dispatch with a completeness gate. Trial dispatch and results need an
  automation identity with `trials:write`.
- `artifacts` (GET), `artifacts/record` (POST): artifact metadata with
  retention and availability. Recording needs a human or automation identity
  allowed to record artifacts.
- `status` (GET): platform status snapshot, `admin` role only.

Actor checks use `WebAuthIdentity` (`IsListenerActor`, `IsTrialServiceActor`,
`CanRecordArtifacts`). Every mutation writes an audit event under a namespaced
idempotency key, so a replay short-circuits.

## Broker operations

`CompanyBroker.h/.cpp` holds the operations. `CompanyBrokerSite` in
`EngineComposition.cpp` adapts them as the broker service `company`. An unknown op
is refused with 404. Each op needs a scope:

| Op | Define | Scope |
|----|--------|-------|
| `create-proposal-draft` | `COMPANY_BROKER_CREATE_DRAFT` | `proposals:write` (the proposal id is derived from a content hash, so a retry names the same proposal) |
| `get-proposal-status` | `COMPANY_BROKER_STATUS` | `proposals:read` |
| `ledger-summary` | `COMPANY_BROKER_LEDGER_SUMMARY` | `ledger:read` |
| `ledger-recent` | `COMPANY_BROKER_LEDGER_RECENT` | `ledger:read`; `admin` adds per-user rows and the subject filter |
| `ledger-balances` | `COMPANY_BROKER_LEDGER_BALANCES` | `admin` |
| `contracts-list` | `COMPANY_BROKER_CONTRACTS_LIST` | `admin` |

The ledger ops are read-only. The ledger is written only inside Company's
workflow.

## Files

| File | Role |
|------|------|
| `Company.upp` | Package file; uses Core, Skylark, WebStorage, WebAuth, WebAccounts, WebEngine |
| `CompanyModel.h` | Record types, role and scope constants (`CompanyAllRoles`, `CompanyAllScopes`) |
| `CompanyStore.h/.cpp` | The store API, implemented on `WebStorage`; secret and config setup |
| `CompanyStoreFixture.cpp` | Deterministic test fixture (test-only) |
| `CompanyWorkflow.h/.cpp` | Lifecycle state machine and mutation rules: proposals, work items, reviews, compensation, administration, token generation (`CompanyGenerateAgentToken`, `CompanyTokenHash`) |
| `CompanyClients.cpp` | Client contracts and projects |
| `PlatformWorkflow.h/.cpp` | Platform v2 control plane (see above) |
| `CompanyBroker.h/.cpp` | Broker operations (see above) |
| `CompanyComposed.h/.cpp` | Host-facing module: `Open`, `Store`, `Handle` |
| `CompanySite.h/.cpp` | Web site and API: identity, routes (`InstallCompanySite`) |
| `CompanyPages.cpp`, `CompanyViews.h` | HTML rendering (plain shell with inline CSS) |

## Differences from the old app, route by route

- Sign-in is the engine's `session/login`. The old `login/submit` form field
  `redirect` is not used.
- The view shell is plain HTML with the original CSS. Icons are text only. The
  navigation is a top bar, not the WebFramework tree.
- Static assets and templates: none (inline CSS).
- The framework spec, the command manifest and the old error envelope are kept.

## Verification

- `upptst/CompanyStoreTests`: headless self-test of this package, on an isolated
  temporary data root. Run `bin/CompanyStoreTests`.
- `upptst/ComposedSite/smoke.sh`: real-process check of the composed sites,
  including the Company module, on temporary ports.
- Build the host with `uppbuilder ManagerSkylark -v`. `CompanyStoreFixture.cpp`
  provides deterministic fixture data for tests.
