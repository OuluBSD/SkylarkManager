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

## Content visibility policy (task 053)

`ContentPolicy.{h,cpp}` holds the model; `ContentPolicyRoutes.cpp` holds the
admin page and routes. Each data site opts in with `InstallContentPolicyRoutes`.

Levels: `public` (everyone), `protected` (named user, group or secret link),
`private` (named user, group or admin), `public-only` (anonymous visitors only).
A group is a role. Visibility is `ContentVisible(effective, principal)`.

Attachment: record, then module, then site. The first explicit policy in that
order wins (`ContentResolve`); with none, the fallback applies. Sites pass their
own fallback: Science uses the paper's `visibility` field, and a paper with no
policy and no field is private.

Storage: `content_policy.json` in the site data root, versioned (`version: 1`),
saved through a temporary file and a rename. A link token is shown once when it
is created and stored only as `ContentLinkHash` (a keyed hash). Tokens are never
logged or shown on the settings page.

Admin surface: `/policy` (page), `api/policy*` (JSON), and manifest commands
`policy-show`, `policy-set`, `policy-grant`, `policy-revoke`, `policy-link`.
Anonymous callers get 401; signed-in non-admins get 403.

Per-site map (anonymous view):
- Science: list, count, paper, article API honour paper visibility; a hidden
  paper answers as not found.
- Atlas: map list, map view, element view are public reads; edits need admin.
- PrivateHub, TaskFoundry, Channels, Financials: public landing with a sign-in
  link; their data routes need sign-in (workspace, grants, channel views,
  ledger, contracts).
- YouTubePlanner: public.
- MetaAccount: root redirects to its sign-in page (the landing).

Generation: writing a generated article never changes a paper's visibility. The
article is served only when the paper's own policy allows the viewer. New content
without a stated policy is private.
