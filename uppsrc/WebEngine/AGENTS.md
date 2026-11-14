# WebEngine Package Guide

`uppsrc/WebEngine` is the composable website engine: it hosts multiple,
independently fenced virtual websites inside one Skylark application, each on
its own listener port, with its own accounts, sessions, cookies, storage
namespace, and route tree.

## File Map

| File | Role |
|------|------|
| `WebEngine.h` | Umbrella header |
| `Router.h/.cpp` | `SiteRoute`, `SiteRouter` (per-site route trees; Skylark-style patterns `""`, `a/b`, `a/*`, `a/**`, `:POST` suffix; literal-preferring best match) |
| `Site.h/.cpp` | `EngineSite`: site id, title, external origin, data root, `SiteAccounts`, router |
| `Request.h/.cpp` | `WebRequest`: fenced request context (path args, params, session/user state, JSON/HTML output, session cookie helpers) |
| `Host.h/.cpp` | `WebEngineHost` (SkylarkApp subclass; extra listeners, acceptor threads), gateway `SKYLARK` catch-alls, `WebEngine` singleton (`TheWebEngine`) |

## How fencing works

- **One Skylark app, many listeners.** The stock listener keeps serving the
  host app; `WebEngineHost` starts one acceptor thread group per extra port.
  Each accepted socket is dispatched through the normal Skylark `Http`/`Dispatch`
  machinery after a thread-local listener port is stamped.
- **Gateway catch-alls.** The engine registers `**` (and `**:POST_RAW`,
  `:PUT`, `:PATCH`, `:HEAD`, `:OPTIONS`) catch-alls. Requests not matched by
  stock routes reach the engine, which resolves the site from the listener
  port and dispatches against that site's own router. Two sites can register
  identical paths without colliding (route trees are per-site objects).
- **Sessions are engine-owned.** Each site has its own cookie name
  (`wfs_<site>`), server-side session records stamped with the site id, and
  TTL. The stock Skylark session subsystem is never engaged for engine
  traffic (its cookie name never matches), and engine code never calls
  `SessionSet` -- so no stock session files are read or written.
- **CSRF/origin fence.** State-changing methods (POST/PUT/PATCH/DELETE) are
  rejected (403) when the `Origin`/`Referer` authority does not exactly match
  the site's configured external origin, and when a cookie-authenticated
  request does not carry the session's server-side CSRF token
  (`__wf_csrf__` field or `X-WF-CSRF` header). `POST_RAW` catch-alls are
  deliberate: the engine enforces its own strict fence instead of Skylark's
  form-identity check.
- **Ports are not cookies.** Different ports are different browser origins,
  but cookies are shared across ports of the same host. The site-stamped
  session records and distinct cookie names are what prevent cross-site
  credential reuse; they are not an OS sandbox.

## Paths on composed ports

- **Engine routes live under a literal prefix no stock route claims.** PrivateHub
  uses `privatehub/...`; the session routes use `session/...`. The stock app
  claims `login`, `logout`, `p/*`, `issues` and others, so a composed route with
  one of those first segments is shadowed.
- **Stock routes are not served on engine listeners.** `ManagerSkylarkApp`
  answers any non-gateway stock match with 404, except the home route `/`. This
  is still the policy. The shadowing problem it guarded against is fixed: the
  gateway used to rebuild the path from the route's wildcard arguments, which
  lose literal segments, so a hand-off could serve the wrong page. The gateway
  now takes the path from `Http::GetRequestURI()` (BazaarCore `Skylark/Http.h`,
  the raw request target), strips the query string, and percent-decodes each
  segment. A future change may serve stock routes on engine listeners without
  that shadowing risk. Keep the 404 rule until that is a deliberate decision.

## Known WP1 limitations (by design)

- No per-site static assets yet (stock `static/**` is app-global); asset
  extraction comes with the Witz work package.
- No witz templates per site yet; handlers emit HTML/JSON directly.
- `SameSite` cookie attribute and a raw request-URI accessor require small
  BazaarCore Skylark hooks; WP1 enforces equivalent protection via
  exact-origin checks and does not patch Skylark.
- Remote (TCP) broker transport is an explicit stub.

## Command console

`CommandConsole.{h,cpp}` provides `InstallCommandConsole(EngineSite&)`: a
`GET /console` page that renders the site's manifest commands as links and
CSRF-bearing forms. **It adds no authority**: the forms post to the real
routes, whose own session, role, CSRF and origin fences stay the gate. Only
http commands with wildcard-free paths render as runnable forms; wildcard
paths and broker commands are documentation rows ("use skylarkctl"). Sites
opt in by calling the installer.

## Website templates and theme preferences (task 055)

- `SessionRoutes.cpp` selects `engine-login` from `WebsiteTemplates` for shared
  sign-in rendering and delegates preference attributes/head propagation to
  that package. Theme/density cookie names, allowlists and task-052 same-host
  `postMessage` behavior are preserved there.
- Add shared page shells and styles in `uppsrc/WebsiteTemplates`; do not add
  stylesheet literals or page-shell CSS to this package. Site packages select
  a template by id and provide their page-specific values.

## Content visibility policy (tasks 053/056)

`ContentPolicy.{h,cpp}` holds the model and common request evaluator;
`ContentPolicyRoutes.cpp` holds the admin page and routes. Every policy check is
additional to the site's existing authentication, role, scope, broker, and grant
checks. The evaluator never reads a link token from a URL; malformed policy
stores return 503 and hidden records use 404 or are omitted from collections.

