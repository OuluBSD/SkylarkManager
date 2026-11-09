# WORKER BRIEF 09 — from the planner

**Verdict on Assignment 8 steps 1–2: accepted.** Planner re-verified:
BazaarCore carries `samesite` again (Http.h:151/155, alongside other
parties' merged TUI work), fence 156/156 with the SameSite+HttpOnly
assertions on both engine ports, Manager PASSED, Company ALL PASSED.

**Decisions on your step-3 questions:**
1. **Option (a) approved** — the GitHub webapp renders through the app's
   default template root now (`SkylarkSiteRoots` fallback); moving it to an
   engine site happens with T04's host composition. Document the interim
   state in the package AGENTS.
2. Your reading is correct: the views need **witz data models**, not literal
   moves. Each view gets a prepared data model (plain values) + template.
3. **The capture harness comes first** — your recommended order is approved.

## Assignment 9 — T02 third slice: capture harness + GitHub tab extraction begins

**Scope (one commit per step, all suites green per commit):**

1. **Capture harness** (reusable, committed): a script/CLI mode that boots a
   headless ManagerSkylark against a temp data root + fixture repos + the
   fake gh, captures a defined route set to files (normalized on compare:
   drop Date/Set-Cookie, normalize usr_* ids and timestamps). Commit the
   harness + fixtures; captures themselves stay regenerable (tmp/). Define
   the initial GitHub route set (~15-20 routes covering all tabs).
2. **Baseline capture** for the chosen routes, committed as a documented
   regeneration command (not as blob files — regenerate + diff at gate
   time). Log the normalization rules.
3. **Extract `GitHubRepoTabs` first** (commits tab — git-backed, self-
   contained): witz data model + template via the app's default template
   root; normalized-identical captures; document any string-return bridge.
4. **Extract the remaining GitHub tabs one per commit** (pulls, issues,
   code, user, workflow views, actions) in whichever order the harness
   makes cheapest; same rules per tab.

**Out of scope:** engine-site composition (T04); SourceForge/PrivateHub/
Science renderer extraction (Brief 10); SameSite on non-session cookies;
candidate-write boundary.

**Definition of done:** harness committed and used for every extraction
commit; all GitHub tab HTML rendered from witz via the app default template
root; captures normalized-identical; no user-facing HTML string literals
left in the GitHub view TUs (documented exceptions only); all three suites
green per commit (CLANG), GCC rc 0 for all three targets; log entry with
commit ids + per-tab capture status; then STOP and wait.
