# WebEngineTests Package Guide

Console self-test host for the composable website engine. It starts one
`WebEngineHost` process with three fenced sites (shell on the stock port,
alpha and beta on extra ports) that deliberately register IDENTICAL route
paths and the SAME username, then drives raw HTTP against the listeners and
asserts the fence properties:

- identical paths route to each site's own handlers;
- per-site cookie names, sessions, users and counters are independent;
- a session cookie of one site is rejected by the other (site stamp);
- mutations are fenced by exact-origin and server-side CSRF tokens
  (POST_RAW catch-alls are intentional -- the engine enforces its own fence);
- cross-site work goes through the WebBroker and is recorded in diagnostics;
  calls to unregistered sites fail explicitly;
- broker TCP transport is an explicit stub.

Run with the app's `--test` convention: exit code 0 = all checks passed.
