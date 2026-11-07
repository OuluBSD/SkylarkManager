# ManagerSkylark Package Guide

`ManagerSkylark` is a U++/Skylark operator portal. It renders generic workspace records, local task-store projects, versioned goals and topology diagrams. Source roots are selected by private runtime configuration; there is no implicit home/workspace scan fallback. Do not add project-instance data, absolute local paths, credentials, or private repository identities to this public package.

## Current Safety State

- The normal listener defaults to loopback and protected application/WebChat routes require an authenticated current operating-system identity. Unsupported platform authentication fails closed.
- Host file browsing, remote host commands, external GitHub publication, SweAgent dispatch and PlantUML process rendering are disabled until their scoped security/policy work is complete.
- Chat is a local demo, not a model connection. Paper evaluation and schema inference endpoints explicitly return unavailable (HTTP 501) until implemented.
- `DataStore` reads only an explicit private root configuration, validates bounded inputs, skips symlinks and reports source errors without returning the configured absolute root.
- Self-tests use an isolated temporary workspace, explicit store roots and a loopback test listener. Do not weaken isolation or run the suite against ambient user stores.

## Build and Verification

```bash
uppbuilder ManagerSkylark -m CLANG -v
./bin/ManagerSkylark --test
```

Use the native `CLANG` build method in this environment; the default `EMCC_WASM` method requires Emscripten. Self-tests are synthetic and must remain independent of private data, external model providers and publishing services.

## Main Routes

| Route | Description |
|---|---|
| `GET /` | Data-driven workspace summary; no host filesystem scan |
| `GET /data` | Configured structured-data explorer |
| `GET /data/ideas` | Task proposals and research candidates with comparison tiers |
| `GET /data/papers` | Paper/analysis records grouped by declared research stage |
| `GET /data/diagrams` | Registered topology and diagram-source index |
| `GET /data/diagram/{scope}` | Generated topology SVG with pan/zoom controls |
| `GET /data/k/{kind}` | Generic record list for a kind |
| `GET /data/doc/{id...}` | Generic, escaped document detail view |
| `GET /projects` | Local task-store workspaces; implementation state is not human acceptance |
| `GET /goals` | Structured goals from the configured record root |
| `GET /api/health` | Minimal public health response |
| `GET /api/data/spec` | Authorized data catalog metadata without an absolute root path |
| `GET /api/tasks/ideas` | DataStore-backed task idea records |
| `GET /api/papers/mining` | DataStore-backed paper and analysis records |
| `GET /api/workspace/discover` | Relative paths for indexed records under the configured root; caller path overrides are rejected |
| `POST /api/tasks/evaluate-paper` | Explicitly unavailable until evidence-backed evaluation is implemented |
| `POST /api/data/schema/generate` | Explicitly unavailable until versioned schema inference is implemented |
| `POST /api/sweagent/run` | External dispatch disabled pending provider policy |

## Engineering Requirements

- Read root/package `AGENTS.md`, the applicable `ai/skills/` and `ai/rules/` before changing architecture or routes.
- Keep domain behavior in shared services/models and reuse it across HTML, API, and headless tests; do not create project-specific handwritten page implementations.
- Treat every imported record as untrusted. Escape HTML/attributes, validate same-origin links, check exact U++ `ValueMap` versus `ValueArray` types, and do not render external SVG or markdown as active content without a reviewed sanitizer/isolation boundary.
- Store canonical structured records in explicitly configured private JSON roots; any SQLite database is a rebuildable index, never a second writable source of truth.
- Preserve worktree changes from other contributors. Do not blanket-stage, revert, commit, publish, or import private records without explicit current authorization.
- If a dependency-repository edit is required, report the exact separate scope before modifying that repository.
