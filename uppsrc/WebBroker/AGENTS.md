# WebBroker Package Guide

`uppsrc/WebBroker` is the transport-independent communication bus between
fenced websites: versioned request/result contracts, an in-process transport,
a bounded diagnostics ledger, and an explicit TCP stub (real network transport
is deferred).

## File Map

| File | Role |
|------|------|
| `WebBroker.h/.cpp` | `BrokerCall`, `BrokerCallContext`, `BrokerResult`, `IBrokerService`, `WebBroker` (registry + in-process dispatch + diagnostics), `TcpBrokerTransport` (explicit not-implemented stub) |

## Invariants

1. **Value boundaries only**: call payloads and results are `ValueMap`s. A
   service never receives a pointer into the caller's domain objects or
   stores; the broker copies values across the boundary.
2. **Every call is observable**: the diagnostics ledger records time,
   correlation id, from/to sites, op, outcome and duration, with a bounded
   ring buffer and JSON export.
3. **Broker routes, services decide**: the broker records identity and
   transport facts; domain authorization (scopes, grants) belongs to the
   target service.
4. **Unsupported transport fails explicitly**: remote calls return a clear
   error rather than silently pretending to work.
