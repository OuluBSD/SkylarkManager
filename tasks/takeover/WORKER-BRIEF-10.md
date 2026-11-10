# WORKER BRIEF 10 — from the planner

**Verdict on Brief 09B: accepted — T02 is complete.** Planner re-verified:
12 tab-conversion commits, 0 inline HTML tags in the four tab files, 30
templates, fence 156/156, Manager PASSED (CLANG+GCC), Company ALL PASSED
(CLANG) + GCC rc 0, tree clean at 555c11a.

---

## Assignment 10 — T02 gap fixes + T03: Science authoring wiring

**Scope (one commit per step, all suites green per commit):**

1. **T02 gap fixes** (the three recorded in AGENTS.md):
   - Editor script: owner/repo must be JS-escaped server-side in the data
     model (same decision as 09B — no raw interpolation into scripts).
   - Marketplace Insert button: fix the invalid onclick.
   - TPP topic loops: add fixture coverage to the capture set (the loops
     currently have no fixture exercising them) — one fixture repo/doc that
     renders a multi-topic TPP doc; normalized-identical gate.
2. **Science authoring endpoints** (in `uppsrc/Science`, routes installed by
   `InstallScienceSite`):
   - `POST api/papers/*/article` (or PUT) — body = article tree JSON;
     validates via `ScienceCorpus::SaveArticleTree` (schema + optimistic
     expected_paper_revision → 409 on drift); atomic sidecar write.
   - Authorization, deny by default: caller must have an engine session
     whose account holds the Science author role, OR a broker grant with
     scope `science.author` (meta-account consent flow). CSRF/origin fence
     is already engine-enforced for mutations.
   - Minimal authoring form (JSON textarea acceptable) linked from the
     paper page, gated by the same authorization.
3. **`ScienceService : IBrokerService`** registered by the Science host:
   ops `list-papers`, `get-paper`, `get-article`, `put-article` (scope-
   gated like the endpoint); this is the surface TaskFoundry (T04) will
   consume.
4. **Real corpus read smoke**: a headless check (CLI flag or test mode)
   pointing `ScienceCorpus` at `~/Dev/AnthologyPrivate/site/data` asserting
   ListPapers returns the real corpus (≥150 records expected; write access
   NOT exercised against the real dir in tests — fixture roots only).
5. **Fence suite additions**: a Science engine site in the test host
   (fixture corpus): author without grant/role → 403; with authorized
   session → saved + rendered; drift → 409; metadata-only paper still
   renders as metadata-only.

**Out of scope:** TaskFoundry (T04), PrivateHub UI + engine-host
composition (T04), SameSite on other cookies, corpus writes to the real
directory from tests, crawling/conversion.

**Definition of done:** the three T02 gaps fixed and covered; authoring
endpoints live with deny-by-default auth, drift detection and atomic
writes, covered by fence + `--test`; ScienceService registered; real-corpus
read smoke passes headlessly; all three suites green per commit (CLANG),
GCC rc 0 for all three targets; log entry with commit ids; then STOP and
wait. After acceptance T03 is complete; next brief: T04 (TaskFoundry +
PrivateHub UI + host composition) — the largest remaining slice.
