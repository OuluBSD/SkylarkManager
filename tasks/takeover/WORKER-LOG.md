# Worker log (second agent; planner = takeover agent)

Format: date — assignment — done (commit ids) — verification results — notes/blockers.

## 2026-11-08 — WORKER-BRIEF-01 (Assignment 1) — done

**Step 0 — land first agent's staged work**
- `1af250c` "Assessment module (first-agent handover): staged work landed" —
  committed with pathspec `-- uppsrc/ManagerSkylark` (9 staged files plus
  two unstaged worktree edits in `AssessmentService.cpp` (2 lines) and
  `GitHubPlatform.cpp` (10 lines), included because the brief treats the
  worktree as authoritative). Flagging in case those two were not meant for
  this commit.

**Step 1 — T01 slice 1 (behavior-preserving, one commit per mechanical step)**
- `0452740` break umbrella cycle: `GitHubPlatform.h` / `SourceForgePlatform.h`
  include `<Core/Core.h>` (+ `<Skylark/Skylark.h>` where they declare `Http`
  renderers) instead of `ManagerSkylark.h`.
- `34253b0` GitHub route handlers → `GitHubRoutes.cpp`
- `1de0c28` SourceForge route handlers → `SourceForgeRoutes.cpp`
- `38b3f1e` Host/Node route handlers → `HostRoutes.cpp`
  (route IDs and URLs byte-identical; handler text moved unchanged)
- `0527577` GitHub renderers → `GitHubUserView.cpp`, `GitHubRepoView.cpp`
- `6b5070b` GitHub service split → `GitHubPlatform.cpp` (738),
  `GitHubActions.cpp` (743), `GitHubWorkflowViews.cpp` (527)
- `e0b26a7` SourceForge service/view split → `SourceForgePlatform.cpp` (714),
  `SourceForgeViews.cpp` (741); `CapitalizeWord` made shared
- `05f3d42` Host service/view split → `HostPlatform.cpp` (659),
  `HostViews.cpp` (431), `HostAdminViews.cpp` (644)

**Verification (CLANG, after every commit; exact commands):**
- `uppbuilder -m CLANG WebEngineTests -v && ./bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/WebEngineTests` → 116/116 checks passed, exit 0
- `uppbuilder -m CLANG ManagerSkylark -v && ./bin/ManagerSkylark --test` → ManagerSkylark verification PASSED, exit 0
- `uppbuilder -m CLANG -a ../SkylarkManager/uppsrc CompanySkylark -v && ./bin/CompanySkylark --test` → ALL TESTS PASSED, exit 0
- `uppbuilder -m GCC ManagerSkylark -v` → rc 0 (final state)

**Notes / open items**
- One failed intermediate build (`HostViews.cpp` needed `DirEntry`,
  `HostAdminViews.cpp` needed `statvfs`) was caught by a build-rc check and
  fixed before commit `05f3d42`. The suites ran only against the green build.
- Still over ~800 lines: `GitHubRepoView.cpp` (1097; one 1,080-line
  `RenderRepoView` function, needs a refactor to split), `RestApi.cpp` (1241),
  `DataStore.cpp` (3074), `ScopedPolicy.cpp` (973), `ManagerSkylark.cpp` (920).
  Only the three platform files were in slice 1's named scope.
- Still not done from T01 item 1: `SystemAuth.h` and `HostPlatform.h` still
  include `ManagerSkylark.h`, so the umbrella cycle is broken only for GitHub
  and SourceForge. `RestApi.cpp` (CONSOLE_APP_MAIN, its own routes) and
  `WebViews.cpp` were not moved (not named in slice 1 step 2 beyond the three
  apps).
- No route-retention check with a non-Blitz (`-bs`) build: no route TU moved
  into a static-library package (all stay in the ManagerSkylark executable).
- Nothing pushed.

Ready for the next brief.

## Planner entry — 2026-11-08 (takeover agent)

Assignment 1 accepted. Independent verification: fence 116/116 (CLANG+GCC),
ManagerSkylark PASSED, CompanySkylark ALL PASSED, tree clean at 24ead1b.
Swept unstaged edits in 1af250c approved (MT removal in generated workflow
templates; AssessmentViews include). Next: WORKER-BRIEF-02.md (T01 slice 2).

## 2026-11-08 — WORKER-BRIEF-02 (Assignment 2, T01 slice 2) — done

**Commits (CLANG suites green after each; GCC rc=0 for ManagerSkylark and WebEngineTests at the end):**
- `bddecdc` headers no longer include the umbrella: `SystemAuth.h` and `HostPlatform.h` include Core/Skylark directly; `HostRoutes.cpp` and `HostAdminViews.cpp` include `ManagerSkylark.h` themselves. No header in the package includes `ManagerSkylark.h` now. No new `ManagerTypes.h` was needed, since the headers only need Core and Skylark types.
- `8962aa2` `RenderRepoView` refactor (route-diff gated, see below): orchestrator `GitHubRepoView.cpp` (1097 → 235 lines), state struct `GitHubRepoPage.h`, per-tab renderers `GitHubRepoCode.cpp` (207), `GitHubRepoIssues.cpp` (202), `GitHubRepoPulls.cpp` (301), `GitHubRepoTabs.cpp` (292). The 1,080-line function is gone. Tab bodies are moved verbatim, one indent level shallower.
- `981a63f` `ForgeCore`: `ForgeIssue::comment_count` (additive; copy, assign, Jsonize). `GitHubForge` maps REST `comments` into it. Other backends default to 0.
- `4505f3f` GitHub issue operations go through `GitHubForgeBackend` (argv-only, `GHFORGE_GH` honored, fresh backend per call). `GetLiveIssues` and `GetLiveIssueDetail` take an optional `String* unavailable`, and the issue views show a warning when it is set. Create, comment and toggle keep their routes: a URL on success (built from the returned ids), `error: ...` on failure. Synthetic self-test `GitHubAdapterTests.cpp` (4 checks) runs from `ManagerSkylark --test` with a fake `gh` script.
- `113a172` live PR and actions paths drop the synthetic data (details below).

**Route-diff gate for item 2 (no rendering change):**
- Captured 19 repo routes (code, tree, blob, commits, issues, issue detail, pulls, PR detail, actions, actions/new, projects, security, settings, agents, unknown tab, and two repos) via curl against a running ManagerSkylark with a temporary local site account (`--site-dir`, `MANAGERSKYLARK_SITE_PASSWORD`). Captures are in the session scratchpad, not in the repo.
- Before = HEAD `bddecdc`, after = `8962aa2`. Two captures of unchanged code differed only in relative ages ("85 seconds ago" vs "2 minutes ago"). Masked-age comparison was identical for all 19 routes before the change.
- Result: 3 of 19 routes differ (code, tree, blob), 8 changed lines in total, all of them relative-age strings. Zero structural differences.

