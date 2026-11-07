# ForgeCore Package Guide

`uppsrc/ForgeCore` is the shared, headless domain suite behind the GitHub,
SourceForge, and PrivateHub webapps: canonical record types, backend-qualified
identities, an explicit provenance model, and the backend capability
interface. It is Core-only (no Skylark, no network) so any webapp or headless
tool can use it.

## File Map

| File | Role |
|------|------|
| `ForgeCore.h/.cpp` | `ForgeBackend` enum, `ForgeRef`, `ForgeIssue`, `ForgeComment`, `ForgeIssueSummary`, `ForgeResult` (+ `Provenance`), `ForgeCapabilities`, `IForgeBackend`, `MemoryForgeBackend` (reference/tests) |

## Binding rules

1. **Backend-qualified identity**: `ForgeRef` always names the backend, the
   project/repository, and the native id. A GitHub issue #42 and a PrivateHub
   issue #42 are different records even with equal numbers; cross-backend
   links are explicit records elsewhere, never implicit.
2. **Provenance is mandatory**: every result states whether its data came
   from the remote service, a local persistent store, a scraped snapshot, a
   synthetic fixture, or is unavailable. Synthetic data must never be
   presented as remote state (this replaces the old silent-fallback pattern).
3. **Capabilities are declared**: a backend states which operations it
   supports; unsupported operations fail with `provenance = unavailable`
   instead of being faked.
4. **Errors carry context**: `ForgeResult` includes the operation, the ref,
   and a diagnostic string; list operations return per-item results only via
   documented out-collections.
5. **Value types only**: all records are Moveable with explicit copy
   operations (U++ Vector members are pick-only) and Jsonize for wire/storage
   use.
