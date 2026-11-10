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