Levels: `public` (everyone), `protected` (named user, group or secret link),
`private` (named user, group or admin), `public-only` (anonymous visitors only).
A group is a role. Record, module, then site is the inheritance order; the first
explicit policy wins. Missing stores use the existing site fallback. Science
uses paper visibility as its fallback; papers without visibility/policy remain
private. Other sites use `public` only after their existing authorization gates.

Storage is versioned `content_policy.json` (version 1), atomically replaced.
Malformed objects, levels, grants, unknown fields and unsupported versions fail
closed. Repair the file and restart to reload it.

The enforced sites install `/policy`, `api/policy*` and manifest commands
`policy-show`, `policy-set`, `policy-grant`, `policy-revoke`, and admin-only
`policy-link`. Anonymous callers get 401 and signed-in non-admins get 403.
Secret-link creation requires an explicitly `protected` scope and a validated
same-site target. The token is shown once as `/policy-access#wfcp=...&to=...`;
the browser strips the fragment from history, then submits the token in the body
of same-origin `POST /api/policy/exchange`. The exchange returns no token and
sets a 15-minute opaque HttpOnly/SameSite=Strict cookie, Secure on accepted
secure transports. The in-memory session is invalidated by process restart;
the fragment link can be exchanged again until its grant is revoked. The server
binds that session to the site, grant scope/resource and stored keyed hash; each
read still applies the effective policy and the site's own authorization gates.
`?key=` and path-token forms are never read. The access page and exchange use `no-store` and
`Referrer-Policy: no-referrer`; destination paths must be local and query-free.
Secure transport means the socket itself reports TLS, or the direct socket peer
matches an explicitly trusted proxy peer and that peer asserts exactly
`X-Forwarded-Proto: https`. `external_origin`, Host, `X-Forwarded-For`, and
`X-Forwarded-Proto` alone do not confer trust. `Http::GetPeerAddr()` must not be
used for this decision because it trusts `X-Forwarded-For`; the implementation
compares the socket peer address directly.

For TLS termination, configure each Manager public origin with
`MANAGERSKYLARK_ORIGIN_<SITE>=https://...` and separately configure
`MANAGERSKYLARK_TRUSTED_PROXY_PEERS` as a comma-separated list of exact IPv4
socket peer addresses (for example `127.0.0.1`). The list defaults empty; an
HTTPS origin does not add to it. It accepts no CIDRs/hostnames and currently
relies on Skylark's IPv4-only peer-address API. The proxy must strip
client-supplied forwarded-proto headers and set one `X-Forwarded-Proto: https`
value itself. Restrict backend listeners/firewall so only configured proxies
can reach them; trusting loopback also trusts any local process able to connect
to that listener. A direct public listener remains untrusted unless its actual
socket is TLS. HTTP direct listeners show no link form and reject
creation/exchange with 403, even when `external_origin` is configured as HTTPS.

Coverage matrix (stable module/record keys and fallback behavior):
- Science: paper list/API, page/API, author page, article API, and home
  aggregates use module `papers` and paper id. The author page checks paper
  visibility before article-tree loading; article sidecars/counts follow the
  parent paper. Hidden papers are 404; unreadable stores hide all papers and
  contribute zero to aggregates.
- Atlas: map list/map page use `maps`, record `<map>-<version>`; element policy
  uses `elements`, record `<map>-<version>:element:<element-id>`. Element and
  road lists, embedded map JSON, element counts, paths and roads are filtered.
  The map index selects its empty state only after filtering/loading visible map
  records, so an all-hidden collection is indistinguishable from no maps.
  Hidden map/element routes return 404; corrupt stores return 503. Edit-role
  checks remain independent.
- PrivateHub: the anonymous root is only the existing sign-in landing. After
  login, workspace uses `workspace`; repositories use `projects`, record
  `<org>/<repo>`; issues/wiki/discussions/files use matching modules and record
  `<org>/<repo>:<issue-id|slug|file-name>`. Lists filter before rendering;
  comments/posts inherit the parent issue/topic. Member authoring checks remain.
- TaskFoundry: anonymous board is only the existing landing. Candidate/proposal/
  submission records use `candidates`, `proposals`, `submissions` and their
  native IDs. Board rows filter before rendering; evidence/snapshots follow the
  parent proposal/submission. Login and approved grant/scope checks run first.
- Channels: anonymous overview is only the existing sign-in landing. Signed-in
  metadata/messages use `channels`, record `<server>/<channel>`. Metadata is
  filtered before rendering; policy runs before shared cache lookup or IPC.
- Financials: anonymous summary is only the existing no-total landing. Authenticated
  summary uses `summary`; finance-role ledger rows use `ledger`, record sequence;
  contracts use `contracts`, record id. Finance role and Company broker scopes
  run first; rows filter before rendering. Company summary is pre-aggregated, so
  any restrictive `ledger` module or record policy suppresses it (404) rather than leaking hidden
  rows through totals. Summary has no row-level policy claim.
- YouTubePlanner remains public with no protected records. MetaAccount is not a
  task-053 data site.

All missing policy files preserve pre-policy behavior through the documented
fallbacks; endpoint tests cover anonymous/authenticated behavior, record grants,
hidden collection entries and corrupt-store 503 responses. Preserve the route
inventory and tests before enabling policy administration on another site.

Generation does not change a paper's visibility; article writes remain subject
to the existing author and paper checks, and article reads follow paper policy.
