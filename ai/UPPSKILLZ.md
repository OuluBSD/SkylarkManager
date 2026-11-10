# Generic U++ and Skylark material: UppSkillz

Generic procedures for U++ and Skylark live in UppSkillz (`skills/<id>/SKILL.md`, with
provenance in `HISTORY.md`). The repo is staged at `~/Dev/UppSkillz` and is not published yet.

Where an UppSkillz skill covers a procedure, it is authoritative for the generic part:

- `upp-containers-ownership`: Vector move-only rules and the clone copy idiom.
- `upp-value-json`: Value and JSON pitfalls (`AsJSON`, `GetValue(i)`).
- `upp-sync-primitives`: `StaticMutex` member trap; `Mutex` members.
- `upp-build-packaging`: uppbuilder assembly builds, `flagMAIN`, `INITBLOCK_`.
- `skylark-witz-templates`: witz escaping, `$raw`, directive whitespace.
- `skylark-engine-fence`: Origin and CSRF checks; POST_RAW caveat.
- `socket-and-console-hygiene`: `EINPROGRESS`, accepted-socket timeouts, unbuffered test output.

Repo policy stays in this `ai/` directory. The local rule and skill files here are not
replaced by UppSkillz.
