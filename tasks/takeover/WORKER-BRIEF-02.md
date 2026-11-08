# WORKER BRIEF 02 — from the planner

**Verdict on Assignment 1 (recorded in WORKER-LOG): accepted.** The two
unstaged edits swept into `1af250c` are planner-approved to keep: removing
deprecated `MT` from generated workflow templates (GitHubPlatform.cpp) is on
plan; the `AssessmentViews.h` include in DataViews.cpp is required by the
landed module. No rework.

Verification performed by planner: fence 116/116 (CLANG + GCC),
ManagerSkylark PASSED, CompanySkylark ALL PASSED, clean tree, route IDs
preserved, file sizes as reported.

---

## Assignment 2 — T01 slice 2 (same constraints as T00/Brief 01)

**Scope (do exactly this; one mechanical commit per step):**

1. **Finish the header-cycle break.** `SystemAuth.h` and `HostPlatform.h`
   still `#include "ManagerSkylark.h"`. Move the types/functions they need
   into small focused headers (e.g. `ManagerTypes.h` for shared domain
   types); app-header includes remain only in .cpp files that truly need
   them. Behavior-preserving.
2. **Refactor `GitHubRepoView.cpp`.** The 1080-line `RenderRepoView` becomes
   per-section functions (header/tabs/files/commits/...) composed by a short
   orchestrator. **Rendered output must be byte-identical**: before touching
   it, capture the rendered HTML of a fixed set of repo routes (run the
   server, `curl` each route into files under `tmp/`), re-capture after, and
   diff. Include the before/after diff summary in the log (no full HTML in
   the log, just the route list + diff stat).
3. **Wire the GitHubForge adapter for live issue operations.** Replace the
   `sh -c`-interpolated `gh` issue calls in the GitHub service with
   `GitHubForgeBackend` (argv-only; inject gh path via the existing pattern,
   `GHFORGE_GH` env respected). Rules:
   - Provenance remote-or-unavailable; **remove synthetic fallback data**
     from the live paths (no fabricated PR #7/diff content presented as
     remote state) — keep existing UI showing an explicit "unavailable"
     state instead.
   - Route URLs and IDs unchanged; `--test` must stay green.
   - Extend `upptst/WebEngineTests` with a check that the manager's GitHub
     issue listing uses the adapter contract (reuse the fake-gh fixture
     pattern), if reachable without network.
4. **Out of scope for this brief:** RestApi/DataStore/ScopedPolicy/
   ManagerSkylark.cpp splits (slice 3), candidate-write boundary, URL or
   handler-ID changes, ForgeCore/SourceForge/PrivateHub wiring (later slices),
   any T02+ work.

**Definition of done:** all three suites green per commit; route-diff shows
no rendering change from item 2; log entry with commit ids, diff stat, and
the gh-adapter behavior change notes (what synthetic data was removed);
then STOP and wait for the planner.

*Planner note for slice 3 preview (not yet ordered):* DataStore.cpp (3074)
and RestApi.cpp (1241, holds `CONSOLE_APP_MAIN` — it stays in the main
package) are next after the adapter wiring proves out.