**Gh-adapter behavior change (item 3, intentional, not byte-identical):**
- Removed synthetic data from live paths: the PR #7 sample in list and detail (title, body, author, date, sample comment); the hardcoded PR #7 diff; the local `~/.aria/sites/github/*_prs.json` mirror merged into list and detail; the `1 open / 28 closed` placeholder counts; the `51 / 51 tests passed` and `Successful in 1m 24s` CI box; the `+145 -12` counter; sample head/base branch names (now `(unknown)`); the fixed CACHED/COMPILED/TESTED actions rows (now one explicit empty row, and the badge reads `No runs` instead of `CI Active`).
- Unavailable states are explicit: issues, issue detail, PR list, PR detail and the diff section each show a warning with the diagnostic.
- Live smoke test (real `gh`, network): `SeppoPakonen/SkylarkManager` issues and PR #7 report unavailable (GitHub returns HTTP 404 for that path). `OuluBSD/*` issues and PRs return real empty lists, and the actions tab shows the empty row. No fabricated markers remain on those routes, except the static New-PR option below.
- Test placement: the brief asked for the check in `WebEngineTests`. That suite cannot link the app package, so the check lives in `ManagerSkylark --test`, reusing the fake-gh pattern. `WebEngineTests` already covers `GitHubForgeBackend` over a fake gh (lines ~557–623).
- A U++ trap came up while writing the timestamp formatter: `Format("%04d-%02d-%02d")` does not pad like printf and produced `2026-10-<N`. Fixed with a two-digit helper, and the synthetic check caught it. Worth adding to the T00 traps list.

**Verification (exact commands, after every commit):**
- `uppbuilder -m CLANG WebEngineTests -v && ./bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/WebEngineTests` → 116/116 checks passed, exit 0
- `uppbuilder -m CLANG ManagerSkylark -v && ./bin/ManagerSkylark --test` → ManagerSkylark verification PASSED (including the 4 new GitHub checks), exit 0
- `uppbuilder -m CLANG -a ../SkylarkManager/uppsrc CompanySkylark -v && ./bin/CompanySkylark --test` → ALL TESTS PASSED, exit 0
- `uppbuilder -m GCC ManagerSkylark -v` and `uppbuilder -m GCC WebEngineTests -v` → rc 0. GCC for CompanySkylark was not run.

**Diff stat per commit:** 8962aa2 7 files, +1169/−973; 981a63f 2 files, +6/−2; 4505f3f 6 files, +203/−59; 113a172 4 files, +53/−172; bddecdc 4 files, +6/−3.

**Open items / not done (for the planner):**
- PR create, merge, close and comment still mirror results into `~/.aria/sites/github/*_prs.json`. No read path uses that file now, so it is write-only. Remove in a follow-up.
- Still `sh -c` with interpolated owner/repo: PR list, PR detail, PR diff, PR merge/close/comment, `GetLiveGitHubRepos` and `GetLiveActions`. Only the issue operations moved to the adapter, as the brief specified. These need an argv-based path in a later slice.
- Static fabricated claims in non-live tabs, not changed here: Security tab ("No vulnerable dependencies detected", "Zero leaked secrets found", "Clang-tidy and compiler checks clean"); Settings tab ("This repository is currently private"); repo header ("Watching: 1", "Activity: Daily"); New-PR form options (`feature/platform-parity` pre-selected, `feature/packet-router`, base `master`). The brief limits this pass to live paths, so these need a planner decision.
- Size: every file touched in this slice is under ~800 lines (largest: `GitHubActions.cpp` 743). `RestApi.cpp` and `DataStore.cpp` remain over target and belong to slice 3.
- Nothing pushed. The user's demo server on port 8081 was left running; the capture server on 18090 was stopped.

Ready for the next brief.

## Planner entry — 2026-11-08 (takeover agent)

Assignment 2 accepted. Planner re-verified: fence 116/116, Manager PASSED
(CLANG+GCC), Company ALL PASSED, GCC builds clean, tree clean at 7b62973.
Deviations approved (app-package --test placement; no ManagerTypes.h
needed). Route-capture methodology approved. Next: WORKER-BRIEF-03.md
(GitHub PR/repos/actions adapter ops, dead _prs.json mirror removal,
static-claims honesty pass). HostPlatform shell sites deferred to their own
slice.

## 2026-11-08 — WORKER-BRIEF-03 (Assignment 3, T01 slice 3) — done

**Commits (on main, nothing pushed):**
- `75960be` GitHubForge: pull request, repo-list and run-list operations (argv-only). No callers yet.
- `dcd9ccd` Read paths (repo list, PR list/detail/diff, actions runs) go through the adapter.
- `5f82a91` PR writes (create, merge, close, comment) go through the adapter; `_prs.json` mirror removed; write routes report `status: error` on refusal and the page shows it instead of reloading.
- `c9ed779` `gh auth status` and the three local `git` reads (repo log, branches, commit log) run argv-only; the synthetic test builds a real fixture repo for the git path.
- `bed3033` Honesty pass: security, settings and sidebar stop asserting unread facts; unknown star and fork counts show `n/a` / `not loaded`.

**Gh operations now on the adapter (`GitHubForgeBackend`, all via `GhRun` argv):**
- Pull requests: list `gh api repos/o/r/pulls?state=all&per_page=20`; get `gh api repos/o/r/pulls/N`; diff `gh pr diff N -R o/r` (plain text); create `gh api -X POST repos/o/r/pulls -f title= -f head= -f base= -f body=`; state `gh api -X PATCH repos/o/r/pulls/N -f state=open|closed`; merge `gh api -X PUT repos/o/r/pulls/N/merge -f merge_method=merge`.
- PR comments reuse the issue endpoints (`AddComment`/`ListComments`), since GitHub treats PR conversation comments as issue comments.
- Repositories: `gh repo list <owner> --limit 100 --json ...`.
- Workflow runs: `gh api repos/o/r/actions/runs?per_page=N`.
- Auth: `gh auth status` (now `GhRun`; success is exit code 0, no longer a string match that also matched "not logged in to github.com").
- New types in ForgeCore: `ForgePullRequest`, `ForgeRepo`, `ForgeRun`. Owner, branch and number inputs are validated before argv is built.
- Decision for the planner: these ops live on `GitHubForgeBackend`, not on `IForgeBackend`, so SourceForge and PrivateHub need no stubs. Promote them to the interface when the forge domain grows a PR contract.

**Shell sites:** `grep -rn 'sh -c\|"sh",' uppsrc/ManagerSkylark/GitHub*.{cpp,h}` returns 0. `grep -rn _prs.json uppsrc` returns 0. Not changed, outside the GitHub module:
- `GitHubActions.cpp` `TriggerWorkflowRunAsync` runs a fixed `~/Dev/ai-upp/bin/build` path with `LocalProcess::Start(cmd)`. No user data is in the command, but it is a string command line. Convert with the rest of the HostPlatform slice.
- `HostPlatform.cpp` and the views: out of scope per the brief.

