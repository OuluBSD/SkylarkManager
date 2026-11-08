# WORKER BRIEF 06A — planner re-scope of Brief 06 steps 3–5

**Step 1–2 acceptance:** approved (including the out-of-brief HostViews.cpp
include fix — the clean-worktree build failure was real — and the 18-site
conversion where the brief said 16). The partial git-subcommand allowlist
residual risk is noted and acceptable for now (tightening is a backlog item).
Live sourceforge.net smoke: deferred as a manual follow-up, recorded.

**Decision on your question:** steps 3–5 assumed a PrivateHub webapp; none
exists yet, and composing the engine host into ManagerSkylark belongs with
the PrivateHub UI work (T04). Re-scoped as follows — **no web UI, no engine
site composition in this slice.**

## Assignment 6A (replaces Brief 06 steps 3–5)

**Scope (one commit per step, all suites green per commit):**

1. **Define the missing PrivateHub record kinds** in `PrivateHubWorkspace`:
   schema cards + storage for `privatehub/wiki-page`,
   `privatehub/discussion-topic`, `privatehub/release-file` (alongside the
   existing issues). Minimal but sufficient fields:
   - wiki-page: page slug, title, wikitext body, revision, updated.
   - discussion-topic: topic slug, title, author, created; posts as child
     records (author, body, created).
   - release-file: file name, size, download url, release label, updated.
   Envelope-style records with revisions; list/get/put via the site store.
   Document the kinds in the package AGENTS.
2. **Migration converter**: `ManagerSkylark` CLI flag
   `--migrate-sf-local <legacy-dir> <privatehub-data-root>`: one-shot import
   of `~/.aria/sites/sourceforge/<project>/{tickets,wiki,discussion,files}_store.json`
   into PrivateHub records in `<privatehub-data-root>`. Originals are never
   modified or deleted. Record provenance `migrated-from-legacy` (source
   path in `provenance_ref`). Corrupt/absent stores are reported, never
   guessed. Fixture + `--test` coverage (temp legacy dir shaped like the
   real one).
3. **PrivateHub broker service in ManagerSkylark**
   (`PrivateHubService : IBrokerService`): read ops over the backend for
   issues, wiki pages, discussion topics, release files (list/get) plus
   issue create/comment. Registered under the site id the config gives; this
   is the surface TaskFoundry (T04) and the future PrivateHub UI will
   consume. `--test` coverage via a temp workspace + temp data root.
4. **Deferred, documented**: PrivateHub engine-site UI + fence additions and
   the engine-host composition move to T04; live sourceforge.net smoke is a
   manual follow-up item; git-subcommand allowlist tightening is backlog.

**Definition of done:** converter + service implemented and tested; record
kinds documented; all three suites green per commit (CLANG), GCC rc 0 for
all three targets; log entry with commit ids; then STOP and wait.

**After acceptance T01 is complete** (with the deferrals above recorded in
the log and README). Next brief starts T02 (Witz/assets).
