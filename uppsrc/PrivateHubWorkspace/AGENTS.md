# PrivateHubWorkspace Package Guide

`uppsrc/PrivateHubWorkspace` is the PrivateHub backend for the forge domain
suite: the local workspace/file-manager side. Unlike GitHub and SourceForge
adapters, PrivateHub has no remote host — it inspects the live local
filesystem and running-tree state, and its issues/tickets are PrivateHub's
own records.

## File Map

| File | Role |
|------|------|
| `PrivateHubWorkspace.h/.cpp` | `PrivateHubWorkspaceBackend` (IForgeBackend over per-repo JSON issue stores), `RepoInfo`/`ListOrganizations`/`ListRepositories`/`RepoStatus` (branch + dirty state via argv-only `git -C ... status --porcelain`), path containment helpers |

## Binding rules

1. **Containment first**: organization/repository names and derived paths are
   validated (`[A-Za-z0-9._-]`, no "..", no separators) and the resolved path
   must stay under the configured workspace root. Repositories outside the
   root are never touched.
2. **Live working tree**: `RepoStatus` reports the real branch and dirty-file
   count through `git -C <path> status --porcelain` executed argv-only —
   never shell. Provenance for repo facts is `local`.
3. **PrivateHub owns its issue records**: issues live in the site-scoped
   `WebStorage` JSON store passed at construction — not inside the managed
   repositories, and not shared with other sites.
4. **No auto-mutations**: this package only reads git state; commits,
   branches, and file edits belong to explicit, guarded operations in the
   PrivateHub webapp (later work package).
5. **Tests use temporary fixtures**: `git init` inside a temp root; never
   ambient repositories.
