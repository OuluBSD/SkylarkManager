## Headless / GUI Dual-Purpose Rule

- Every new GUI-facing feature should have a headless path that exercises the same behavior one-to-one for agents, CI, and scripted verification.
- Do not duplicate meaningful logic between headless and GUI implementations. Put shared state transitions, layout decisions, command handling, serialization, and diagnostics into common helper classes or free functions.
- GUI classes should own presentation and input wiring, not the authoritative model. Prefer headless model/controller classes that GUI controls observe, edit, and render.
- When an existing `Upp::Ctrl` class owns behavior that headless execution needs, refactor toward a shared non-`Ctrl` base or service object. The concrete GUI class may inherit both the shared class/interface and the relevant `Upp::Ctrl` class when that is the cleanest bridge.
- Virtual interfaces and virtual inheritance are acceptable when they remove real duplication between GUI and headless paths. Keep the interface narrow and make invariants explicit with assertions.
- Add CLI/stdout diagnostics for new automated behavior. GUI-only verification is not sufficient when a headless equivalent can be built.

Related: `gui-automation.md`, `human-user-emulation.md`.
