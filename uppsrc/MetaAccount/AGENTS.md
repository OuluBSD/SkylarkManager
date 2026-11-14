# MetaAccount Package Guide

`uppsrc/MetaAccount` is the meta-account webapp (the ecosystem's
"accounting & authenticator", Google-account style): it owns ecosystem
identities, real TOTP two-factor enrollment/verification with single-use
recovery codes, and consent grants that client webapps must obtain through
the WebBroker. Client webapps keep their own lightweight accounts
(`WebAccounts`); linking happens through explicit consent grants.

## File Map

| File | Role |
|------|------|
| `MetaAccountSite.h/.cpp` | `InstallMetaAccountSite` (routes on an `EngineSite`: login, TOTP enroll/confirm/disable, consent list/decide, inbox list/read-all), `MetaAccountService` (broker ops: authenticate, request-consent, check-grant, revoke-grant, notify, ping), `RegisterMetaAccountService` |
| `MetaInbox.h/.cpp` | The identity inbox: subject-scoped `InboxEvent` records over a `JsonFileStore` (`engine/meta/inbox`), push/list/mark-read with retention (latest 200 per subject) and a type allowlist |

## Invariants

1. **Second factor is real**: enrollment is pending until a live TOTP code
   confirms it; login with an enrolled account requires password + code;
   recovery codes are single-use and consumed on success.
2. **Consent is explicit and revocable**: client webapps request grants via
   the broker; the user decides on the meta site; grants can be revoked and
   re-checked at any time. Idempotent requests return the live grant. The
   linked-consent shape (task 009) is the standard: the client site verifies
   the user's meta credentials through the broker `authenticate` op and
   requests the grant with the **meta user id as subject** and the
   requesting client-site user recorded in `client_user_id`, so the grant
   serves exactly that client user's session and its pending request shows
   on the meta consent page of the subject.
3. **No OAuth impersonation**: this is an internal consent flow, not an
   OAuth/OIDC implementation; external federation is deferred.
4. **Broker is the boundary**: other sites never touch the meta store
   directly; all cross-site access flows through `MetaAccountService` and is
   recorded in the broker diagnostics ledger.
5. **Template ownership**: shared page shells and CSS come from
   `WebsiteTemplates` (`meta-login` for sign-in, `glassmorphism` for signed-in
   content). Handlers provide escaped values and page-specific content, never
   inline `<style>` blocks or theme helpers.
6. **The inbox is subject-scoped and events are inert**: every inbox event
   names its meta subject; the page and `inbox/read-all` serve the session
   identity only, and a second identity sees an empty inbox. Events carry
   ids and states only — never scopes, grant ids, or credentials. The
   broker `notify` op (design 013, task 014) is how client sites deliver
   events; it validates the type allowlist, the subject, and the message
   length, and is recorded in the diagnostics ledger. Retention keeps the
   latest 200 events per subject. Consent transitions (requested, granted,
   denied, revoked) always write events locally.
