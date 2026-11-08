# T01 — WP4: ManagerSkylark module split + engine host wiring

## Goal

Turn `uppsrc/ManagerSkylark` from a monolithic app into a **module
collection**: generic packages stay reusable, web apps become engine sites of
one composed host. All existing URLs, `--test` checks, and behavior are
preserved.

## Preconditions

- Your own work is committed and the first agent's staged files are committed
  or archived (coordinate: `git status` in SkylarkManager must be clean of
  foreign staged entries before you start refactoring `uppsrc/ManagerSkylark`).
- Baseline green: fence suite 116/116, ManagerSkylark `--test` PASSED.

## Work items

1. **Split by responsibility** (behavior-preserving; do this in several
   commits, each keeping `--test` green):
   - `GitHubPlatform.cpp` (~3.5k lines) → GitHub webapp module: domain calls
     move to `GitHubForge` where they map (live `gh` issue ops), synthetic
     fallbacks (fabricated PR #7/diff, hardcoded build cache) become
     provenance-labelled or removed; routes stay.
   - `SourceForgePlatform.cpp` (~1.6k) → SourceForge module; local
     tickets/wiki/discussions become PrivateHub-style local records (see
     plan decision: local stores are PrivateHub data, not official
     SourceForge).
   - `HostPlatform.cpp` (~2.2k) → keep host/system inspection inside
     ManagerSkylark; extract the duplicated local-repo inspection into
     `PrivateHubWorkspace` service calls where it maps.
   - `WebViews.cpp`, `RestApi.cpp` (keep `CONSOLE_APP_MAIN` in the main
     package), `ManagerSkylark.cpp` — route handlers into per-app handler
     files/subpackages (`ManagerSkylark/VersionControl/GitHub` etc.).
   - Break the umbrella-header cycle: `GitHubPlatform.h`/`SourceForgePlatform.h`
     include `ManagerSkylark.h` which includes them; domain headers must not
     depend on the app header.
2. **Route registration retention**: routes currently register via `SKYLARK`
   macros in large TUs. If you move route-only TUs into static-library
   packages, verify retention (Shared builds mask this; run a non-Blitz
   build `-bs` once to check). Prefer keeping route adapters in the
   executable package.
3. **Compose the host**: `ManagerSkylark` keeps its stock port + app routes
   (stock Skylark behavior), and the new engine sites register behind
   `WebEngineHost` on separate ports, e.g. github 18201/sourceforge
   18202/privatehub 18203 (align with the fence test); the existing UI links
   to them. FilterHandler (`SystemAuth::AuthorizeRequest`) must add the
   engine gateway handler ids to its allowlist so engine sites are reachable
   (`EngineGatewayGet`, `EngineGatewayPost`, ... — see Host.cpp names).
4. **Wire the adapters**: the GitHub app uses `GitHubForgeBackend`; PrivateHub
   uses `PrivateHubWorkspaceBackend`; SourceForge uses `SourceForgeForgeBackend`.
   Existing local SourceForge stores under `~/.aria/sites/sourceforge/**`
   migrate explicitly into the PrivateHub site storage with a one-shot
   converter + `--test` coverage; synthetic GitHub fallback data is removed
   or clearly labelled `synthetic`.
5. **Candidate boundary**: keep `candidate_writes_disabled`; do not revive
   `#if 0` write paths.
6. Update `AGENTS.md` (root package table) and `uppsrc/ManagerSkylark/AGENTS.md`
   to the new layout.

## Acceptance

- `./bin/ManagerSkylark --test` PASSED with all pre-existing checks intact
  (route URLs unchanged).
- Fence suite extended: ManagerSkylark composed host runs the github/
  sourceforge/privatehub sites with the same isolation guarantees
  (identical paths across sites still fenced).
- No file larger than ~800 lines in ManagerSkylark; no umbrella-header cycle.
- Both repos build with CLANG + GCC; CI green.

## Risks

- `SystemAuth` allowlist: engine gateway ids must be public or engine sites
  403.
- RestApi.cpp contains `CONSOLE_APP_MAIN` — moving it breaks the executable.
- The SourceForge URL namespace owns `projects/*` while Manager owns
  `/projects` — preserve exact URLs.
