# WORKER BRIEF 11B — from the planner

**Verdict on Assignment 11A: accepted — T04 slice 1 complete.** Planner
re-verified: fence 179/179, Manager PASSED (CLANG+GCC), Company ALL PASSED
(CLANG) + GCC rc 0, composed-site smoke 17/17 (note: smoke.sh takes the
ManagerSkylark binary as `$1`; it lives in the **Mt** config dir — planner
documented the invocation in the script-header request below). Defect fixes
approved. Raw-URI accessor routed to backlog (BazaarCore additive patch).

---

## Assignment 11B — T04 slice 2: Company dual-use + TaskFoundry + submission via broker

**Scope (one commit per step; every commit gates: fence 179+,
ManagerSkylark --test PASSED, CompanySkylark --test ALL PASSED (CLANG), GCC
rc 0 for all three targets):**

**Step 0 — Science account provisioning + stronger smoke.**
- CLI `--set-science-user <name>` mirroring `--set-privatehub-user`
  (provisions a Science account with the science-author role).
- Extend `upptst/ComposedSite/smoke.sh`: Science session login, then a
  caller-bound science.author authoring write in the real process; document
  the `$1` binary-path invocation in the script header.

**Step 1 — CompanySkylark dual-use.**
- Move `CONSOLE_APP_MAIN` into `main.cpp` guarded by `#ifdef flagMAIN`
  calling `CompanyMain()`; keep the package's `mainconfig` (dual-main
  package exception). Upstream macro is `INITBLOCK`/`INITBLOCK_(Name)`.
- `.icpp` registration unit: `INITBLOCK_(CompanySkylark)` registers a module
  descriptor/factory only; configuration/stores resolve later by the host
  (no static-init ordering assumptions). Verify static-link retention with a
  non-Blitz build once.
- No behavior change standalone; `CompanySkylark --test` stays green.

**Step 2 — Company broker service.**
- `create-proposal-draft`: idempotent by content hash of the submitted
  package (returns the same Company proposal id on retry; records the
  snapshot); `get-proposal-status` (read-only). Review/compensation remain
  internal to Company.
- Registered when composed as a module; in standalone mode the broker is
  simply not exposed (document).
- `--test` coverage in CompanySkylark: idempotency (same hash → same id),
  snapshot immutability, status readback.

**Step 3 — TaskFoundry package (`uppsrc/TaskFoundry`).**
- Engine site module (installer like Science/PrivateHub) + data model in
  site-scoped `WebStorage`: candidates, proposals, evidence links,
  validation results, submissions (envelope-style, revisions).
- Routes: candidate board, proposal editor (minimal HTML acceptable), 
  validation view, submission status.
- Evidence references: Science paper (revision + article node id) via the
  Science broker; PrivateHub branch/fork refs via the PrivateHub service.
- **Submission**: broker call to Company `create-proposal-draft` with
  idempotency key = content hash; stores the returned Company proposal id;
  later TaskFoundry edits never mutate the submitted snapshot.

**Step 4 — Fence additions.** TaskFoundry site composed in the real
ManagerSkylark process; full round trip: candidate → proposal (with Science
+ PrivateHub evidence via broker) → validate → submit → Company proposal id;
duplicate submit returns the same id; grant caller-binding applies to
TaskFoundry's science.author use too.

**Out of scope:** PrivateHub wiki/topic/release authoring UI (Brief 12),
raw-URI accessor, SameSite elsewhere, host composition for Company
standalone (Company module registration is enough for the broker path).

**Definition of done:** all steps committed with gates green; fence
additions green; log entry with commit ids; then STOP and wait. After
acceptance: T04 is complete; next: T05 leftovers, T06 CI, T07 UppSkillz,
Brief 12 backlog sweep.
