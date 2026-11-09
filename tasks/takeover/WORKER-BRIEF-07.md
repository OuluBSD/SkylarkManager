# WORKER BRIEF 07 — from the planner

**Verdict on Assignment 6A: accepted — T01 is complete** (module split,
fenced engine, forge adapters wired, meta-account app, migration converter,
PrivateHub broker service). Planner re-verified the full matrix: fence
135/135, Manager PASSED (CLANG+GCC), Company ALL PASSED (CLANG) + GCC rc 0,
tree clean at e553db3.

---

## Assignment 7 — T02 first slice: per-site templates/static + WebFramework asset extraction

The BazaarCore foundation is merged and stable — the Skylark hooks this
needs are now unblocked. Keep BazaarCore changes **additive and minimal**
(surgical-hooks rule; see BazaarCore/AGENTS.md).

**Scope (one commit per step, all suites green per commit):**

1. **BazaarCore Skylark hook — per-site template and static roots.** Two
   small additive seams, both defaulting to today's behavior:
   - Template resolution: a thread-local override (or virtual on SkylarkApp)
     consulted by the template/render path, set by the engine gateway before
     handlers run. Default unset → current app-global root.
   - Static serving: the stock `static/**` handler currently resolves
     against the app-global static dir and would capture engine sites'
     `/static/...` paths. Add a consultable override (same pattern) that the
     engine sets per request; engine sites get site-scoped static roots with
     path containment.
   - Cache identity: the template cache must include the site root (two
     sites with same-named templates must never share cached renders — add
     a fence check for this).
2. **Engine static serving.** Engine gateway serves site static under a
   defined convention, with Content-Type by extension, path containment, and
   ETag or versioned caching. No execution of uploaded content; read-only.
3. **WebFramework asset extraction.** `PageBuilder`/`Template.cpp` inline
   CSS/JS → real `.css`/`.js` files listed in `WebFramework.upp` with
   category separators. DOM ids/classes and script init order preserved.
   Rendering must be byte-identical: capture key routes before/after and
   diff (same methodology as the RenderRepoView refactor). Asset deployment
   must be explicit — remember Skylark copy recipes are `custom(_NEVER)`.
4. **Fence additions:** two engine sites with identical template/static
   names resolve to different content; cached renders are site-scoped;
   static containment (no escape via ../ or symlinks out of the site root).

**Out of scope:** GitHub/SourceForge/PrivateHub renderer extraction to witz
(Brief 08), SameSite re-add, Company-side work, T04 work.

**Definition of done:** BazaarCore hooks committed additively with default
behavior unchanged (upstream pristine rule intact); no inline CSS/JS string
literals in WebFramework; fence additions green (135+new); all three suites
green per commit (CLANG), GCC rc 0 for all three targets; examples
(`SkylarkGuiExample`, `WebShowcase`) render identically; log entry with
commit ids and the route-diff stat; then STOP and wait.

**Risks:** template cache is app-global — the fence check for site-scoped
caching is mandatory; the stock static handler must keep working for
non-engine sites byte-for-byte.
