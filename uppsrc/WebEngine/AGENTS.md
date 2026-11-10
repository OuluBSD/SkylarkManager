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
  answers any non-gateway stock match with 404, except the home route `/`. The
  gateway rebuilds the request path from the route's wildcard arguments, and a
  stock route with literal segments loses them, so a gateway hand-off would
  serve the wrong page. Fixing this needs a public raw-URI accessor on
  `Http` in ai-upp (the header is private); until then, keep the rule.

## Known WP1 limitations (by design)

- No per-site static assets yet (stock `static/**` is app-global); asset
  extraction comes with the Witz work package.
- No witz templates per site yet; handlers emit HTML/JSON directly.
- `SameSite` cookie attribute and a raw request-URI accessor require small
  BazaarCore Skylark hooks; WP1 enforces equivalent protection via
  exact-origin checks and does not patch Skylark.
- Remote (TCP) broker transport is an explicit stub.