**Behavior changes to know about (intentional):**
- Mutation route responses: `status` is `error` when GitHub refused, with the reason in `result` or `url`. The page JavaScript now alerts on error and no longer reloads or redirects to PR 7 after a failed create.
- `api/gh/repos/*` JSON: local-only repos carry `stars: -1` and `forks: -1` (unknown) instead of `0`.
- Pull request diff failure shows `Diff is unavailable from GitHub: <reason>` instead of the empty-diff message.
- Actions tab: an error shows `Workflow runs are unavailable from GitHub: <reason>` under any locally started runs.

**Verification (exact, after every commit):**
- `uppbuilder -m CLANG WebEngineTests -v && ./bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/WebEngineTests` → 116/116 checks passed, exit 0.
- `uppbuilder -m CLANG ManagerSkylark -v && ./bin/ManagerSkylark --test` → ManagerSkylark verification PASSED, exit 0. The GitHub section now covers: issue list/detail/create; PR list (open and merged states), detail with comment thread, diff text; workflow runs; repo list; local git log and branches through a real fixture repo; PR create (including rejection of an unsafe branch before gh runs), merge (success and refusal), close, comment URL; unavailable state for issues, PRs, PR detail, actions, all with gh missing; security and settings tabs with no fixed claims.
- `uppbuilder -m CLANG -a ../SkylarkManager/uppsrc CompanySkylark -v && ./bin/CompanySkylark --test` → ALL TESTS PASSED, exit 0.
- GCC: `uppbuilder -m GCC ManagerSkylark -v`, `uppbuilder -m GCC WebEngineTests -v`, `uppbuilder -m GCC -a ../SkylarkManager/uppsrc CompanySkylark -v` → rc 0 each.
- REST shape: the real `gh api repos/OuluBSD/CompanySkylark/actions/runs` response has the `workflow_runs` array with `head_branch`, `created_at` and `event`, matching the fixture. No PR object was available to probe live (that repo has no PRs). The PR fixture follows the REST docs, so one live PR check is still open.
- Not done: a browser or live-server pass of the PR merge, close and comment flows against a real repo. The JavaScript change is covered only by reading it.

**Diff stat (cf728fc..HEAD):** 14 files, +711 / −263. Per commit: `75960be` 4 files; `dcd9ccd` 7; `5f82a91` 4; `c9ed779` 2; `bed3033` 5.

**Open items (not done; for the planner):**
- Static claims outside the brief's list, left alone: the user profile page's "Active GitHub Projects" cards ("ai-upp Core Delivery", "Active Sprint") are invented. The New-PR form options are unchanged, as the brief said.
- `GetRepositories` (the local fallback behind the repo list) marks every local repo `is_private = true`, and the repo header's Private/Public badge uses that flag. That is a fixed claim. It needs a tri-state flag, which is a `LocalRepoInfo` schema change.
- `LoadScrapedData` still reads `~/.aria/sites/github/<owner>_<repo>/summary.json`. It is read-only and fills the local fallback only, but the scraper it reads is not part of this repo.
- `GitHubPlatform.cpp` `RenderRepoHeader` still shows the `Private`/`Public` label from the flag above.
- Slice 4 as the brief lists it: `DataStore.cpp`, `RestApi.cpp` (still over target), `ScopedPolicy.cpp`, `ManagerSkylark.cpp`.

Nothing pushed. The user's demo server on port 8081 was not touched in this assignment.

Ready for the next brief.

## Planner entry — 2026-11-09 (takeover agent)

Assignment 3 accepted. Planner re-verified: 0 sh -c sites in the GitHub
module, 0 _prs.json references, 0 fixed clean-scan claims; fence 116/116,
Manager PASSED (CLANG+GCC), Company ALL PASSED, GCC rc 0, tree clean at
ad3cb09. Decisions: PR/repo/run ops stay on GitHubForgeBackend (promote
later); TriggerWorkflowRunAsync converted now (argv + honest local-build
label); profile-page invented cards removed; is_private tri-state approved;
LoadScrapedData stays read-only. Next: WORKER-BRIEF-04.md (relayed also via
CompanySkylark/tmp/task.txt).

## 2026-11-09 — WORKER-BRIEF-04 (Assignment 4, T01 slice 4a) — done

**Commits (on main, nothing pushed):**
- `5f55e44` Workflow trigger is a labelled local build, argv only. `TriggerWorkflowRunAsync` is now `TriggerLocalBuildAsync()`: `Start2` with an argv vector for the fixed `bin/build` path. The run record is named "Local build: reference/ManagerSkylark" with event `local_build`. The page has one "Start local build" button in place of two duration buttons. The route message says it is not a GitHub Actions run.
- `8bb3e8d` Profile page drops invented claims. The Projects tab ("Active GitHub Projects", "Active Sprint") and the Overview heatmap ("2,842 contributions", "Active Daily") become neutral "not loaded" notices. I also removed the heatmap, which the brief did not name; it was the same class of claim.
- `27962b0` `LocalRepoInfo.is_private` is tri-state: -1 unknown (default and local fallback), 0 public, 1 private. JSON key stays `is_private`, now an int. Live repos map `isPrivate`; the scraped snapshot maps its value when present. `VisibilityLabel` renders Unknown/Public/Private in the repo header and both profile badges. A synthetic check covers all three renderings.
- `55821bc` `DataStore.cpp` (3074 lines) split into eight translation units with shared helpers in `DataStoreInternal.h`. Sizes: `DataStore.cpp` 636, `DataStoreSchema.cpp` 500, `DataStoreJournal.cpp` 419, `DataStorePrepare.cpp` 438, `DataStoreWrite.cpp` 467, `DataStoreLock.cpp` 186, `DataStoreValidate.cpp` 218, `DataStoreQuery.cpp` 123, `DataStoreInternal.h` 228. Largest file is 636 lines, under the ~800 target.

**DataStore split, what changed mechanically:**
- Shared helpers (16 functions, 4 types, the canonical write mutex, two constants) moved to `DataStoreInternal.h`. Functions lost `static` and got prototypes. POSIX-only types and prototypes keep their `#ifdef PLATFORM_POSIX` guards. The mutex is one `extern` definition in `DataStoreLock.cpp`, as before.
- Check: comparing the original and the split as line multisets (after stripping `static `) shows zero original lines missing. Additions are only includes, guards, prototypes, and `extern`. The `HasConflictingCreateIdentity` forward declaration in the lock file is now a single shared prototype.

**Verification (after every commit, CLANG):**
- Fence: `WebEngineTests` 116/116 checks passed.
- `ManagerSkylark --test`: PASSED. This includes the RestApi self-tests, which exercise the store load, query, and service paths.
- `CompanySkylark --test`: ALL TESTS PASSED.
- GCC, final tree: `ManagerSkylark`, `WebEngineTests`, and `CompanySkylark` each rc 0.

**Gaps to know about:**
- The self-tests do not call `PublishBatch`, `WriteRecord`, `RecoverPendingBatches`, or the journal code directly. The journal and recovery paths have no direct test in this repo. The line-multiset check and the compiler are the only evidence those paths are unchanged.
- The POSIX guards are preserved, but the non-POSIX (MSVC) branches were not compiled here. CI covers MSVC.
- `RestApi.cpp` (1241 lines), `ScopedPolicy.cpp`, and `ManagerSkylark.cpp` are unchanged and out of scope (slice 5).

