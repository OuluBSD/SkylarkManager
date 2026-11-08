# WORKER BRIEF 03 — from the planner

**Verdict on Assignment 2: accepted.** Deviations approved: the new issue
check belongs in ManagerSkylark `--test` (the fence suite cannot link the
app package); no `ManagerTypes.h` was needed. Route-capture methodology
(baseline-vs-baseline control) was correct. Planner re-verified: fence
116/116, Manager PASSED (CLANG+GCC), Company ALL PASSED, GCC builds of all
three targets clean, tree clean at 7b62973.

---

## Assignment 3 — T01 slice 3: finish the GitHub adapter surface, remove the dead mirror, honesty pass

**Scope (one mechanical commit per step, all suites green per commit):**

1. **Extend `GitHubForgeBackend` with PR, repo-list, and actions operations**
   (all argv-only via `gh`, same GhRun path, remote-or-unavailable
   provenance, stdin/body via `-f`/`-F` argv entries — never shell):
   - PRs: list (`gh api repos/o/r/pulls`), view (`.../pulls/N`), diff
     (`gh pr diff N -R o/r` — plain text result), create
     (`gh api -X POST repos/o/r/pulls -f title= -f head= -f base= -f body=`),
     close/reopen (`-X PATCH ... -f state=`), comment
     (`-X POST .../issues/N/comments -f body=`), merge
     (`-X PUT .../pulls/N/merge -f merge_method=merge`).
   - Repos: `gh repo list --limit 100 --json name,owner,...` (or
     `gh api user/repos`).
   - Actions runs: `gh api repos/o/r/actions/runs?per_page=20`.
   - Extend `ForgeCore` result types as needed (a `ForgePullRequest` and a
     generic text-result are fine). Keep mapping defensive (Value casts).
2. **Replace the remaining `sh -c` call sites in the GitHub module** with
   the adapter: PR list, detail, diff, create, merge, close, comment;
   `GetLiveGitHubRepos`; `GetLiveActions` (GitHubPlatform.cpp ~11 sites,
   GitHubActions.cpp 1). The 16 sites in `HostPlatform.cpp` and the 2 in
   views are **out of scope** (separate privilege boundary, later slice).
3. **Remove the dead `_prs.json` mirror writes** in
   `uppsrc/ManagerSkylark/GitHubPlatform.cpp` (all `GetHomeDirFile(".aria/
   sites/github")` + `*_prs.json` sites). Nothing reads the file anymore;
   do not delete existing user files on disk — only stop writing. If a test
   referenced the mirror, re-point it at the adapter with the fake gh.
4. **Honesty pass on static claims** (non-live hardcoded content presented
   as fact):
   - Security tab "Zero leaked secrets found" → "Secret scan: not
     performed".
   - Repo header / Settings hardcoded stats → remove or mark as
     unavailable placeholders (no fake numbers).
   - New-PR form: keep the `feature/platform-parity` default (harmless
     suggestion), no change required.
5. **Tests**: extend the fake-gh fixture and the ManagerSkylark `--test`
   GitHub checks to cover PR list/view/comment/close and the
   unavailable-state rendering after synthetic-data removal. Fence suite
   must stay 116/116+ (add checks if you touch WebEngine).

**Out of scope:** HostPlatform shell conversion (later), DataStore/RestApi/
ScopedPolicy/ManagerSkylark.cpp splits (slice 4), SourceForge/PrivateHub
wiring, URL/ID changes, candidate-write boundary.

**Definition of done:** zero `sh -c` sites remain in the GitHub module;
`grep -rn '_prs.json' uppsrc/` returns no write sites; all three suites
green per commit (CLANG) + GCC builds; log entry with commit ids and the
new gh-op list; then STOP and wait for the planner.

*Slice 4 preview:* DataStore.cpp (3074) responsibility split; then
RestApi.cpp (keep `CONSOLE_APP_MAIN` in the main package), ScopedPolicy.cpp,
ManagerSkylark.cpp splits; SourceForge/PrivateHub module wiring + local
store migration; HostPlatform argv-only conversion as its own item.
