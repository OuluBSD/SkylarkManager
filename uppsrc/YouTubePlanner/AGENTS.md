# YouTubePlanner Package Guide

`uppsrc/YouTubePlanner` is a composed webapp that shows the six planning levels of a YouTube video
(Screenwrite, Screenplay, Script, Mockup Image, High-Level Objective, Low-Level Objective) and what
belongs at each one.

## Invariants

1. **A stub.** It holds no plans, makes no YouTube or OAuth call, uploads nothing, publishes nothing and
   writes nothing. The self-tests fail if a form, input, textarea, button or select appears in a page body.
2. **Truthful copy.** Every placeholder says what belongs at the level and that nothing is saved. Do not
   describe a saved plan, an edit or a published video.
3. **Anonymous and read-only.** Pages need no account. Sign-in controls come from the shared session chrome only.
4. **Composition.** The site is registered in `EngineComposition` with the `youtubeplanner` id, the
   `YouTubePlanner` title and its own port (`--youtubeplanner-port`, default 18218). The portal lists it from the
   engine's site listing.

## File Map

| File | Role |
|------|------|
| `YouTubePlannerSite.h/.cpp` | Level table, the pure body renderers, the routes (`/`, `/youtubeplanner`, `/level/*`), the command manifest entries and the self-tests |