**Left alone and flagged:**
- `GitHubUserView.cpp` Packages tab: a static description of the U++ packages. It makes no claims about state.
- Pinned-repo descriptions fall back to "Ultimate++ Agentic repository in ~/Dev/<name>" when GitHub gives no description. This is placeholder text, not a state claim.
- The `RenderOperatorWarningBar` notice was rewritten for this brief: its cache counts and timings were invented, so the notice now only describes the local build.

Nothing pushed. The demo server on port 8081 was not touched.

Ready for the next brief.

## Planner entry — 2026-11-09 (takeover agent)

Assignment 4 accepted. Planner re-verified: four commits as reported;
DataStore split sizes exact (largest 636); TriggerLocalBuildAsync argv-only
confirmed; invented profile claims gone (extra heatmap removal approved);
RenderOperatorWarningBar honesty rewrite approved; fence 116/116, Manager
PASSED (CLANG+GCC), Company ALL PASSED, GCC rc 0. The flagged journal/
recovery test gap becomes Brief 05 item 1 (do it first). Next:
WORKER-BRIEF-05.md (slice 5: hard-path tests + RestApi/ScopedPolicy/
ManagerSkylark splits + CompanySkylark.upp cleanup).

## 2026-11-09 — WORKER-BRIEF-05 (Assignment 5, T01 slice 5) — done

**Commits:**
- `6232cdb` ManagerSkylark: direct self-tests for DataStore write hard paths (SkylarkManager).
- `ba79bfc` ManagerSkylark: split RestApi.cpp by route area.
- `eea3698` ManagerSkylark: split ScopedPolicy.cpp by responsibility.
- `c2915f0` ManagerSkylark: split ManagerSkylark.cpp by responsibility.
- `599ee15` CompanySkylark.upp: trailing newline (CompanySkylark repo).

**1. DataStore hard-path tests (done first, `6232cdb`).**
- New `DataStoreSelfTests.{h,cpp}`, run from `--test` against a fresh writable temp source per case. The brief allowed a private-friendship header if documented: `DataStoreSelfTests` is a friend of `DataStore` and `DataWriteAuthorization`, and the header says so. The test-only load path `LoadWriteFixtureForTests` had no callers before this; it is now used.
- Covered: `WriteRecord` (commit, same-key replay, stale revision, out-of-scope source); `PublishBatch` (commit, replay, reused op id with a different request); `PrepareJournal` round-trip (manifest written to disk parses back with the staged op and record; publish blocked while pending; recovery clears it); crash recovery at two failpoints (after the record replace and before it): a second publish does not re-execute, recovery rolls back to the original content, a second recovery is `clean`, the retry commits once, and a replay is `already_committed`.
- Crash simulation uses the in-process batch failpoints, not a real kill. Crafted journal files were not used; the public API and a real temp dir were enough.

**Defect found by the new tests (not fixed here; planner decision needed).**
- A committed batch leaves `.mgr-journal/<op>` in place. `PublishBatch` returns `committed` with the journal still present.
- No production code calls `RecoverPendingBatches` (grep: only the tests). After the first batch on a source, every further publish returns `journal_recovery_required` until something runs recovery.
- A replay before recovery returns `journal_recovery_required`, not the prior result. AGENTS rule 7 says duplicates return the prior result.
- The tests assert the current lifecycle: blocked before recovery, `already_committed` after. They do not present the wedge as expected behavior.
- Options for the planner: (a) remove the journal inside `PublishBatch` after the receipt is stored (the receipt is durable first, so cleanup is safe); (b) have each write path run `RecoverPendingBatches` first. (a) matches rule 7 more closely.
- The candidate-write boundary is still disabled in production, so no live data is affected today.

**2. `RestApi.cpp` (1241) split by route area (`ba79bfc`).**
- `RestApi.cpp` 85: core routes (`api/health`, `api/framework/spec`, `quit`) and `CONSOLE_APP_MAIN`, which stays in the main package.
- `RestApiProjects.cpp` 266: projects, project detail, task next/claim/done, goals, issues, releases.
- `RestApiSweAgent.cpp` 37: `api/sweagent/run`.
- `RestApiGitHub.cpp` 33: `api/project/*/sync-github`.
- `RestApiSelfTests.cpp` 864: fixtures and `RunSelfTests`. Test harness, not production code. One sequential harness shares locals across the whole run, so it stays whole. **Logged exception** (the ~800-line rule covers non-test files).
- The brief names a "chat" area; this file has no chat routes, so none was created.
- The 13 `SKYLARK` registrations are unchanged (checked as a sorted set against the original).

**3. `ScopedPolicy.cpp` (973) split by responsibility (`eea3698`).**
- `ScopedPolicy.cpp` 155: classification, defaults and settings validation, redacted view, `Resolve` orchestration, external-model and publication grants, review granularity.
- `ScopedPolicyIdentity.cpp` 131: identity validity and keys, identity field parsing, request-source and provider lookups.
- `ScopedPolicyAncestry.cpp` 42: the source ancestry walk, with its depth and consistency checks.
- `ScopedPolicyRegistry.cpp` 272: `TrustedPolicyRegistry` validation and loading.
- `ScopedPolicySelfTests.cpp` 405: `RunSyntheticTests` (test file).
- `ScopedPolicyInternal.h`: shared limits and helper declarations (not public API). No name collisions elsewhere in the assembly.
- Deviation from the brief's list: external-model and publication grants stayed in `Resolve`, not separate files. They are short boolean expressions over the same locals; a split would pass many values for no gain.
- Checked as a line multiset: the only changes are the `return result;` → `return false;` rewrite inside the moved helpers, the braces and signatures, and the new call sites. Each early return keeps its original status and reason.

**4. `ManagerSkylark.cpp` (920) split (`c2915f0`).**
- `ManagerSkylark.cpp` 130: constructor, destructor, singleton accessor, `Initialize`, `StopServer` (shutdown).
- `ManagerSkylarkSetup.cpp` 135: `LoadSitePolicy`, `InitApp` (site setup and registration). `ReadPolicySettings` moved with `LoadSitePolicy`, its only caller.
- `ManagerSkylarkSeed.cpp` 264 + `ManagerSkylarkSeed.h`: demo and synthetic fixture seeding. The four entry points are declared in the header because `InitApp` calls them. They lost `static` and their default arguments, which moved to the declarations.
- `ManagerSkylarkFilters.cpp` 40: `FilterHandler`, `ResolveEffectivePolicy`, `IsAssessmentSourceVisible` (filter hooks).
- `ManagerSkylarkNavigation.cpp` 248: `BuildNavigation` and the projects, mesh and goals trees.
- `ManagerSkylarkRender.cpp` 154: `RenderBadge`, `RenderProgressBar`, `RenderHtmlPage`, `AddMarkdownSupport`.
- Checked as a line multiset: no original line missing.

**5. Trailing newline (`599ee15`, CompanySkylark repo).** Diff was one blank line; committed with the brief's path-scoped command.

