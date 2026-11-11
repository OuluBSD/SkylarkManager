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
   session. In the linked-consent shape (standard since task 009) the grant's
   `client_user_id` is the session user and its subject is the meta identity
   the user linked; a legacy/operator grant carries an empty `client_user_id`
   and its subject is the session user. Without an approved grant, pages
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

- `grants` shows the caller's TaskFoundry grants. `grants/request` links the
  signed-in user to a meta identity (the form takes meta username, password
  and, when enrolled, a TOTP code), verifies the credentials through the
  broker `authenticate` op, and files the request with the meta user id as
  subject and the session user as `client_user_id`. The pending request is
  decided by its subject on the MetaAccount site (consent page), which also
  revokes: the next TaskFoundry request sees the change.
- The operator CLI (`ManagerSkylark --approve-taskfoundry-grant <wag_id>`,
  `--revoke-taskfoundry-grant`) stays as a documented fallback for bootstrap
  and tests; it refuses grants for other sites. Legacy grants (empty
  `client_user_id`, subject = the session user) keep working.
- The meta grant store is the same one Science uses. Composition passes it to
  `InstallTaskFoundrySite`.

## Status

Brief 11B step 4 composed the site into the real process. Backlog item 1
(caller-bound grants replacing blanket scopes) is done under brief 13 item 1.
The self-tests use contract stand-ins for Science, PrivateHub and Company.
