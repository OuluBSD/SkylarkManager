# WORKER BRIEF 01 — from the planner (takeover agent)

**Role setup (per user):** the planner (takeover agent session) writes the
plan, work orders and acceptance criteria; the worker executes them. When the
worker's capacity ends, this arrangement stops and remaining tasks stay in
`tasks/takeover/`.

**How to report:** append a dated entry to `tasks/takeover/WORKER-LOG.md`
after each assignment or when blocked: what was done (commit ids), test
results (exact commands + outcome), open questions. The planner reads the log
and issues the next brief.

**Standing constraints (from T00 — binding):**
1. Commit with explicit pathspecs (`git commit -m ... -- <paths>`); never
   `git add -A`; never commit files you did not intend.
2. After every assignment: fence suite
   (`./bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/WebEngineTests`,
   exit 0) + `./bin/ManagerSkylark --test` PASSED + CompanySkylark
   (`uppbuilder -m CLANG -a ../SkylarkManager/uppsrc CompanySkylark -v`;
   `./bin/CompanySkylark --test`) ALL PASSED, plus a GCC build of the touched
   target before finishing.
3. Do not weaken fences (site-stamped sessions, exact-origin + CSRF,
   provenance labels, candidate writes stay disabled). `upptst/WebEngineTests`
   is the executable specification.
4. Read `tasks/takeover/T00-context-and-rules.md` first (U++ traps + commands).

---

## Assignment 1 (now)

**Step 0 — Land the first agent's finished work.**
The first agent retired leaving finished, *staged but uncommitted* work in
`uppsrc/ManagerSkylark/` (Assessment module, DataViews, ManagerSkylark
app/upp, RestApi changes; worktree state is authoritative — the tree built
and tested green with it: ManagerSkylark PASSED, fence 116/116).

- `git add uppsrc/ManagerSkylark && git commit -m "Assessment module (first-agent handover): staged work landed" -- uppsrc/ManagerSkylark`
- Re-run the three suites (constraint 2) and record results in WORKER-LOG.

**Step 1 — T01 slice 1: behavior-preserving split (from
`tasks/takeover/T01-module-split-and-host.md`).**

Do ONLY slice 1 now:
1. Break the umbrella-header cycle: `GitHubPlatform.h` / `SourceForgePlatform.h`
   must stop including `ManagerSkylark.h` (move shared bits into small
   domain headers). No behavior change.
2. Move `SKYLARK(...)` handler registration into dedicated, conspicuous
   handler files per app (GitHub / SourceForge / Host-System), keeping route
   IDs and URLs byte-identical. Handlers may stay in the executable package
   for now (route retention in static builds is the known hazard).
3. Split `GitHubPlatform.cpp` / `SourceForgePlatform.cpp` / `HostPlatform.cpp`
   by responsibility (transport vs domain vs rendering), same package,
   same behavior. Keep files under ~800 lines.
4. One commit per mechanical step; every commit must keep all three suites
   green (constraint 2).

Do NOT yet: wire ForgeCore/GitHubForge adapters, change URLs, touch the
candidate-write boundary, or reformat code beyond the split.

**Definition of done:** suites green; log entry with commit ids; then STOP
and wait for the planner's next brief.

---

*Planner: takeover agent. Next brief follows after WORKER-LOG entry for
Assignment 1.*
