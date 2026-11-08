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
