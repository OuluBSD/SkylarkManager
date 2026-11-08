# T03 — WP6b: Science authoring + real corpus binding

## Goal

Make the Science webapp fully functional: bind to the real corpus
(`~/Dev/AnthologyPrivate/site/data`, 190 papers), add agent-authoring
endpoints for structured article trees with proper authorization, and keep
repository-backed data intact.

## Preconditions

- T01 merged (engine host composed in ManagerSkylark) or a temporary test
  host binding Science on its own port.
- Accounts: meta-account consent grants (`WebGrants`) gate authoring.

## Work items

1. **Real corpus binding**: `ScienceCorpus` instantiated with the real
   `~/Dev/AnthologyPrivate/site/data` (configurable via CLI/ini, default for
   the composed host; tests keep fixture roots). Never write metadata
   records; article sidecars are new files in the same directory
   (git-visible by design — the AnthologyPrivate repo is the storage).
   Confirm `.gitignore` there does not exclude `*.article.json` (coordinate
   with the AnthologyPrivate repo owner if needed).
2. **Authoring endpoints** (on the Science site):
   - `PUT/POST api/papers/*/article` with body = article tree JSON.
   - Authorization: requires (a) an engine session whose account holds the
     Science author role, or (b) a broker-granted scope
     `science.author` from the meta-account consent flow. Deny by default.
   - CSRF/origin fence already enforced by the engine for mutations — reuse
     it; the handler validates the tree via
     `ScienceCorpus::SaveArticleTree` (schema + revision drift already
     covered by tests).
   - Optimistic concurrency: accept `expected_paper_revision` from the
     client; surface 409 on drift.
3. **Authoring UI**: minimal form (JSON textarea is acceptable initially;
   Witz form comes with T02) + link from the paper page.
4. **Broker service for Science** (`ScienceService : IBrokerService`):
   `get-paper`, `list-papers`, `get-article`, `put-article` (scope-gated) so
   TaskFoundry (T04) can cite/author without touching the corpus directly.
5. **Fence test additions**: author via session without grant → 403; with
   grant → saved; drift → 409; metadata-only paper stays renderable.

## Acceptance

- Real corpus listing works against `~/Dev/AnthologyPrivate/site` (smoke
  check: `ListPapers` returns ~190 records; corrupt/odd files are skipped
  with LOG, corpus untouched).
- Article authoring round-trips through the site with grant-gated auth;
  sidecars are valid, atomic, revision-checked.
- All suites green; CI green.

## Notes

- Do NOT point `WebStorage::JsonFileStore` at the corpus directory — it
  expects `{"envelope","data"}` and would quarantine flat records. The flat
  reader in `ScienceModel.cpp` is the only sanctioned reader.
