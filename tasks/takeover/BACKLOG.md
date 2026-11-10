# Backlog (recorded, not scheduled)

Items below were routed here by a worker brief. Each is recorded with its
source so a later brief can pick it up. No work is in progress on any of them.

## 1. Caller-bound grants replacing TaskFoundry's blanket scopes

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

- **What:** the host-side git subcommand check (`ManagerSkylark` host
  validation, before argv is built) is a partial allowlist. Git options that are
  not on the refuse list are not checked one by one. Remote strings still pass
  through the remote login shell, as ssh requires, built only from validated
  tokens.
- **Source:** WORKER-LOG, host argv-only conversion (`8f33310`), residual risk;
  WORKER-BRIEF-06A.

## 4. Idempotency-record expiry and a multi-process mutex

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

- **What:** the PrivateHub engine site serves reads, plus issue, comment, and
  discussion post writes. It has no authoring UI for wiki pages, discussion
  topics, or release files. Witz templates were routed with it.
- **Source:** WORKER-LOG (brief 11A open items, routed to brief 12); TaskFoundry
  uses PrivateHub read operations only, so this does not block it.
