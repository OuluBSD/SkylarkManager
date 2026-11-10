# WORKER BRIEF 12 — from the planner

**Verdict on Assignment 11B: accepted — T04 is complete.** Planner
re-verified: fence 179/179; composed smoke 40/40 including the full round
trip (TaskFoundry submit → Company proposal id `prp_...`, duplicate submit
returns the same id and the same Company id, anonymous submit refused);
Manager PASSED (CLANG + GCC with `-a ../CompanySkylark/uppsrc`); Company
ALL PASSED (CLANG) + GCC rc 0; CompanySkylark independence confirmed; tree
clean at 25cb7ce.

**Decisions on your questions:**
1. TaskFoundry makes **no science.author writes** — confirmed: it consumes
   Science read-only (`science.read`); article authoring stays Science-side.
   The existing Science caller-binding tests cover the machinery.
2. Interim blanket TaskFoundry scopes: accepted interim; caller-bound
   grants replacing blanket scopes is a recorded backlog item.
3. Build change accepted: all future ManagerSkylark builds (and CI) need
   `-a ../CompanySkylark/uppsrc`. The planner reproduced the cold-cache
   GCC failure without it and verified the fix.

---

## Assignment 12 — T05 Company security gaps + T06 CI updates

**Scope (one commit per step; every commit gates: fence 179+,
ManagerSkylark --test PASSED (with the CompanySkylark -a), CompanySkylark
--test ALL PASSED (CLANG), GCC rc 0 for all three targets):**

1. **T06 CI updates** (`.github/workflows/ci.yml`):
   - ManagerSkylark build steps need the CompanySkylark checkout and
     `-a ../CompanySkylark/uppsrc` (the new dependency).
   - CompanySkylark: add build + `--test` jobs (all platforms; MSVC compile
     coverage; run on POSIX).
   - Keep the WebEngineTests steps as-is unless the assembly list needs the
     same -a (verify).
2. **T05 Company security gaps** (from the original T05 list, still open):
   - `JsonStore` validates `schema_version` but not `platform_version` —
     validate it (CompanyModel.h: Platform v2 records stamp
     `schema_version:1 + platform_version:2`; the store must check
     `platform_version` per collection policy).
   - JSON temp files are created 0644 pre-umask (JsonStore.cpp) — tighten
     to 0600 for record temp files and the `secret.key` path.
   - `secret.key` initialization bypasses the lock/atomic-write mechanism —
     bring it under the same discipline (lock + tmp + fsync + rename), and
     make concurrent first-startup deterministic.
   - ID-generator doc mismatch (header says 16 hex chars, code makes 32) —
     fix the doc or the code, pick one and document.
   - Company `--test` coverage for each fix.
3. **Backlog record only (no work):** caller-bound grants replacing
   TaskFoundry's interim blanket scopes; raw-URI accessor; git-subcommand
   allowlist tightening; idempotency-store expiry + multi-process mutex;
   PrivateHub wiki/topic/release authoring UI (Brief 12 backlog file or
   AGENTS backlog section).

**Out of scope:** T07 UppSkillz (separate), TaskFoundry UI polish, GitHub
renderer changes.

**Definition of done:** CI updated (CompanySkylark jobs + CompanySkylark -a
for ManagerSkylark); the four Company gaps fixed with `--test` coverage;
all three suites green per commit (CLANG), GCC rc 0; log entry with commit
ids; then STOP and wait. After acceptance: T05 and T06 are complete;
remaining plan: T07 UppSkillz + Brief 13 backlog sweep.