**Size rule (brief item 4).** Largest non-test file in ManagerSkylark is now `CandidateWorkflow.cpp` at 758. The only file over 800 is `RestApiSelfTests.cpp` (864), a logged test-harness exception.

**Verification (after every commit, CLANG; GCC after each step):**
- Fence: `WebEngineTests` 116/116 checks passed.
- `ManagerSkylark --test`: PASSED (includes the new hard-path tests and the RestApi self-tests).
- `CompanySkylark --test`: ALL TESTS PASSED.
- GCC: `ManagerSkylark`, `WebEngineTests`, `CompanySkylark` each rc 0 after every commit.

**Gaps and notes:**
- MSVC (non-POSIX) branches were not compiled here; CI covers them.
- `DataStoreSelfTests.{h,cpp}` changed on disk once during the step (formatting only; contents matched my edits). I kept the file as it is on disk.
- Slice 6 is untouched: SourceForge/PrivateHub wiring, the `~/.aria/sites/sourceforge` migration, HostPlatform argv conversion, T02+.

Nothing pushed. The demo server on port 8081 was not touched.

Ready for the planner.

## Planner entry — 2026-11-09 (takeover agent)

Assignment 5 accepted. Planner re-verified: splits as reported (RestApi 4
area files + main with CONSOLE_APP_MAIN; 13 SKYLARK set preserved;
ScopedPolicy grants-deviation approved; CandidateWorkflow.cpp 758 = largest
non-test; RestApiSelfTests 864 logged exception); DataStore hard-path tests
present; fence 116/116, Manager PASSED (CLANG+GCC), Company ALL PASSED +
GCC rc 0. On-disk DataStoreSelfTests change: keep as-on-disk. Next:
WORKER-BRIEF-06.md (slice 6, final T01 slice: Host argv conversion,
SourceForge/PrivateHub wiring, local-store migration, fence additions).

## Worker entry — WORKER-BRIEF-06 (T01 slice 6, final T01 slice)

