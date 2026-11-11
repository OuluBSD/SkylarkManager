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
| `MetaAccountSite.h/.cpp` | `InstallMetaAccountSite` (routes on an `EngineSite`: login, TOTP enroll/confirm/disable, consent list/decide), `MetaAccountService` (broker ops: authenticate, request-consent, check-grant, revoke-grant, ping), `RegisterMetaAccountService` |

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
5. **Minimal HTML**: until the Witz work package, pages are small functional
   HTML emitted directly by handlers.
