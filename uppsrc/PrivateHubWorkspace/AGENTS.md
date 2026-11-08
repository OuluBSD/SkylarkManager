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

## Record kinds (site store)

All kinds live in the site-scoped `WebStorage` store as envelope records (see
`WebStorage` `RecordEnvelope`). Collections are `<kind>-<project>` with `/`
flattened to `__`. Schema cards are registered by `RegisterSchemas()`.

| Kind (collection) | Record id | Data fields | Child records |
|---|---|---|---|
| `privatehub/wiki-page` (`wiki-`) | page slug | `slug`, `title` (required), `body` (wikitext), `updated`, `provenance` | none |
| `privatehub/discussion-topic` (`discussion-`) | topic slug (no `.`) | `slug`, `forum`, `title` (required), `author` (required), `created`, `provenance` | posts |
| `privatehub/discussion-post` (child of a topic, same collection) | `<topic>.<post_id>` | `post_id`, `topic`, `author`, `body` (all required), `created`, `provenance` | none |
| `privatehub/release-file` (`release-`) | file name (dots allowed) | `name` (required), `release`, `size_bytes`, `url`, `updated`, `provenance` | none |
| issues (`issues-`) | `phi_<uuid>` | `ForgeIssue` fields | comments `<issue>.<comment>` |

Rules:

1. **Revisions are envelope revisions.** The `revision` field on the C++ structs
   is filled on read and never stored in data. A `Put*` with a non-empty
   expected revision is a conflict check; an empty one creates or overwrites.
2. **Provenance is data plus envelope.** `provenance` is `local` for records
   made in PrivateHub, or `migrated-from-legacy` for imports. The envelope
   `provenance_ref` holds the source path for imports.
3. **Children are found by prefix.** Top-level listings skip ids containing
   `.`; post listings take ids starting with `<topic>.`. Topic slugs cannot
   contain `.` for this reason. The issue listing skips comment records too.
4. **Ids and keys are validated before the store.** Record ids are
   `[A-Za-z0-9_-]` (plus `.` for release file names). Project keys are one or
   two `[A-Za-z0-9._-]` segments. Traversal names are rejected.
5. **Writes go through the store.** `PutRecord` validates data against the
   schema card and writes the envelope through `JsonFileStore::PutOpaque`, which
   takes the lock, writes a temp file, fsyncs, and renames. Nothing writes
   the files directly.
6. **Corrupt records fail loudly.** A listing that meets a corrupt record
   returns a failed `ForgeResult` naming the record. It does not skip it.

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
