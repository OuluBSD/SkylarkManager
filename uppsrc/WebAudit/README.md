# WebAudit

## Implemented

`WebAudit` is a site-scoped Core/WebStorage package. Version-1 events have a
closed set of bounded fields and reject query/fragment characters, oversized
identifiers, unbounded durations, and malformed categorical values. Event data
contains no place for request body, cookies, CSRF values, passwords, TOTP or
recovery values, bearer tokens, authorization headers, IPs, user-agent,
referrer, full URL, broker payload, or exception text.

The shared WebEngine dispatch boundary records matched registered route
patterns (or a fixed `UNMATCHED:/` code), method, status, duration, site, signed-in
user ID and a random opaque session reference. Request and generic mutation
action events are emitted after handlers; failed CSRF/origin checks are also
recorded. Audit API requests are excluded to avoid recursive read logging.
Writes use best-effort `Record`: storage exceptions/failures only set a
sanitized in-memory health flag and do not escape into request handling.

Session creation, adoption, logout/revocation and failed/expired cookie
resolution are recorded. A random `as_` reference is stored in session metadata
and is never a cookie credential. MetaAccount and local SSO adoption pass
through the adoption hook. Admins can query active per-site snapshots containing
only user ID, opaque ref, created time and expiry; session storage IDs and raw
cookie/CSRF values are not returned. Login success/failure events use
`password`, `password_totp`, `password_recovery`, `password_second_factor`, or
`sso` categories and omit
submitted usernames and credentials. Redirect calls are classified as
approved-local, approved-SSO, external, or rejected for invalid `next` attempts, without
persisting destination values. Mutations also produce generic action events
keyed by registered route.

Every `WebBroker::Call` is recorded as `broker_inprocess`, with source/target,
allowlisted-looking operation code, generated opaque correlation ID, result
category and duration. Payloads and error strings are not passed to the audit
observer. This is explicitly in-process; `CallRemote` remains a 501 stub and
is not labeled as network traffic.

The admin endpoints are `GET /api/audit/events`, `GET /api/audit/sessions`,
`GET /api/audit/health`, and `/audit`. The page renders the event list, active
session snapshot and recorder diagnostics through those APIs. Anonymous callers get 401 and signed-in
non-admins get 403.
Storage selection is the authenticated site context, never a caller-provided
site ID. Read endpoints set no-store headers. Event page size is at most 200,
offset at most 10,000, and a query range at most 31 days (default: last day).
Active-session snapshots paginate with the same offset bound and at most 200
sessions per page.

Per-site history is pruned to 30 days on append and has a hard configurable
upper bound of 10,000 events. Reaching the cap rejects new events instead of
discarding recent records. A separate append lock serializes retention/capacity
check plus append across processes; WebStorage provides per-record locking and
atomic file replacement. Storage permission modes are inherited from
WebStorage and the application data directory; deployment must ensure that
directory is private to the service account.

## Communication And SSO Inventory

`WebBroker::Call` calls `IBrokerService::Handle` synchronously in-process.
MetaAccount SSO validation, consent, grant checks and revocation use these
broker calls. Browser SSO continuation uses redirects and can cross listener
ports. Each composed site has a distinct `wfs_<site>` cookie; same-host ports
share browser cookie scope, while site-stamped session records and distinct
cookie names enforce the application fence.

SSO URL construction now uses each composed target site's configured
`MANAGERSKYLARK_ORIGIN_<SITE>` public origin and the host portal's
`MANAGERSKYLARK_ORIGIN_PORTAL`, not the request Host or forwarded headers.
Return URLs must match an exact configured site/portal origin and pass the
existing safe-path validation. Default origins remain localhost HTTP for local
development; public deployment must configure HTTPS origins. TLS proxy handling
continues to use task-056's exact `MANAGERSKYLARK_TRUSTED_PROXY_PEERS` socket
peer allowlist and trusted `X-Forwarded-Proto: https`; this change does not
broaden that trust.

The source scan found outbound HTTP in `SourceForgeForge` and local loopback
control/test requests in ManagerSkylark. No composed webapp-to-webapp HTTP
client was found. GitHub operations use the host adapter/CLI; inter-webapp
service calls use the in-process broker. These are inventory findings, not
claims that the host's external network traffic is audited.

