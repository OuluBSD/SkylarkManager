# WORKER BRIEF 08 — from the planner

**Verdict on Assignment 7 (T02 slice 1): accepted.** Planner re-verified:
15 asset files under `WebFramework/assets/`, `Template.cpp` at 536 lines,
per-site roots via `SkylarkSiteRoots`/`SiteRootsScope`, fence 152/152,
Manager PASSED (CLANG+GCC), Company ALL PASSED (CLANG) + GCC rc 0. Route-diff
methodology sound. The deploy-assets behavior change is approved — flagged
to the user that production/demo servers need the deploy step after pull.

---

## Assignment 8 — T02 second slice: SameSite re-add + GitHub renderer Witz extraction

**Scope (one commit per step, all suites green per commit):**

1. **BazaarCore: re-add SameSite support** as a proper additive patch
   (same shape as the reverted `f73fb72`: optional `samesite` parameter on
   `SetRawCookie`/`SetCookie`, appended as `; SameSite=...` when set; also
   keep the httponly forwarding in `SetCookie`). The earlier revert was an
   accidental sync, not a design decision — document that in the commit
   message. Patch discipline per `BazaarCore/AGENTS.md` (additive, minimal).
2. **Engine cookies use it again:** WebEngine session cookies carry
   `SameSite=Strict`; add fence checks asserting the attribute on
   `Set-Cookie` (fresh login on both engine ports).
3. **GitHub renderer extraction to Witz.** Move the GitHub webapp's inline
   HTML from its view/render TUs into per-site `.witz` templates rendered
   through the engine's template root (WebEngine renders with witz now that
   templates are site-scoped). Constraints:
   - Behavior-preserving: route-capture before/after diffs on the GitHub
     routes (same normalized methodology; identical after normalization).
   - String-returning render APIs may remain as bridges (`Renderer::
     RenderString` style) where the router isn't witz-native yet — document
     each bridge.
   - Templates listed in the owning `.upp` (category separators), one
     template root per site.
   - No user-data interpolation into unescaped contexts; witz escaping rules
     apply (`$value` escapes; raw only via explicit raw slots).
   - Keep files under the size target; templates are content, not counted
     against the 800-line code rule, but keep them modest and split per view.
4. SourceForge/PrivateHub/Science renderer extraction is **out of scope**
   (Brief 09); T04 work untouched.

**Definition of done:** SameSite committed in BazaarCore (additive patch,
upstream-pristine rule intact) and asserted by the fence suite; GitHub
webapp renders from site-scoped witz templates with route diffs identical;
no user-facing HTML string literals left in the GitHub view TUs (small
exceptions documented); all three suites green per commit (CLANG), GCC rc 0
for all three targets; log entry with commit ids + route-diff stat; then
STOP and wait.
