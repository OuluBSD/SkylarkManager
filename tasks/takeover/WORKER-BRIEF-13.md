# WORKER BRIEF 13 — from the planner

**Verdict on Assignment 12: accepted — T05 + T06 are complete.** Planner
re-verified: CI carries CompanySkylark (checkout + `-a` for ManagerSkylark;
CompanySkylark jobs with MSVC compile-only); BACKLOG.md documents the five
items with sources; the four Company gaps fixed (platform_version
validation on load/update/append with quarantine, 0600 temp files +
secret.key chmod, locked deterministic secret.key first start, id doc
"32 hex"); fence 179/179; Manager PASSED (CLANG); Company ALL PASSED
(CLANG) + GCC rc 0; Company --test 220 checks (17 new). Self-notes
(GH_BASELINE requirement; YAML-parsed-only CI validation; mode caveats)
recorded.

---

## Assignment 13 — backlog sweep (the five recorded items)

**Scope (one commit per item; every commit gates: fence 179+,
ManagerSkylark --test PASSED (with the CompanySkylark -a), CompanySkylark
--test ALL PASSED (CLANG), GCC rc 0 for all three targets):**

1. **Caller-bound grants replacing TaskFoundry's blanket scopes.** A
   signed-in TaskFoundry member gets scopes only through grants bound to
   their session (per the Science caller-binding pattern). TaskFoundry
   stays science.read-only. Fence: signed-in member without a grant is
   refused a scope-gated op; with a grant it passes; the grant is
   revocable.
2. **Raw-URI accessor on Http** (BazaarCore additive patch: public
   `GetRequestURI()` returning `hdr.GetURI()`), and the WebEngine gateway
   uses it instead of catch-all-arg reconstruction. Document as the fix for
   the stock-route-shadowing limitation. Upstream-pristine rule intact;
   dry-run the patch application.
3. **Git-subcommand allowlist tightening** (HostExec): allowlist the full
   argv (subcommand + options), not just refuse-list options; unknown
   options rejected; `--test` coverage for refused and allowed forms.
4. **Idempotency-store expiry + multi-process note** (PrivateHubService):
   recorded results expire (TTL); document/enforce that one process owns a
   data root (flock on the data root at registration, second process
   refuses) instead of pretending the mutex is cross-process.
5. **PrivateHub authoring UI**: wiki-page create/edit, discussion topic +
   posts, release-file registration — over the backend, witz templates per
   site, same escaping rules, login-gated with the PrivateHub roles;
   smoke/fence coverage for one authoring path (wiki edit) end-to-end.

**Out of scope:** T07 UppSkillz (planner handles next); TaskFoundry UI
polish beyond item 1's grant flow; new features not in BACKLOG.md.

**Definition of done:** BACKLOG.md items marked done with commit ids (or
moved to a new residual backlog with reasons); all five implemented with
tests; all three suites green per commit (CLANG), GCC rc 0; log entry with
commit ids; then STOP and wait. After acceptance: the code plan is
COMPLETE — remaining plan item: T07 UppSkillz (planner-ordered next).
