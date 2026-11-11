# Financials Package Guide

`uppsrc/Financials` is the read-only manager-portal window over the Company
module's money flows: the contributor compensation ledger (summary and
recent entries) and the client contracts. Design: the planner handoff task
015 (`~/skylark/tasks/design-015-financials.md`).

## File Map

| File | Role |
|------|------|
| `FinancialsSite.h/.cpp` | `InstallFinancialsSite`: `GET /financials` (summary: totals by flow, entry count, newest entry), `GET /ledger` and `GET /contracts` (finance role), read-only manifest commands, and the role→scope mapping for the broker calls |

## Invariants

1. **Read-only**: the ledger is append-only and its writes stay inside the
   Company workflow; this site never mutates Company data.
2. **Broker is the boundary**: every read is a scope-checked broker op on
   the Company module (`ledger-summary`, `ledger-recent`,
   `ledger-balances`, `contracts-list` — see `CompanyWorkflow/
   CompanyBroker.{h,cpp}` in the private CompanySkylark repo), recorded in
   the diagnostics ledger. The site never touches the Company store.
3. **Roles carry the scopes**: the viewer's role maps to the caller scopes
   — `viewer` carries `ledger:read` (aggregates and rows without user ids),
   `finance` additionally carries `admin` (per-user rows, subject filters,
   balances, contracts). The Company module re-checks every scope, so the
   site mapping is a convenience, not the gate.
4. **Typed payloads are rendered by their type**: broker payloads carry
   int/double Values; rendering uses the value's real type (`CellText`) —
   a `(String)` cast of a numeric Value throws in Upp.
5. **Deny by default**: every page needs a session; the ledger and
   contracts routes answer 403 with a clear page for viewers without the
   finance role; a failing module answers 503, never a partial render.

## Composition

`--financials-port` (default 18216); accounts with
`--set-financials-user` + `MANAGERSKYLARK_FINANCIALS_PASSWORD` and
`--financials-role` (default `viewer`; `finance` sees the ledger rows and
the contracts). The Company module itself composes with `--company-root`
and `--company-repos` (a fresh store shows the empty states).

## Testing

The fence drives the site through a canned `company` stand-in service
(`FakeCompanyService` in `upptst/WebEngineTests`) with the same scope
checks as the real module: role negatives, escaped rendering, a direct
broker check that a subject filter without `admin` is refused, and the
503 failure page. The composed smoke runs the site against the real,
freshly seeded Company store (empty states). The `financials-view`
scenario reads the three views live.
