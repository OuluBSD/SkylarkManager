# Science Package Guide

`uppsrc/Science` is the arXiv-style discovery/reading webapp over the
existing paper corpus (default `~/Dev/AnthologyPrivate/site/data`, injectable
for tests): repository-backed metadata records plus agent-authored
**structured article trees**.

## File Map

| File | Role |
|------|------|
| `ScienceModel.h/.cpp` | `SciencePaper` (flat corpus record mapping), article-tree node model + `science/article-tree` schema card, `ScienceCorpus` (read-only flat corpus reader; article sidecar read/write with optimistic revisions and atomic writes) |
| `ScienceSite.h/.cpp` | `InstallScienceSite`: read routes on an `EngineSite` (HTML paper list/detail, JSON API, article tree) |

## Real-corpus smoke

Read-only check against the real corpus, run by hand (not in the fence suite):

```bash
./bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/WebEngineTests \
    --science-corpus ~/Dev/AnthologyPrivate/site/data 150
```

It lists the corpus only; the article root is the corpus directory and nothing
is written. Unparsable or id-less files are skipped with a LOG line. The corpus
has no `.gitignore`, so `*.article.json` sidecars are git-visible by design.

## Binding rules

1. **Corpus is read-only here**: metadata records keep their flat on-disk
   shape (`{...fields..., "envelope":{...}}`, schema `skylark/paper`). This
   package never rewrites them and never "migrates" them to another store
   shape (a store that expects nested envelope/data would quarantine them).
2. **Full texts are sidecar article trees**: `<paper-id>.article.json` holds
   `{"envelope":{schema_id "science/article-tree", ...},"article":{...}}`,
   schema-validated, written atomically with an optimistic expected revision.
   A paper without a sidecar is shown as *metadata-only* — never rendered as
   if full text existed.
3. **Provenance and stability**: article trees reference the paper's current
   envelope revision; TaskFoundry-style consumers cite paper revision +
   node id. Node ids are stable strings; renames are editorial events, not
   silent rewrites.
4. **Defensive corpus reading**: corrupt corpus files are skipped with LOG,
   never quarantined or deleted (the corpus is git-backed elsewhere).
5. **Authoring is deny by default**: `POST api/papers/<id>/article` and the
   `papers/<id>/author` form require either a session user with the
   `science-author` role, or an active meta-account grant carrying
   `science.author` presented in the `x-science-grant` header. The grant is
   caller-bound: it authorizes only the authenticated session of its
   `subject_user_id`. A grant id presented by another session, or without a
   session, is refused (403). Writes go through `ScienceCorpus::SaveArticleTree` (schema,
   optimistic `expected_paper_revision` -> 409 on drift, atomic sidecar write).
   Status codes: 403 unauthorized, 404 unknown paper, 400 invalid body or tree,
   409 drift, 500 write failure.
