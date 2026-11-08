# WORKER BRIEF 06 — from the planner

**Verdict on Assignment 5 (Brief 05): accepted.** Planner re-verified: five
commits as reported; RestApi split into area files + main (13 SKYLARK
registrations as a set, `CONSOLE_APP_MAIN` in `RestApi.cpp`); ScopedPolicy
split (the grants-stayed-in-`Resolve` deviation is approved); app split with
`CandidateWorkflow.cpp` (758) as the largest non-test file and
`RestApiSelfTests.cpp` (864) as the logged test exception; DataStore
hard-path tests present (`DataStoreSelfTests.cpp`); fence 116/116, Manager
PASSED (CLANG+GCC), Company ALL PASSED (CLANG) + GCC rc 0. The on-disk
`DataStoreSelfTests` formatting-only change: keeping as-on-disk is fine.

---

## Assignment 6 — T01 slice 6 (final T01 slice): module wiring + local-store migration + Host argv conversion

**Scope (one mechanical commit per step, all suites green per commit):**

1. **HostPlatform argv-only conversion (16 `sh -c` sites).** All git/SSH
   execution goes through `LocalProcess::Start2` with argv vectors. Every
   user-supplied input (paths, refs, hosts, commands) is validated before
   argv construction; SSH remotes keep their existing allowlist semantics.
   No shell anywhere in the package afterwards
   (`grep -rn 'sh -c\|"sh",' uppsrc/ManagerSkylark/Host*.cpp` → 0).
2. **SourceForge module wiring.** The SourceForge webapp's data access moves
   onto `SourceForgeForgeBackend` (Allura REST, injectable transport;
   real base URL `https://sourceforge.net`; the adapter's existing config
   carries the tool mount and credentials — none configured = read-only).
   Views show explicit unavailable/empty states; scraped snapshot data
   (`~/.aria/sites/**/summary.json`) stays read-only and labelled snapshot.
3. **PrivateHub module wiring.** The PrivateHub webapp's issues move onto
   `PrivateHubWorkspaceBackend` (site-scoped store), workspace root and
   org/repo mapping configurable; live working-tree views keep using it.
4. **Local SourceForge store migration (one-shot, explicit).** Existing
   local stores under `~/.aria/sites/sourceforge/<project>/`
   (`tickets/wiki/discussion/files_store.json`) are imported into the
   PrivateHub site storage by a converter command/CLI flag with a
   `--test`-covered fixture; originals are never modified or deleted by the
   converter. Record provenance = migrated-from-legacy.
5. **Fence suite additions:** a SourceForge site and a PrivateHub site in the
   test host (stub transport / temp workspace), asserting the usual fences
   (identical paths across sites, site-scoped storage, unavailable states).
   Update CI if the fixture needs anything new (keep it POSIX-sh portable).

**Out of scope:** HostPlatform behavior changes beyond argv mechanics; T02+
(Witz/assets); PR-contract promotion in ForgeCore; anything Company-side.

**Definition of done:** zero `sh -c`/`"sh",` sites in HostPlatform; both
modules wired to their backends with migration covered; fence suite
extended (green), all three suites green per commit (CLANG) + GCC rc 0;
log entry with commit ids; then STOP and wait. After acceptance this
completes T01; the next brief starts T02 (Witz/assets).

*Note:* if real-network SourceForge behavior cannot be exercised locally,
stub-transport coverage plus a manual smoke note is acceptable; record the
limitation in the log.
