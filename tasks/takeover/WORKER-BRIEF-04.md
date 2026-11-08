# WORKER BRIEF 04 — from the planner

**Verdict on Assignment 3 (Brief 03): accepted.** Planner re-verified:
zero `sh -c` sites in the GitHub module, zero `_prs.json` references, zero
fixed clean-scan claims; fence 116/116, ManagerSkylark PASSED (CLANG+GCC),
CompanySkylark ALL PASSED, GCC builds rc 0, tree clean at ad3cb09.

**Decisions on your questions:**
1. PR/repo/run ops staying on `GitHubForgeBackend` — approved. Promote to
   `IForgeBackend` only when a cross-forge PR contract actually exists.
2. HostPlatform/HostAdminViews/HostViews shell sites — confirmed deferred.
3. **Convert `TriggerWorkflowRunAsync` now** (`GitHubActions.cpp:696`,
   `p.Start(cmd)`): switch to `LocalProcess::Start2` with an argv vector, and
   label the UI honestly — it triggers a **local build** of a fixed path, not
   a GitHub Actions run ("Trigger local build (ai-upp)"-style text; the
   actions list already shows unavailable). Fixed path, no user data — so a
   mechanical conversion, no policy change.

## Assignment 4 — T01 slice 4a: leftovers + DataStore split

**Step 1 — TriggerWorkflowRunAsync** (see decision 3). One commit.

**Step 2 — Profile page honesty** (`GitHubUserView.cpp`): the invented
"Active GitHub Projects & Kanban Boards" / "Active Sprint" cards go away.
Replace with real data if trivially available from the manager store; else a
neutral "not loaded" placeholder. One commit.

**Step 3 — `is_private` tri-state.** `LocalRepoInfo.is_private` becomes a
tri-state: `-1` unknown (local fallback default), `0` public, `1` private.
Repo header badge renders Private/Public/unknown accordingly. Where a real
source knows the value (config, scraped snapshot), map it; local fallback
stays unknown. Keep JSON key `is_private` compatible (int). Tests for the
three renderings. One commit.

**Step 4 — `DataStore.cpp` (3074 lines) responsibility split**, same package,
behavior-preserving (schema/validation, persistence with lock+atomic+journal,
record/source registration, batch publication/receipts). Target: largest
resulting file under ~800 lines. The self-tests in RestApi `--test` are the
regression net — keep them green. One or two commits.

**Out of scope:** RestApi/ScopedPolicy/ManagerSkylark.cpp splits (slice 5),
HostPlatform shell conversion, SourceForge/PrivateHub wiring, candidate-write
boundary, URL/ID changes.

**Definition of done:** all three suites green per commit (CLANG) + GCC
builds rc 0; log entry with commit ids; then STOP and wait.

*Slice 5 preview:* RestApi.cpp (1241; `CONSOLE_APP_MAIN` stays in the main
package), ScopedPolicy.cpp (973), ManagerSkylark.cpp (920) splits; then
SourceForge/PrivateHub module wiring + the `~/.aria/sites/sourceforge` local
store migration; HostPlatform argv-only conversion; CompanySkylark.upp
trailing-newline cleanup (pre-existing, trivial — fold into slice 5).
