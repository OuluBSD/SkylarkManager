# T02 — WP5: Witz/JS/CSS extraction + per-site static

## Goal

Move inline HTML/CSS/JS out of C++ string literals into `.witz`/`.css`/`.js`
files with explicit deployment, and give engine sites their own template and
static namespaces.

## Preconditions

- T01 merged (module layout stable).
- BazaarCore Skylark hooks are permitted (additive, verify with pristine
  upstream dry-run if `patches/` changes; note `--make-patches` cannot
  capture root-file Core changes — only per-package).

## Work items

1. **Per-site template resolution**: `Http` template lookup
   (`GetTemplateDir`/`GetViewVar` paths) is app-global. Add a small Skylark
   hook: engine gateway sets the template/static root from the resolved site
   before dispatch (e.g. thread-local override consulted by
   `App::TemplateDir`/static file handler), or route static serving through
   the engine (preferred: engine-owned `static` catch-all is NOT possible
   while stock `static/**` exists app-global — a minimal Skylark patch in
   `BazaarCore/uppsrc/Skylark` for site-scoped static roots is the honest
   fix; keep it additive and covered).
2. **Extract WebFramework assets**: `PageBuilder`/`Template.cpp` inline CSS/JS
   (`uppsrc/WebFramework/Template.cpp:132–260`) → `.css`/`.js` files listed
   in `WebFramework.upp` with category separators; DOM ids/classes and script
   init order preserved (fence test + WebShowcase examples must render
   identically).
3. **Extract app templates**: GitHub/SourceForge/PrivateHub renderers' inline
   HTML → `.witz` templates per site (keep `Renderer::RenderString` compat
   for string-returning APIs). Navigation keeps using `Navigation.h` model.
4. **Asset deployment is explicit**: Skylark copy recipes are
   `custom(_NEVER)` — do not assume listing deploys files. Define install/
   search roots per site; cache identity must include site root (template
   cache is app-global).
5. **MIME + caching**: engine static serving sets correct Content-Type by
   extension and reasonable cache headers (version query or ETag).
6. Escape discipline: witz `$value` HTML-escapes; raw HTML only through
   explicit raw slots (`$post_identity`-style) — audit every converted route.

## Acceptance

- No user-facing HTML/CSS/JS string literals in GitHub/SourceForge/PrivateHub/
  WebFramework renderers (small exceptions allowed with a comment).
- Fence suite extended: two sites with same-named templates/assets resolve to
  different files; cache identity isolated per site.
- Examples (`SkylarkGuiExample`, `WebShowcase`) still render correctly;
  `--test` suites green; CI (incl. MSVC compile) green.

## Risks

- Skylark template cache is global: forgetting the site in the cache key
  cross-contaminates sites (the fence test must assert this).
- Witz editor support exists upstream (`CodeEditor/RegisterSyntax.cpp`) — no
  tooling work needed.
