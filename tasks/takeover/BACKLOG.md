# Backlog (recorded, not scheduled)

Items below were routed here by a worker brief. Each is recorded with its
source so a later brief can pick it up. All five are done as of brief 13; each entry records its commit.

## 1. Caller-bound grants replacing TaskFoundry's blanket scopes

- **Status: done** (brief 13 item 1, `df1e4b7`). TaskFoundry scopes come from
  caller-bound grants; the blanket member scopes are gone. Residual: grants are
  approved out of band with the operator flags `approve-taskfoundry-grant` and
  `revoke-taskfoundry-grant`. MetaAccount is not composed in the real process,
  so consent is an operator action, not a user action in the UI.

- **What:** a signed-in TaskFoundry member currently gets every TaskFoundry
  scope (`TaskFoundryMemberScopes()`, `uppsrc/TaskFoundry/TaskFoundryService.h`).
  Replace it with grants bound to the calling user, so each scope needs a grant
  that names that user and the operation.
- **Status of the decision:** the blanket scopes are an accepted interim
  policy (planner decision, brief 11B). TaskFoundry stays science.read-only;
  it makes no science.author writes, so the science caller-binding machinery
  has no TaskFoundry call to bind yet.
- **Source:** WORKER-BRIEF-11B / WORKER-BRIEF-12.

## 2. Raw-URI accessor on `Http`

- **Status: done** (brief 13 item 2). `Http::GetRequestURI()` is in the BazaarCore
  Skylark package (`4d8d833`, BazaarCore repo). The gateway routes on the raw
  target (`abca36e`, SkylarkManager). Stock routes shadowing is fixed, and the
  404 policy for engine listeners is kept. The patch set in `patches/` is unchanged.

- **What:** the stock Skylark route matcher is not served on engine listeners
  because the gateway rebuilds the request path from wildcard arguments, so a
  stock route with literal segments serves the wrong page. The fix needs a
  public raw-URI accessor on `Http` in the ai-upp (BazaarCore) package, since
  the header keeps it private.
- **Until then:** keep the rule that engine listeners answer stock routes with
  404 except the home route (`uppsrc/WebEngine/AGENTS.md`).
- **Route:** a BazaarCore additive patch.
- **Source:** `uppsrc/WebEngine/AGENTS.md` (raw-URI note); WORKER-LOG (brief 11A
  routing).

## 3. Git-subcommand allowlist tightening

- **Status: done** (brief 13 item 3, `1fff8b6`). Each git subcommand has its own
  argv allowlist, and unknown options are refused before argv is built. Remote
  strings reach the login shell only as single-quoted validated tokens.
  `HostExecSelfTests` adds 85 checks.

- **What:** the host-side git subcommand check (`ManagerSkylark` host
  validation, before argv is built) is a partial allowlist. Git options that are
  not on the refuse list are not checked one by one. Remote strings still pass
  through the remote login shell, as ssh requires, built only from validated
  tokens.
- **Source:** WORKER-LOG, host argv-only conversion (`8f33310`), residual risk;
  WORKER-BRIEF-06A.

## 4. Idempotency-record expiry and a multi-process mutex

- **Status: done** (brief 13 item 4, `8d3b8e5`). Records expire after 30 days
  (`PRIVATEHUB_IDEMPOTENCY_TTL`, a constructor parameter). Expired keys run again
  and overwrite. A data root has one owner process, enforced by an exclusive lock
  on `owner.lock`. A second owner is refused at registration. Residual: the owner
  lock is covered on POSIX only. Its Windows path (`LockFileEx`) is written but
  not built or run here.

- **What:** the idempotency record store in ManagerSkylark has no expiry, and
  its mutex serializes only one process. Two processes sharing a data root can
  race a duplicate key.
- **Scope note:** CompanyStore is not in this gap. `JsonStore` takes an
  inter-process file lock on every write (since brief 12 step 2 there is also a
  lock on `secret.key` creation). The gap is the ManagerSkylark idempotency
  store.
- **Source:** WORKER-LOG, the known-gaps list for the PrivateHub/engine broker
  work (brief 06).

## 5. PrivateHub wiki, topic, and release authoring UI

- **Status: done** (brief 13 item 5, `caa2758`). Member-role writes for wiki
  create/edit (edit must name its revision), discussion topic create, and release
  file register. Each create refuses an existing key. Residual: the smoke covers
  the member path only. A login without the member role is refused by the
  handler, but no test exercises that denial.

- **What:** the PrivateHub engine site serves reads, plus issue, comment, and
  discussion post writes. It has no authoring UI for wiki pages, discussion
  topics, or release files. Witz templates were routed with it.
- **Source:** WORKER-LOG (brief 11A open items, routed to brief 12); TaskFoundry
  uses PrivateHub read operations only, so this does not block it.

## 6. User-facing grant consent (MetaAccount composition)

- **What:** TaskFoundry grants are approved only through the operator CLI
  (`approve-taskfoundry-grant`, `revoke-taskfoundry-grant`). MetaAccount is not
  composed into the real host process, so a user cannot consent to a grant in the UI.
- **Decision (planner, brief 14):** interim operator-CLI consent stays. User-facing
  consent means composing MetaAccount into the host; that is a later decision.
- **Source:** WORKER-BRIEF-13 item 1 residual; WORKER-BRIEF-14 residual decision 1.

## 7. Windows LockFileEx runtime validation

- **What:** the PrivateHub data-root owner lock (`DataRootOwner`, `owner.lock`) has a
  Windows path using `LockFileEx`. It is compile-only coverage. Its runtime behavior
  is not run on Windows, since no Windows runner executes the tests.
- **Decision (planner, brief 14):** deferred. POSIX is the supported path until a
  Windows runner can execute the tests.
- **Source:** WORKER-BRIEF-13 item 4 residual; WORKER-BRIEF-14 residual decision 2.

## 8. Login-only PrivateHub writes (issues, comments, discussion posts)

- **What:** issue create, issue comment, and discussion post are still gated by login
  only (`RequireUser`), not the member role. Item 5 gated only the authoring endpoints
  it added (wiki, topic, release file). Under the rule that every mutation checks the
  actor's role, these three need the same gate.
- **Status:** not changed in brief 14, because the brief names only the authoring
  endpoints. Needs a planner decision on whether viewers may comment.
- **Source:** found while writing the viewer-denial fence (brief 14 step 0).

## 9. Stale `MT` mainconfig in CompanySkylark

- **What:** `uppsrc/CompanySkylark/CompanySkylark.upp` still declares `mainconfig "" = "MT";`.
  Multithreading is always compiled in, so the `MT` setting does nothing. The mainconfig
  itself is kept only because the package builds a standalone main behind `flagMAIN`.
- **Status:** not changed in brief 14. The change is a CompanySkylark edit, and it needs a
  planner decision on whether the mainconfig stays with an empty value or is removed.
- **Source:** found while verifying the UppSkillz build skill (brief 14 step 2).
