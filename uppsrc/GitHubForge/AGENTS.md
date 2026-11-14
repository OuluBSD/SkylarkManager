# GitHubForge Package Guide

`uppsrc/GitHubForge` owns the GitHub backend, host-integrated routes and views,
their tests, and the witz templates. `GitHubForgeBackend` is an abstract
interface over `IForgeBackend` with two implementations, chosen per
process by `GitHubForgeBackend::Configure` (ManagerSkylark `--github-backend`
`gh|emulated`, default `gh`) and obtained through `GitHubForgeBackend::Create()`.

## File Map

| File | Role |
|------|------|
| `GitHubForge.h/.cpp` | Abstract `GitHubForgeBackend`: full op surface (issues, pull requests, repos, runs), process-wide `Config`, `Create()` factory, GitHub JSON mapping, ISO-8601 time parsing, repo-slug / login / branch validation |
| `GitHubGhConnector.h/.cpp` | `GitHubGhConnector`: github.com through the argv-only `gh` CLI (`GhRun` over `LocalProcess::Start2`) |
| `GitHubEmulatedBackend.h/.cpp` | `GitHubEmulatedBackend`: stateful local emulation, no network, no gh executable; JSON store with flock and atomic replace |
| `GitHubPlatform.*`, `GitHubActions.cpp`, `GitHubRepo*.cpp/.h`, `GitHubRoutes.cpp`, `GitHubWorkflowViews.cpp`, `GitHubUserView.cpp` | GitHub web routes, service mapping, and page views |
| `GitHubForgeHost.h/.cpp` | Injected navigation, auth/menu, markdown, ManagerStore, and Agent dashboard callbacks; the package has no dependency on ManagerSkylark |
| `GitHub*Tests.cpp` | Adapter and backend contract tests run as part of `ManagerSkylark --test` |
| `GitHubTemplates.*`, `github-*.witz`, `deploy-templates.sh` | Template renderer, view templates, and executable-directory deployment script |

## Binding rules

1. **Argv only, never shell**: repository slugs, issue ids, titles and bodies
   are separate argv entries via `LocalProcess::Start2`; retained output is
   bounded; timeouts kill the child (same proven pattern as CompanyGitHub).
2. **Provenance is never faked**: a connector success reports `remote`; an
   emulation success reports `local-persistent`; any failure reports
   `unavailable` with the diagnostic. No fallback between implementations.
3. **Pull requests are not issues**: issue listings exclude pull requests
   (the `pull_request` marker for the connector; separate records in the emulation).
4. **Strict validation before execution**: `owner/repo` slugs and numeric
   ids are validated before argv construction or store lookup.
5. **Testable without network**: the connector's gh path is injectable
   (constructor or `GHFORGE_GH`); the connector's shared-contract run uses a
   canned gh only because it has to execute one. The emulation needs no
   executable at all, and the capture harness runs on it.
6. **One store, many writers**: every emulated operation holds an exclusive
   `flock` on `github-emulated.lock` for its whole read-modify-write; mutations
   replace `github-emulated.json` through a temp file, fsync and rename. Without
   POSIX locks the emulation refuses to run.
7. **Shared contract**: `GitHubBackendContractTests.cpp` runs one
   body of expectations against both implementations on the fixture.