**Step 1 — HostPlatform argv-only conversion (`8f33310`).**
- All `sh -c` and `"sh",` sites in `Host*.cpp` now run through `HostExec::Run` (LocalProcess argv, no shell). `grep -rn 'sh -c\|"sh",' uppsrc/ManagerSkylark/Host*.cpp` → 0.
- Count: the brief says 16 sites. The grep finds 18: 16 in HostPlatform.cpp, plus one each in HostViews.cpp (remote file browser) and HostAdminViews.cpp (remote `df` probe). Both view sites were converted too.
- Validation before argv: user, host, remote path, repo name, hash, build method and flags, git subcommand (allowlist plus refused options).
- Residual risk: the git subcommand allowlist is partial. Git options not on the refuse list are not individually checked. Remote strings still go through the remote login shell (ssh's design), built only from validated tokens.
- Out-of-brief fix (required to build): HostViews.cpp used `VividIcon`, `UserSession`, `SystemAuthService` without including their headers. It built only through earlier units' includes. A clean worktree of HEAD `51031d7` fails the same way (22 errors). Added `ManagerSkylark.h` and `SystemAuth.h`.
- `HostExec.{h,cpp}` added to `ManagerSkylark.upp` after `HostRoutes.cpp`.
- Verification: fence `WebEngineTests` 116/116; `ManagerSkylark --test` PASSED; `CompanySkylark --test` ALL TESTS PASSED (CLANG). GCC rc 0 for `ManagerSkylark`, `WebEngineTests`, `CompanySkylark`.

**Step 2 — SourceForge webapp onto SourceForgeForgeBackend (`7c6bab7`).**
- Tickets go through `SourceForgeForgeBackend`, one default backend per project (each backend is bound to one project). Base URL `https://sourceforge.net`, no credentials = read-only.
- Writes (create, comment, status) return the backend's gate result. Without credentials they report unavailable; nothing is faked.
- Wiki, discussion and files have no backend surface, so they report Unsupported and the views show an explicit unavailable state. The seeded demo content in the old local stores is gone, and the webapp no longer reads `~/.aria/sites/sourceforge/**` at all. Step 4 is where that legacy data is imported.
- Read failures reach the API as `ok=false` with provenance and error. The views render an unavailable banner.
- Scraped `summary.json` stays read-only and shows a "Scraped snapshot (read-only)" badge.
- `SourceForgeForgeBackend` now rejects project slugs outside `[a-z0-9._-]` (and `.` / `..`) before they reach an Allura path.
- New `SourceForgeAdapterTests.cpp`: stub Allura transport, reachable and unreachable, wired into `ManagerSkylark --test`.
- Verification: fence 116/116; `ManagerSkylark --test` PASSED; `CompanySkylark --test` ALL TESTS PASSED (CLANG); GCC rc 0 for `ManagerSkylark`, `WebEngineTests`, `CompanySkylark`.
- Limitation: live `sourceforge.net` was not exercised (stub transport only). A manual smoke test still needs to be done with real Allura responses.

**Stopped before step 3. Blocked, needs a planner decision.**
- Step 3 assumes a PrivateHub webapp with issues that can be moved onto `PrivateHubWorkspaceBackend`. None exists: no PrivateHub routes or views in ManagerSkylark or CompanySkylark, and the WebEngine host is not composed into ManagerSkylark. The only PrivateHub code is `PrivateHubWorkspace` itself, which the fence test exercises. Question for the planner: should step 3 build a minimal PrivateHub issues surface, or should the brief be re-scoped?
- Step 4 depends on the PrivateHub storage model. Tickets map onto `ForgeIssue`. Wiki pages, discussion topics and release files have no PrivateHub record type, so the converter needs a schema decision for those.
- Steps 4 and 5 are not started.

## Planner entry — 2026-11-09 (takeover agent)

Brief 06 steps 1–2 accepted. Planner re-verified: 0 sh -c sites in Host*,
HostExec added, SourceForgeForgeBackend wired with stub-transport tests,
scraped data read-only + badge, slug validation added; fence 116/116,
Manager PASSED (CLANG+GCC), Company ALL PASSED. Out-of-brief HostViews
include fix and 18-site conversion approved; allowlist tightening noted as
backlog; live sourceforge.net smoke deferred as manual follow-up.

RE-SCOPE (worker's blocker question): no PrivateHub webapp exists to
migrate into; engine-host composition belongs with the PrivateHub UI work
(T04). Brief 06 steps 3–5 replaced by WORKER-BRIEF-06A.md: PrivateHub record
kinds (wiki-page/discussion-topic/release-file), --migrate-sf-local
converter (originals untouched), PrivateHub broker service in ManagerSkylark.
PrivateHub engine-site UI + fence additions + host composition deferred to
T04 (recorded as deferral).

## Brief 06A (T01 re-scope) — 2026-11-09

**Step 1 done — commit 59b4a60.** PrivateHubWorkspace record kinds: wiki-page, discussion-topic (posts as `<topic>.<post>` children), release-file. Each has a schema card, envelope records with revisions, and data.provenance. Kinds documented in `uppsrc/PrivateHubWorkspace/AGENTS.md`.
- Fixed in WebStorage: `JsonFileStore::PutOpaque` wrote the typed debug format (`StoreAsJson`), so reads quarantined every record. It now writes plain JSON with `AsJSON`. No other callers.
- Fixed in the backend: the issue listing returned comment records as issues.
- Gate: fence 135/135 (19 new checks). Full gate green on that run: ManagerSkylark `--test` PASSED, CompanySkylark ALL TESTS PASSED (CLANG), GCC rc 0 for ManagerSkylark, WebEngineTests, CompanySkylark.
- Caveat: that ManagerSkylark build was incremental (objects cached). See step 2.

**Step 2 — converter written, NOT committed. Blocked on the build environment.**
- Added `PrivateHubWorkspace/PrivateHubMigration.{h,cpp}`: one-shot import of `tickets/wiki/discussion/files_store.json` into PrivateHub records. Provenance `migrated-from-legacy`, `provenance_ref` = `<store>#<key>`. Re-runs skip existing ids. Absent and corrupt stores are reported, and malformed entries are rejected, not guessed. Data root inside the legacy dir is refused. Originals are read only.
- Added `ManagerSkylark/SfLegacyMigrationCli.{h,cpp}` (`--migrate-sf-local` handler, exit 0 clean / 1 not clean / 2 usage or refusal), `SfLegacyMigrationTests.cpp` (fixture shaped like the real stores; byte-compare of originals; idempotence; corrupt/absent; CLI exit codes; read-back through the backend), and the dispatch in `RestApi.cpp` plus the self-test call in `RestApiSelfTests.cpp`.
- Verified: the converter and its self-test pass (`MIGRATION SELF TESTS PASSED`), run from a temporary verifier package that depended only on Core, ForgeCore, WebStorage and PrivateHubWorkspace. The verifier was deleted afterward. The `--migrate-sf-local` dispatch in `RestApi.cpp` is NOT compiled or run.
- Blocker: a full (non-incremental) build now fails in code this step did not touch. Errors:
  - `ManagerSkylark.h:27` — `FilterHandler` marked `override`, but the base has no such virtual.
  - `SystemAuth.cpp:108` — `Http::GetMethod` missing.
  - `ManagerSkylarkSetup.cpp:76` — `ManagerStore::SetRootDir` missing.
  - `WebEngine/Request.cpp:99,118,128` and `WebEngine.cpp:113` — an `Http` call with 8 arguments where the installed API takes 7, plus `GetMethod`.
  - No copy of these members exists under `~/Dev` (searched). The installed Skylark (`~/Dev/ai-upp/uppsrc/Skylark`, unchanged since 2026-10-17) does not define them.
  - Consequence: the green gates recorded at 06 and at step 1 relied on cached objects. Nothing was recompiled from scratch. ManagerSkylark, the fence and CompanySkylark cannot currently be rebuilt cleanly, so the per-commit gate cannot pass. Step 2 was therefore not committed.
- Working tree left uncommitted: the step 2 files above, the `PrivateHubWorkspace` validators made public (`ValidProjectKey`, `SafeRecordId`), `ManagerSkylark.upp` (adds `PrivateHubWorkspace` to uses and the three new files), and `PrivateHubWorkspace.upp` (adds the migration files).

**Not started:** step 3 (`PrivateHubService : IBrokerService`).

**Decision needed:** either restore the Skylark/Manager package versions the ManagerSkylark and WebEngine code was written against, or planner approves updating those untouched call sites to the installed API as a separate, prior commit. Only then can step 2 be gated and committed.

**Deferred as recorded:** PrivateHub engine-site UI and fence additions, and engine-host composition (T04). Live sourceforge.net smoke test (manual follow-up). Git-subcommand allowlist tightening (backlog).

## Planner entry — 2026-11-09 (takeover agent) — FOUNDATION COLLISION, builds paused

Brief 06A step 1 (59b4a60) verified: record kinds correct, fence at 135/135
claimed and spot-consistent. Step 2 (migration converter + tests) is written
and looks correct but is NOT committed: a full (non-incremental) rebuild now
fails in BazaarCore — uncommitted edits by another party in
/home/sblo/Dev/BazaarCore remove `Http::GetMethod`,
`SkylarkApp::FilterHandler`, `ManagerStore::SetRootDir` (API surface
ManagerSkylark/WebEngine build on; present in committed f73fb72 via
cdff7d6/f73fb72) and the uncommitted patches/Core.patch adds a `SchemaCard`
to Core, colliding with WebStorage's. A pinned clone at f73fb72 does not
build SuperIntelligence (needs the uncommitted Core/Ext deltas). The
foundation is mid-restructure by another party and currently has no
self-consistent buildable state for our stack.

WORKER ORDERS: PAUSE. Keep the uncommitted step-2 files in the worktree, do
not commit, do not revert anything in BazaarCore. Planner escalates to the
user for arbitration. Last fully-verified commits: SkylarkManager 59b4a60
(+ step-2 files uncommitted), CompanySkylark unchanged.

## Planner entry — 2026-11-09 (takeover agent) — WAITING ON FOUNDATION

User guidance: the BazaarCore activity is the split agent implementing the
older ai-upp split plan (BazaarCore was created by that plan). Decision:
do NOT adapt to the moving uncommitted tree. The worker stays paused
(step-2 files preserved in the worktree). When the split agent lands their
BazaarCore restructure as commits, the planner will verify the new API
surface and order the adaptation work: re-home SystemAuth filtering
(FilterHandler removed), request-method access (GetMethod removed),
ManagerStore root config (SetRootDir removed), SchemaCard naming if Core
gains one, SameSite re-add if wanted (or fence-only, documented).
Planner watches BazaarCore on a timer.

## Planner entry — 2026-11-09 (takeover agent)

Foundation merged (BazaarCore 3b0f04a: all four APIs + Core/Ext helpers,
pushed to origin). Unblocked and gated: the SchemaCard collision between
DataStore.h and WebStorage.h (first shared translation unit via 06A step-2
tests) resolved by renaming the WebStorage types to WebSchemaCard/
WebSchemaField (DataStore keeps the plain name; its contract is the richer
one). Full gate green: fence 135/135, Manager PASSED (CLANG+GCC), Company
ALL PASSED + GCC rc 0. WORKER: RESUME Brief-06A step 2 — your worktree files
are intact (PrivateHubWorkspace.cpp carries the mechanical rename; it
commits with your step-2 work). Commit step 2 when its gate passes, then
step 3 per 06A.

## Brief 06A — steps 2 and 3 done (update, 2026-11-09)

The blocker above is resolved. BazaarCore now provides the Skylark APIs
(Http::GetMethod, FilterHandler, cookies, ManagerStore::SetRootDir), and the
planner landed the WebSchemaCard/WebSchemaField rename (7560954). Full gate
re-run on each commit below.

**Step 2 — commit 67a3f41.** `--migrate-sf-local <legacy-dir> <data-root>`
converter: `PrivateHubWorkspace/PrivateHubMigration.{h,cpp}`,
`ManagerSkylark/SfLegacyMigrationCli.{h,cpp}`, `SfLegacyMigrationTests.cpp`,
dispatch in `RestApi.cpp`. Originals are read-only; provenance
`migrated-from-legacy`; corrupt/absent stores reported; fixture-based --test
(byte-compare of originals, idempotence, CLI exit codes).

**Step 3 — commit e57b457.** `PrivateHubService : IBrokerService`
(`ManagerSkylark/PrivateHubService.{h,cpp}`) with `RegisterPrivateHubService`.
Ops: issue.list/get/create/comment, wiki.list/get, topic.list/get, post.list,
file.list/get. Deny by default: unknown op 404; read scope `privatehub:read`,
write scope `privatehub:write` (403 otherwise); writes need caller_id (401)
and an idempotency_key (400). Author is always caller_id. Successful writes are
recorded under (caller, op, key) in a `broker-idempotency` collection, so a
repeat returns the recorded result. Writes are serialized by an in-process
mutex. --test (`PrivateHubServiceTests.cpp`) runs through a WebBroker with a
temp workspace and temp data root.

**Gate after step 3 (all green):** fence 135/135; ManagerSkylark --test PASSED;
CompanySkylark --test ALL TESTS PASSED (CLANG); GCC rc 0 for ManagerSkylark,
WebEngineTests, CompanySkylark.

**Known gaps, recorded:**
- The site id for the PrivateHub broker comes from the caller of
  `RegisterPrivateHubService`. No PrivateHub site entry exists yet in the
  ManagerSkylark config; wiring that is T04.
- The idempotency record store has no expiry, and the mutex only serializes
  one process. Two processes sharing a data root can race a duplicate key.
- Live sourceforge.net smoke test: manual follow-up.

**Deferred as recorded:** PrivateHub engine-site UI and fence additions;
engine-host composition (T04); git-subcommand allowlist tightening (backlog).

**Stopped after step 3 and waiting for acceptance.**

## Planner entry — 2026-11-09 (takeover agent)

**T01 COMPLETE.** Brief 06A steps 2-3 accepted. Planner re-verified: fence
135/135, Manager PASSED (CLANG+GCC), Company ALL PASSED (CLANG) + GCC rc 0,
tree clean at e553db3. The "result: not clean" line in GCC build output is
an umk informational artifact (build succeeded, entrypoint created, binary
verified). Known gaps recorded for follow-ups: PrivateHub broker site-id
wiring (T04), idempotency store expiry + multi-process mutex (backlog),
live sourceforge.net smoke (manual).

Next: WORKER-BRIEF-07.md — T02 first slice: per-site template/static roots
(BazaarCore Skylark hook, foundation now unblocked) + WebFramework asset
extraction.

## Worker entry — Brief 07 (T02 first slice)

**Commits**

| Step | Repo | Commit | Subject |
|---|---|---|---|
| 1 | BazaarCore | 9f22dfc | per-request site roots for template and static lookup |
| 2 | BazaarCore | 4b14dd7 | site-scoped static serving with containment |
| 2 | SkylarkManager | 9a5315b | per-site static and template roots for engine listeners |
| 3 | SkylarkManager | d75e7ed | WebFramework: move inline CSS and JS into asset files |
| 4 | SkylarkManager | 7508be2 | two-site static and template resolution (fence) |
| log | SkylarkManager | (this commit) | Brief 07 worker entry |

**Step 1: BazaarCore hooks.** `SkylarkSiteRoots` (thread-local Set, Clear,
IsOverridden, TemplateRoot, StaticRoot) defaults to `SkylarkApp::Config().path`,
so an unset root is today's behavior. `Renderer::GetTemplate` puts the template
root in its cache signature. `GetTemplateDefs` and the stock static handler
read the root at render time.

**Step 2: engine static serving.** While a site root is set, `static/**` is
served from that site's static root. Only GET and HEAD are accepted (other
methods get 405 with `Allow`). `SkylarkSiteRoots::ResolveStatic` accepts plain
relative paths only: no leading `/`, backslash, NUL, empty, `.` or `..` segment.
It accepts only an allowlist of static extensions. It resolves symlinks with
`realpath` and requires the target to lie inside the root. Responses carry
`X-Content-Type-Options: nosniff`, and ETag and 304 behave as before. Without a
site root the stock handler is unchanged. `EngineSite::TemplateRoot()` and
`StaticRoot()` point under the site's data root. `SiteRootsScope` sets them
around one dispatch in `WebEngineHost::ListenerLoop` and clears them after it.

**Step 3: WebFramework assets.** The 15 style and script providers that
returned pure string literals now return `WebFrameworkAsset("<file>")`. The
files are `assets/*.css` and `assets/*.js`, listed in `WebFramework.upp` under
`Styles` and `Scripts` separators. Each file was written from its provider's
literal text, byte for byte. The providers that build from theme or density
data (`ThemeRegistry::GetGlobalStyles`, `WebTheme::GetCssVariables`) are unchanged.
The loader reads `GetExeDirFile("static/WebFramework/")`, caches the text, and
throws on a missing file. `deploy-assets.sh <bin_dir>` copies the assets into
the real directory of a binary. Pass the `CLANG-…` directory, not the
`bin/examples/` symlink. The full gate runs the deploy step before each test
binary.

**Step 4: fence additions.** Four checks run after `SetupSites()`: the same
static name resolves to different files and bytes on alpha and beta, and the
same template name compiles from each site's own root. Containment checks
(traversal, absolute and backslash paths, allowlist, missing file, symlink
escape, cleared state) were added in step 2, 13 checks in all.

**Verification after each commit:** fence 148/148 (step 2 and step 3) and
152/152 (step 4); ManagerSkylark `--test` PASSED; CompanySkylark `--test`
ALL TESTS PASSED (CLANG); GCC rc 0 for ManagerSkylark, WebEngineTests and
CompanySkylark.

**Route diff (WebShowcase, 9 routes, before vs after step 3):** IDENTICAL after
normalization. Normalization drops `Date` and `Set-Cookie` lines and replaces
`usr_*` ids and dd/mm/yyyy timestamps. Before normalization, `diff -rq` lists 10
differing files. All of them are the normalized fields. The baseline is the
pre-step-1 capture; the post-step-2 capture was also identical.

**Examples.** WebShowcase: rebuilt and recaptured after each step. The
captured routes include the landing page, showcase, dashboard, workspace and
telemetry. SkylarkGuiExample is a CtrlLib GUI app with no WebFramework
dependency, so step 3 cannot change its output and no capture was taken.

**Coverage limits, recorded:**
- The route captures exercise MegaMenu, Dashboard and AppWorkspace styles and
  the density, theme, tree and audio scripts. They do not render the GitHub,
  SourceForge or Sidebar templates. Those three extracted files are checked by
  construction only (the same literal-to-file extraction as the rest).
- The cache check in step 4 covers the template root that the cache key uses.
  It does not read the cache directly.
- Deployment is explicit. A binary that renders WebFramework pages without
  `deploy-assets.sh` run for its directory now throws instead of rendering
  unstyled HTML. Production deployment needs the same step.

**Out of scope, not touched:** Brief 08 (app-renderer witz extraction),
Company-side work, T04. The untracked `tasks/takeover/WORKER-BRIEF-07.md` is
left as it was.

**Stopped after step 4 and waiting for acceptance.**

## Planner entry — 2026-11-09 (takeover agent)

Assignment 7 (T02 first slice) accepted. Planner re-verified: 15 asset files
under WebFramework/assets (incl. template-github/sourceforge css, audio.js),
Template.cpp 536 lines, per-site roots via SkylarkSiteRoots +
SiteRootsScope (WebEngine/Host.cpp), fence 152/152, Manager PASSED
(CLANG+GCC), Company ALL PASSED (CLANG) + GCC rc 0, tree clean.
Route-diff methodology (baseline control + normalization) sound.
NOTE for deployment: pages now throw if WebFramework assets are not
deployed (deploy-assets.sh) — production/demo servers need that step after
pull. Next: WORKER-BRIEF-08.md (SameSite re-add as BazaarCore patch + GitHub
renderer witz extraction, behavior-preserving).

## Worker entry — Brief 08 (T02 second slice)

**Commits**

| Step | Repo | Commit | Subject |
|---|---|---|---|
| 1 | BazaarCore | none | SameSite already present in main; no patch needed (see below) |
| 2 | SkylarkManager | e47a999 | WebEngineTests: assert SameSite=Strict on fresh engine logins |
| 3 | — | none | GitHub renderer extraction not started; blocked on decisions below |

**Step 1: SameSite re-add — not done, premise does not hold.** The brief says
SameSite was reverted (f73fb72). In this repo it was never reverted:
- `f73fb72` is in BazaarCore history, and `3b0f04a` (merge) includes it.
- `main` at 11e9512 and `origin/main` (96f8dd5) both contain the patch:
  `Http::SetRawCookie` and `Http::SetCookie` take `samesite`, and `SetCookie`
  forwards `httponly`. That is the f73fb72 shape.
- Engine session cookies already use it (`WebEngine/Request.cpp` lines 99, 118,
  128: `SetRawCookie(..., true, "Strict")`).
I made no BazaarCore commit, to avoid a duplicate patch. If a revert exists in a
different clone or branch, the planner should point me at it.

**Step 2: fence assertions.** Two checks on the fresh-login Set-Cookie of each
engine port: `SameSite=Strict` and `HttpOnly`. Fence 156/156 (was 152).
Gate after the commit: ManagerSkylark PASSED, CompanySkylark ALL PASSED
(CLANG), GCC rc 0 for all three targets.

**Step 3: GitHub renderer extraction — stopped before editing. Decisions
needed:**
- The GitHub webapp is a ManagerSkylark page set (`GitHub*.cpp`, `ManagerSkylark`
  routes under `/repo/...`). It is not a WebEngine site. The WebEngine has no
  GitHub site binding, so there is no engine site-scoped template root for it to
  render through. Options: (a) render through the app's default template root,
  which is what `SkylarkSiteRoots` falls back to, and move to an engine site
  after T04; (b) wait for T04 engine composition. I recommend (a).
- The inline HTML is about 1,500 lines across eight files
  (`GitHubWorkflowViews` 126 HTML-like lines, `GitHubRepoTabs` 86,
  `GitHubRepoPulls` 84, `GitHubUserView` 78, `GitHubRepoCode` 54,
  `GitHubRepoIssues` 51, `GitHubPlatform` 28, plus `GitHubActions` and
  `GitHubRepoView`). Output is data-driven and interleaved with live `gh` and
  `git` calls, so each view needs a witz data model, not a literal move.
- Byte-identical route captures need a headless ManagerSkylark with a temp data
  root, fixture repos, and stubbed `gh`. No such harness exists yet; building it
  is a separate step. Witz syntax is confirmed (`$for(x in list)…$endfor`,
  `$if`, `$raw(...)`, escaped `$value` by default).

**Recommended order once decided:** build the capture harness first (route
baseline, before any edit), then extract one tab per commit starting with
`GitHubRepoTabs` (commits tab, git-backed and self-contained), then the rest.

**Stopped after step 2 and waiting for decisions on step 3.**

## Planner entry — 2026-11-09 (takeover agent)

Assignment 8 steps 1-2 accepted. Planner re-verified: BazaarCore samesite
restored (Http.h:151/155, alongside other parties' merged TUI work), fence
156/156 with SameSite+HttpOnly assertions on both engine ports, Manager
PASSED, Company ALL PASSED. Step-3 decisions: option (a) approved (app
default template root now; engine site at T04); witz data models per view;
capture harness FIRST (worker's recommended order approved). Next:
WORKER-BRIEF-09.md (capture harness + GitHub tab extraction begins).

## Worker entry — Brief 09 (T02 slice 3, takeover agent)

Commits (each with fence 156/156, ManagerSkylark PASSED, CompanySkylark ALL
PASSED under CLANG, GCC rc 0 for all three targets, GitHub capture compare):

- eda0d4b — step 1: headless GitHub route capture harness
  (`upptst/GitHubCapture/`: fixtures, fake gh, capture, normalize, compare,
  21 routes).
- (step 2 is the documented baseline regeneration command in
  `upptst/GitHubCapture/README.md`; baseline is regenerated, not committed as
  blobs. Normalization: Date/Set-Cookie/Content-Length lines dropped; usr_<hex>
  → usr_X; ms-capture.<random> → ms-capture.X; dd/mm/yyyy hh:mm:ss → TS;
  "N units ago" → AGO.)
- 1b0f1e3 — step 3: commits tab (`RenderRepoCommitsTab`) renders
  `uppsrc/ManagerSkylark/templates/github-commits.witz` through the app default
  template root. String bridge `GitHubTemplates.cpp`: `\r\n` → `\n`; throws if
  the template does not compile. `deploy-templates.sh` copies templates beside
  the binary; gate deploys them. Capture IDENTICAL, 42 files.
  Documented change: witz escapes msg/author/time/hash by default; the fixtures
  contain no escapable characters, so output is identical.
- 88cfa82 — harness update: fake gh now serves canned demo-repo JSON for the
  issues, pulls, comments, PR diff, runs and repo-list shapes. Before this,
  those views were only captured in their "unavailable" state. Baseline
  regenerated and stable across two runs.

Witz findings: loop tags join to content lines (a `$for(...)` on its own line
emits an extra newline per row); member access `$c.msg` works.

Step 4 status (remaining GitHub tabs, one commit each):
- commits: done (1b0f1e3), capture identical.
- issues: NOT STARTED. Blockers in source: owner/repo interpolated into an
  inline `<script>` (needs a JS-string context, not HTML escaping); SVG icons
  from `VividIcon::Get` need raw slots; the search bar and new-issue card are
  emitted before the availability check.
- pulls, code, user, workflow views, actions, projects, security, settings,
  agents: NOT STARTED.

Stopped after the commits extraction and the harness update. The remaining
tabs are not converted, so the brief's definition of done is not yet met.
