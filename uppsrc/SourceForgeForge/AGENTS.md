# SourceForgeForge Package Guide

`uppsrc/SourceForgeForge` is the official-SourceForge adapter for the forge
domain suite: implements `IForgeBackend` against the SourceForge Allura REST
API (`/rest/p/<project>/<tool>/...`).

## File Map

| File | Role |
|------|------|
| `SourceForgeForge.h/.cpp` | `ISfHttpTransport` + `SfHttpTransport` (HttpRequest/SSL), `SourceForgeForgeBackend` (ticket list/get/create/comment/state via Allura REST), Allura JSON mapping |

## Binding rules

1. **Transport is injectable**: the backend speaks to an `ISfHttpTransport`;
   the default implementation uses `HttpRequest` (SSL-capable). Tests supply
   a stub transport with canned Allura JSON — never live network or
   credentials.
2. **Reads are unauthenticated; writes are gated**: list/get/comment-list
   work anonymously; create/comment/state changes require configured
   `api_key` + `api_token` (Allura two-legged API auth) and are reported as
   unavailable provenance when missing, never faked.
3. **Allura shape mapping is defensive**: ticket fields and thread posts
   vary between Allura versions; missing fields map to empty values instead
   of crashing.
4. **Provenance is mandatory**: successes report `remote`; failures report
   `unavailable` with the transport diagnostic.
5. **Authoritative author**: the author of a locally created ticket/comment
   is the configured identity — request data cannot spoof it.
