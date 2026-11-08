# GitHubForge Package Guide

`uppsrc/GitHubForge` is the official-github adapter for the forge domain
suite: implements `IForgeBackend` against the real github.com through the
`gh` CLI, executed argv-only (never shell).

## File Map

| File | Role |
|------|------|
| `GitHubForge.h/.cpp` | `GitHubForgeBackend` (issue list/get/create/comment/state via `gh api`; pull request list/get/diff/create/state/merge, repo list via `gh repo list`, workflow run list via `gh api`), GitHub JSON mapping, ISO-8601 time parsing, repo-slug and login/branch validation |

## Binding rules

1. **Argv only, never shell**: repository slugs, issue ids, titles and bodies
   are separate argv entries via `LocalProcess::Start2`; retained output is
   bounded; timeouts kill the child (same proven pattern as CompanyGitHub).
2. **Remote provenance or nothing**: a successful call reports
   `provenance = remote`; any failure (missing gh, non-zero exit, bad JSON)
   reports `unavailable` with the diagnostic. No synthetic fallback.
3. **Pull requests are not issues**: list results containing the
   `pull_request` marker are filtered out.
4. **Strict validation before execution**: `owner/repo` slugs and numeric
   issue ids are validated before argv construction.
5. **Testable without network**: the gh executable path is injectable
   (constructor or `GHFORGE_GH` env); tests use a fake gh that prints canned
   JSON, never live credentials.
