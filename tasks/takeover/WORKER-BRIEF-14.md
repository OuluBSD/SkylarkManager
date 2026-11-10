# WORKER BRIEF 14 — from the planner

**Verdict on Assignment 13 (backlog sweep): accepted — the code plan is
complete.** Planner re-verified: BazaarCore `4d8d833` (`Http::GetRequestURI`,
consumed by the gateway at WebEngine.cpp:119), all five item commits, fence
179/179, Manager PASSED (CLANG+GCC), composed smoke 61/61 rc 0, Company ALL
PASSED + GCC rc 0, tree clean at edf135f.

**Residual decisions:**
1. Grant consent stays operator-CLI interim; user-facing consent (compose
   MetaAccount into the host) is recorded in BACKLOG.md.
2. Windows LockFileEx runtime validation: recorded — compile-only coverage
   until a Windows runner can execute tests; POSIX is the supported path.
3. The missing member-role denial test: YOUR step 0 below.

---

## Assignment 14 — T07 UppSkillz (final plan item) + step-0 micro-fixes

**Step 0 — micro-fixes (one commit):**
- Add the missing fence check: a logged-in PrivateHub account WITHOUT the
  member role is refused the authoring endpoints (the handler already
  refuses it; exercise the denial).
- Record in BACKLOG.md: user-facing grant consent (MetaAccount composition)
  and Windows LockFileEx runtime validation (both already decided above —
  just ensure they are entries with sources).

**Step 1 — UppSkillz bootstrap.** Create the repository:
- Prefer `gh repo create OuluBSD/UppSkillz --public` from the local
  `~/Dev/UppSkillz` working repo (coordinate: if GitHub credentials/org
  permissions are unavailable, stage the local repo fully and note that the
  push awaits the user).
- Structure: `skills/<skill-id>/SKILL.md` (frontmatter name+description,
  <500 lines, relative references), `ai/rules|skills|info/*.md` (ecosystem
  method), `HISTORY.md` (per-item provenance from ai-upp: source path,
  review verdict, date).

**Step 2 — first curated batch.** Review-and-migrate in this order, one
commit per skill group:
- U++ fundamentals: containers/ownership (pick-only + `clone()` copy
  idiom), `Value`/Jsonize (`StoreAsJsonValue` typed-format trap,
  `AsJSON` for maps, `LoadFromJsonValue`), `ValueMap::operator[](int)` is a
  KEY lookup (positional = `GetValue(i)`), `Vector`/`VectorMap` move-only
  rules, `StaticMutex` member trap (OnceFlag uninit pre-C++20), `Mutex`
  members.
- Build/package: `mainconfig` rules (MT obsolete — Core defines
  multithreading unconditionally; mainconfig only for dual-main packages
  guarded by `flagMAIN`), `INITBLOCK`/`INITBLOCK_(Name)`, `.upp` category
  separators, assembly roots and `-a` multi-repo builds (the
  CompanySkylark/ManagerSkylark example).
- Web: Skylark/witz (loop-tag line joins, `$var` before `$else`/`.`,
  trailing `$endif` blank line, `$raw(...)` for trusted static content
  only, escaping by default), engine fence model (exact-origin + server-
  side CSRF, site-stamped sessions, POST_RAW caveat), `TcpSocket::Connect`
  EINPROGRESS, zero-wait accepted-socket reads (set a read timeout after
  accept), console test buffering (`setvbuf`/flush before `_Exit`).
- Sources: `/home/sblo/Dev/ai-upp/ai/**` (historical, non-binding,
  provenance-required), the current repos' `ai/**`, the worker-log findings,
  and `tasks/takeover/T00-context-and-rules.md`.
- Stale guidance fixes are mandatory per item (MT-required, bin/build
  monorepo paths, automatic asset-copy claims, POST_RAW CSRF caveat).

**Step 3 — repo-local pointers.** `SkylarkManager/ai` and
`CompanySkylark/ai` gain a short pointer file to UppSkillz for the generic
material (repo-specific policy stays local). Both repos stay green.

**Out of scope:** domain-reference migration beyond the first batch
(follow-up), code changes beyond step 0, editing `ai-upp/ai` (other agent's
territory).

**Definition of done:** UppSkillz repo exists (pushed, or staged locally
with the push blocker documented); skills/<id>/SKILL.md batch valid
(frontmatter + relative refs); provenance recorded; stale guidance fixed;
step-0 test landed; all three suites green per commit (CLANG), GCC rc 0;
log entry with commit ids/repo URL; then STOP and wait.

**After acceptance: the takeover plan is COMPLETE** — the planner will
record final completion.
