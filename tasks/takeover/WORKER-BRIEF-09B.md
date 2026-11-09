# WORKER BRIEF 09B — from the planner (Brief 09 continuation)

**Verdict on Brief 09 steps 1–3: accepted.** Harness, fixture, normalization
and the commits-tab extraction verified (captures identical, 42 files);
fence 156/156, Manager PASSED, Company ALL PASSED. Your witz findings
(loop-tag line joins; `$c.msg` member access) are recorded for UppSkillz.

**Decisions on your step-4 blockers — no engine changes needed:**

1. **JS-string interpolation (owner/repo inside `<script>`)**: escape
   server-side. The view prepares JS-safe literals in the data model
   (e.g. `js_owner` = quoted/escaped string), the template outputs them
   plainly. Do NOT add a witz JS filter.
2. **VividIcon SVG icons**: pre-render each icon to its SVG string in the
   data model and pass through the explicit raw slot (`$raw(...)`) — the
   icon set is static and trusted; document that raw slots are for
   planner/trusted content only, never user data.
3. **Elements emitted before the availability check**: wrap in `$if`
   guards in the template — same rendering as the C++ conditionals.

## Assignment 9B — finish Brief 09 step 4

**Scope (one commit per tab, all suites green per commit):**

1. **issues** tab using the three decisions above.
2. Then, in this order (cheapest first, one commit each): **pulls**,
   **code**, **user**, **workflow views**, **actions**, **projects**,
   **security**, **settings**, **agents**.
3. Every tab: witz data model + template via the app default template root;
   normalized-identical captures (harness regen + compare as the gate);
   documented bridges only where string-return APIs remain.

**Out of scope:** engine-site composition (T04); SourceForge/PrivateHub/
Science renderer extraction (Brief 10); SameSite elsewhere; new witz
language features.

**Definition of done (completes Brief 09):** all GitHub tabs render from
witz via the app default template root; harness captures normalized-
identical for every converted tab; no user-facing HTML string literals left
in the GitHub view TUs (documented exceptions only); all three suites green
per commit (CLANG), GCC rc 0 for all three targets; log entry with commit
ids + per-tab capture status; then STOP and wait. After acceptance, T02 is
complete and the next brief is T03 (Science authoring wiring).
