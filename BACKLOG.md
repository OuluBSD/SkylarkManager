# Backlog

Deferred work with the reason it is deferred. Newest entries first.

## Forge repository creation: done (task 022)

`GitHubForgeBackend::CreateRepository` runs `gh repo create` on the gh connector only, with
`POST /api/gh/repos/create` (admin session). The emulation refuses creation; there is no fake
success. The contract suite covers invalid names and both implementations.

## GitHub host routes: role matrix documented, roles deferred (task 022)

The role of each `api/gh` route is now written in `uppsrc/ManagerSkylark/AGENTS.md`. Only repo
creation checks a role. Repo roles (PR-only, merge rights) still belong with the Company project
and repo model. DECIDED (planner, 2026-11-11): `workflows/save` and `actions/trigger` stay signed-in only; no current threat model justifies narrowing them to admin.

## GitHub host routes: project-scoped role matrix

The stock host (`ManagerSkylark`) has one account type on its GitHub routes, so
"contributor cannot merge" cannot be asserted. Repo roles (PR-only, merge rights) belong with the
Company project and repo model. Until then, the AgentLab `github-pr-flow` scenario asserts only
anonymous and token-less refusals and merged-PR refusals.

Also deferred to WP4: the direct-push negative is a backend-level assertion, not a SiteCli one.
