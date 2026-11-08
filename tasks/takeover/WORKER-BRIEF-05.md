# WORKER BRIEF 05 — from the planner

**Verdict on Assignment 4 (Brief 04): accepted.** Planner re-verified: the
four commits as reported; `DataStore.cpp` split sizes exact (largest 636);
`TriggerLocalBuildAsync` argv-only confirmed (`GitHubActions.cpp:701`); the
invented profile/heatmap claims gone (the extra heatmap removal was in-class
and is approved); `RenderOperatorWarningBar` honesty rewrite approved; fence
116/116, Manager PASSED (CLANG+GCC), Company ALL PASSED, GCC rc 0 everywhere.

---

## Assignment 5 — T01 slice 5: test the DataStore hard paths, finish the big-file splits

**Scope (one mechanical commit per step, all suites green per commit):**

1. **Direct tests for the DataStore hard paths** (the gap you flagged — do
   this FIRST, it de-risks the mechanical split you already made): self-tests
   exercising `PublishBatch`, `WriteRecord`, `RecoverPendingBatches`, and the
   journal round-trip — including a crash-recovery simulation (kill/abandon a
   batch mid-write via a crafted journal state file, then recover and assert
   idempotency and no data loss). These live with the existing `--test`
   suite. If the journal format makes crafted state awkward, test via the
   public API and a real temp data dir; a private-friendship header for tests
   is acceptable if documented.
2. **`RestApi.cpp` (1241 lines) split** by route area (health/projects/tasks/
   chat/sweagent/api-gh/...). `CONSOLE_APP_MAIN` **stays in the main package**
   (it may remain in `RestApi.cpp` or move to a `ManagerSkylarkMain.cpp` —
   either is fine, it must not move into a library). Handler IDs/URLs
   unchanged.
3. **`ScopedPolicy.cpp` (973 lines) split** by responsibility (identity
   resolution, ancestry checks, external-model grants, publication grants).
4. **`ManagerSkylark.cpp` (920 lines) split** (app setup, site registration,
   filter hooks, shutdown) — after this, no non-test file in the package
   exceeds ~800 lines except noted exceptions (list any remaining with a
   reason in the log).
5. **Trivial cleanup folded in:** the pre-existing
   `uppsrc/CompanySkylark/CompanySkylark.upp` trailing-newline change in the
   CompanySkylark repo — commit it there as a one-line cleanup commit on your
   way (`git commit -m "CompanySkylark.upp: trailing newline" --
   uppsrc/CompanySkylark/CompanySkylark.upp`).

**Out of scope (slice 6):** SourceForge/PrivateHub module wiring + the
`~/.aria/sites/sourceforge` local-store migration; HostPlatform argv-only
conversion (16 sites); T02+ work.

**Definition of done:** new DataStore hard-path tests exist and pass; no
non-test file in ManagerSkylark over ~800 lines without a logged reason; all
three suites green per commit (CLANG), GCC builds rc 0, CompanySkylark
GCC rc 0; log entry with commit ids; then STOP and wait.
