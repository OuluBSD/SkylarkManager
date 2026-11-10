# AGENTS - TaskFoundry

**Scope**: `uppsrc/TaskFoundry`, the engine site for the candidate-to-submission
workflow. Sibling of Science and PrivateHubWorkspace.

## Rules

1. **Broker only across sites.** Science, PrivateHub and Company are reached
   through `WebBroker`, never by sharing objects or stores.
2. **Deny by default.** Every mutation needs `taskfoundry:write`. Each broker
   call is made with the caller's scopes, and the target site checks them.
3. **Submitted snapshots are immutable.** A submission stores its snapshot once.
   Later edits change the live proposal, never the snapshot.
4. **Idempotent submit.** The submission id derives from the proposal id and the
   content hash. A repeat submit returns the prior submission. The Company call
   uses the content hash as its idempotency key.
5. **Records are plain ValueMaps.** `StoreAsJsonValue` degrades vectors of
   structs, so records are written with `AsJSON(ValueMap)`.

## Status

Step 3 of brief 11B. Routes are installed by `InstallTaskFoundrySite`. Composition
into a process, and the real Company broker, are step 4. The self-tests use
contract stand-ins for Science, PrivateHub and Company.