ManagerSkylark's host-root dispatcher is also instrumented without moving any
host routes into WebEngine. Its independent store is site `portal` under
`<site_dir>/portal-audit`; it cannot read or write composed-site collections.
The host `/audit` UI and `/api/audit/{events,sessions,health}` endpoints require
`SystemAuthService` root-admin authorization, return 401/403 as appropriate,
and use no-store. The root UI identifies its `portal` scope and links to each
configured composed-site origin's independent audit page. Each composed API
continues to select its site from its EngineSite context. Root auth events cover
password failures/success, SSO bridge adoption/failure, session create/revoke,
expiry and failed resolution. In-memory host sessions carry a random non-bearer
ref; snapshots return only a pseudonymous user ID, ref and timestamps.

The host Skylark filter only supplies the resolved session/authorization
context. A narrow generic `SkylarkApp::RequestComplete` hook runs after the
`Http::Finalize` serialization/socket-write attempt, so the host audit producer
sees the final response code and elapsed request/response-finalization duration.
It records a fixed handler ID
or the fixed `host:unmatched` route for NotFound, with no path/query/body/header
values. The hook runs after exception handlers as well; observer exceptions
are swallowed after finalization and cannot change the response object.
Skylark's current `Finalize` does not report whether the peer received every
byte, so the audit records the finalized response status, not client receipt.
`test_mode` deliberately
bypasses root request logging.

`EngineSite::Url`, `PublicUrl`, CSRF Origin validation and host portal app links
are now pinned to configured public origins. A request Host can no longer pick
an IP-literal/local authority, change scheme, or affect redirect authority;
untrusted X-Forwarded-* headers are not consulted. Host-root session cookies
are Secure only on actual TLS or when the direct peer is in the same exact
`MANAGERSKYLARK_TRUSTED_PROXY_PEERS` IPv4 allowlist and XFP is exactly `https`.

## Remaining Gaps

- There is no dedicated expiry event from background/session cleanup. Composed-site expiry is recorded and tested when a presented cookie resolves expired; stale sessions never presented are not enumerated.
- Session refresh/rolling-expiry is not present in the existing session API and therefore has no producer.
- The generic completion hook runs after response serialization and socket write; a socket failure before hook invocation may leave that request without an audit event. Session refresh/rolling expiry is not implemented by the root session model.
- Root host sessions are in-memory and disappear on process restart; root audit snapshots reflect current process memory only. No background expiry event is emitted for sessions never presented.
- Explicit policy/grant/account mutation subtypes are not wired across every app handler; generic POST/action route events are present, and MetaAccount login/SSO has explicit categories.
- Redirect classifications are coarse; not every application redirect has a validation-aware approved/rejected producer. Destinations remain omitted.
- Broker operation categories are service-provided allowlist values for MetaAccount, Science, PrivateHub and Company; the base default is `unclassified`. Values outside the event schema are rejected. Payloads/errors are never passed to the observer.
- Broker calls with neither source nor target resolving to a registered site cannot be assigned to a site ledger and are not persisted.
- No per-route opt-out is exposed. `/api/audit/health` returns only the sanitized recorder-write boolean and a fixed statement separating operational broker diagnostics from the durable ledger; it does not expose filesystem paths or exception text.
- Tests cover composed and root HTTP dispatch/login/session creation/logout/expiry/failure, root 200/401/400/403/404 and throwing-handler 500 completion statuses/durations, unmatched-path query canary redaction, MetaAccount TOTP/recovery and grant failures, SSO origin/redirect classification, spoofed literal/local/DNS Host and Origin, broker success/failure/unknown targets, root and composed API admin/site scope, redaction, snapshots, schema bounds, persistence, retention, capacity, newest-first pagination and failed storage. The observer exception safety check is helper-level: the pinned Skylark tree has no isolated `Dispatch`/socket unit harness, and invoking its dispatcher requires a listener. Root MetaAccount adoption is unit-tested via the host account adoption hook; a full root-to-Meta browser bridge and public HTTPS browser SSO still lack end-to-end transport tests.

The current completion-hook follow-up was build-validated from the CompanySkylark/BazaarCore assembly, including both ManagerSkylark and WebEngineTests. The live HTTP suites were not rerun for this follow-up because concurrent ManagerSkylark listeners were already active; they were left untouched. The new root status/NotFound/500 cases are present in the host integration self-test and compiled, but require a safe test-server window for runtime confirmation.
