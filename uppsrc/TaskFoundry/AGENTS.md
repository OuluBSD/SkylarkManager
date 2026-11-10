# AGENTS - TaskFoundry

**Scope**: `uppsrc/TaskFoundry`, the engine site for the candidate-to-submission
workflow. Sibling of Science and PrivateHubWorkspace.

## Rules

1. **Broker only across sites.** Science, PrivateHub and Company are reached
   through `WebBroker`, never by sharing objects or stores.
2. **Deny by default.** Every mutation needs `taskfoundry:write`. Each broker
   call is made with the caller's scopes, and the target site checks them.
   A signed-in user holds scopes only through an approved TaskFoundry grant
   (`client_site` `taskfoundry`) in the meta grant store, bound to the user's
   session: the grant's subject must be the session user. Without one, pages
   redirect to `grants` and writes get 403. Revoking the grant takes effect on
   the next request. `science.author` is never grantable, and TaskFoundry never
   writes Science.
3. **Submitted snapshots are immutable.** A submission stores its snapshot once.
   Later edits change the live proposal, never the snapshot.
4. **Idempotent submit.** The submission id derives from the proposal id and the
   content hash. A repeat submit returns the prior submission. The Company call
   uses the content hash as its idempotency key.
5. **Records are plain ValueMaps.** `StoreAsJsonValue` degrades vectors of
   structs, so records are written with `AsJSON(ValueMap)`.

## Grants

- `grants` shows the caller's grants. `grants/request` files a request for the
  grantable scopes. Approval is out of band: an operator runs
  `ManagerSkylark --approve-taskfoundry-grant <wag_id>` (or
  `--revoke-taskfoundry-grant`). The CLI refuses grants for other sites.
- The meta grant store is the same one Science uses. Composition passes it to
  `InstallTaskFoundrySite`.

## Status

Brief 11B step 4 composed the site into the real process. Backlog item 1
(caller-bound grants replacing blanket scopes) is done under brief 13 item 1.
The self-tests use contract stand-ins for Science, PrivateHub and Company.
