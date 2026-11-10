# WORKER BRIEF 11A — from the planner

**Verdict on Assignment 10 (Brief 10): accepted — T03 is complete.**
Planner re-verified: the three T02 gap fixes (GitHubJsEscape; data-snippet +
dataset onclick; TPP topic fixtures), authoring endpoint deny-by-default
(science-author role or science.author grant), `ScienceService` scope
checks, real-corpus smoke (190 papers, checksum-proven read-only), fence
177/177, Manager PASSED (CLANG+GCC), Company ALL PASSED (CLANG) + GCC rc 0,
tree clean at 3b3c14d.

**Grant-semantics decision:** bearer-style grant IDs are acceptable interim
(single-process, fence-protected). **T04 requirement:** caller binding — a
grant is usable only by its subject's authenticated session (or per-caller
tokens) before any cross-site consumption. Authoring-page inline HTML is an
accepted interim; the witz form folds into T04's Science UI work.

---

## Assignment 11A — T04 first slice: engine-host composition + PrivateHub UI

**Scope (one commit per step, all suites green per commit):**

1. **Compose the engine host into ManagerSkylark.** The ManagerSkylark
   process hosts engine sites behind `WebEngineHost` on configurable engine
   ports (default 18201+; `--test` uses its own). Sites composed now:
   **PrivateHub** and **Science** (both have `Install*Site` installers).
   Requirements:
   - `SystemAuth::AuthorizeRequest` allowlist extended for the engine
     gateway handler ids (EngineGatewayGet/Post/Put/Patch/Head/Options) so
     composed sites are reachable while stock routes keep their global
     login enforcement.
   - Science broker service registered in the composed host; PrivateHub
     service likewise (site id from config).
   - Real corpus binding: ScienceCorpus points at
     `~/Dev/AnthologyPrivate/site/data` by default (CLI/ini overridable);
     article writes land as sidecars in the same directory — this is the
     first production write path, so keep the optimistic-revision + atomic
     discipline (already in `SaveArticleTree`).
   - Caller binding for science.author grants (the T04 requirement above):
     implement it now for the composed Science site (grant usable only by
     its subject's authenticated session) — the fence asserts it.
2. **PrivateHub UI** (PrivateHub site): issues list/detail/create/comment,
   wiki pages, discussion topics, release files — views + templates over
   `PrivateHubWorkspaceBackend`/site store; template root per site (T02
   infrastructure); same witz/escaping rules as the GitHub extraction.
3. **Fence additions:** PrivateHub + Science sites composed IN THE REAL
   ManagerSkylark process (not just the test host): identical-path fencing,
   site-scoped sessions/cookies/storage, grant caller-binding negative test
   (grant used by a different session → refused).
4. **Docs:** ManagerSkylark AGENTS route table gains the composed sites;
   README overview updated.

**Out of scope:** TaskFoundry webapp (Brief 11B), Company module + submission
op (11B), SameSite on other cookies, GitHub/SourceForge renderer work.

**Definition of done:** composed host runs PrivateHub + Science in the
ManagerSkylark process with per-site fencing proven by the extended fence
suite; Science authoring reachable in the real app with caller-bound grants
and real-corpus sidecar writes; all three suites green per commit (CLANG),
GCC rc 0 for all three targets; log entry with commit ids; then STOP and
wait. After acceptance: Brief 11B (TaskFoundry + Company submission).
