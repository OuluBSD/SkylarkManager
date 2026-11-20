# WebStorage Package Guide

`uppsrc/WebStorage` is the generic, Core-only persistence foundation for the
composable website engine: site-scoped record collections, schema registry,
and safe JSON-file storage. It has no Skylark dependency so any host (web or
headless) can use it.

## File Map

| File | Role |
|------|------|
| `WebStorage.h/.cpp` | `StoreResult`, `RecordEnvelope`, `IRecordCollection`, `JsonFileStore` (per-record JSON files, inter-process lock, atomic writes, quarantine), `MemoryStore`, `SchemaRegistry`, `StoreManager` |
| `WebStorage.h/.cpp` (`BlobStore`) | Binary blobs beside the JSON records: metadata (`BlobRecord`: id, SHA-256, byte size, width, height, content type) in the collection, bytes in `<collection>/<id>.blob`; upload limit; checksum checked on read |

## Invariants

1. **Site scoping is by construction**: a store instance is rooted at one
   site's data root. Cross-site access requires the WebBroker, never direct
   store sharing.
2. **Every write** takes the in-process mutex AND the inter-process file lock
   (`flock` on POSIX, `LockFileEx` on Windows), writes a unique temp file,
   fsyncs, and atomically renames. Readers never see partial documents.
3. **Optimistic revisions**: `Put`/`Delete` accept `expected_revision`;
   mismatch is a conflict error, never a silent overwrite.
4. **Corrupt files are quarantined** (renamed `.corrupt-<timestamp>`) and
   reported, never deleted silently.
5. **Ids are validated** (`[A-Za-z0-9._-]`, no path separators) before they
   ever touch the filesystem.
6. **Schema validation is opt-in per collection** via `SchemaRegistry`; valid
   JSON with an unknown schema is stored, not rejected (forward compatibility).
7. **U++ JSON idioms used here** (they bite otherwise):
   - `StoreAsJson(jsonizeStruct)` emits plain JSON, but `StoreAsJson(ValueMap)`
     or `AsJSON` of a `StoreAsJsonValue` result emits Upp's typed debug format
     (`{"key":{"type":...},"value":...}`) which parses back as an ARRAY, not
     an object. Plain JSON for map values: `AsJSON(Value(map))`; for Jsonize
     structs: `StoreAsJson(struct)` directly.
   - `Vector`/`VectorMap` members are pick-only: record structs stored in
     containers need `Moveable<T>` plus explicit copy ctor/`operator=` with
     `clone(...)` (see CompanyModel.h for the house pattern).
   - `TcpSocket::Connect` may return true on `EINPROGRESS`; and a freshly
     accepted socket with the default zero-wait timeout can close before the
     client's request bytes arrive (engine listener sets a 30 s read timeout
     after accept).
