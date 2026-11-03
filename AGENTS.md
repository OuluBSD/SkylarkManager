# AGENTS - OuluBSD/SkylarkManager Guide

**Scope**: Applies to the root of `OuluBSD/SkylarkManager` and all packages within.

## Repository Overview

`OuluBSD/SkylarkManager` is the web-based project management, system inspection, and autonomous development agent portal for the U++ ecosystem. It materializes the concept of **"Manager as a Skylark program"**: a high-performance C++ web application serving interactive dashboards, real-time agent pair-programming interfaces, host workstation telemetry, and RESTful task dispatch APIs.

### Packages & Components

| Directory | Type | Description |
|---|---|---|
| `uppsrc/ManagerSkylark` | Application / Server | Core web app and REST API server following `Manager/v2` multi-repository schemas and local `./mgr/projects` tracking. |
| `uppsrc/WebFramework` | Library | Reusable web framework: KDE Plasma-inspired themes, PageBuilder, mega-menu navigation, tree views, SVG icons, and audio feedback. |
| `uppsrc/WebChat` | Module / Subsystem | Real-time SSE streaming web chat view and session manager connecting to `SweAgent` LLM backends. |
| `examples/SkylarkGuiExample` | Example | Bi-directional desktop integration combining U++ `CtrlLib` GUI with an embedded `Skylark` HTTP server. |
| `examples/WebShowcase` | Example | Standalone reference showcase application for `WebFramework` components, themes, and navigation widgets. |

---

## AI Knowledge Framework (`ai/`)

Binding rules and procedural skills specifically relevant to `SkylarkManager` are located under `ai/`:

| Folder | Contents | Key Files |
|---|---|---|
| `ai/skills/` | How-to procedural guides | `skylark-web-ui.md` (Skylark handlers, Witz templates, forms, CSRF)<br>`manager-json-format.md` (Manager JSON document schema & ledgers)<br>`manager-planner-worker-loop.md` (Task claim/done planner-worker protocol)<br>`upp-value.md` (Value, ValueMap, ValueArray serialization)<br>`upp-format-usage.md` (Format string syntax)<br>`memory-safe-containers.md` (Vector, Array, Index, VectorMap) |
| `ai/rules/` | Binding coding rules | `headless-gui-dual-purpose.md` (Headless CLI/test twin for every web feature)<br>`assertion-and-fix-it-now.md` (Fast crash assertions, no excuse-code)<br>`logging-rules.md` (LOG / RLOG / DLOG / LLOG standards)<br>`naming-conventions.md` (Symbol, package, and file naming rules)<br>`movable-and-ownership.md` (Moveable<T> and NTL ownership)<br>`code-readability.md` (Self-documenting code and clear errors)<br>`upp-flags.md` (Positive feature flags, no legacy flagV1) |

Incoming agents must consult `ai/skills/skylark-web-ui.md` and `ai/skills/manager-planner-worker-loop.md` before making architectural or route additions.

---

## Multi-Assembly Dependency Graph

`SkylarkManager` builds independently using `UppBuilder` (`bin/uppbuilder`), discovering sibling assemblies automatically:

```
                     +-------------------+
                     |   UppBootstrap    | (bootstrap umk)
                     +---------+---------+
                               |
                               v
                     +-------------------+
                     |    UppBuilder     | (assembly & patch materializer)
                     +---------+---------+
                               |
               +---------------+---------------+
               |                               |
               v                               v
    +--------------------+           +-------------------+
    |     BazaarCore     |           | SuperIntelligence |
    | (Core, Core/Ext,   |           |    (SweAgent)     |
    |  Skylark, Manager) |           +---------+---------+
    +----------+---------+                     |
               |                               |
               +---------------+---------------+
                               |
                               v
                  +-------------------------+
                  |   OuluBSD/SkylarkManager|
                  |                         |
                  |  - WebFramework         |
                  |  - WebChat (->SweAgent) |
                  |  - ManagerSkylark (app) |
                  |  - SkylarkGuiExample    |
                  |  - WebShowcase          |
                  +-------------------------+
```

- **`BazaarCore`**: Patched standard library (`Core`, `Core/Ext`, `Skylark`, `Manager`, `Sql`, `Ctrl/Core`).
- **`SuperIntelligence`**: Autonomous LLM agent framework (`SweAgent` connecting via `Core/SSL`).
- **`ultimatepp`**: Upstream base packages.

---

## Building and Verification

### Commands

```bash
# Build ManagerSkylark web server binary
uppbuilder ManagerSkylark -v

# Run automated headless verification suite (100% test coverage)
./bin/ManagerSkylark --test

# Run the web server interactively or daemonized (default port 8080)
./bin/ManagerSkylark
./bin/ManagerSkylark --port 8081

# Build examples
uppbuilder examples/SkylarkGuiExample -v
uppbuilder examples/WebShowcase -v
```

### Self-Test Suite Details
The `--test` flag boots an internal server instance on port 18080, runs automated HTTP requests across all primary API and UI endpoints, verifies responses and JSON payloads, and exits cleanly with code 0 on success or 1 on failure. When running in a fresh workspace without `./mgr/projects`, it automatically seeds an isolated test fixture so verification never depends on ambient external data.

---

## Web Routes and REST API Map

### Web Views & UI
| Route | Method | Description |
|---|---|---|
| `/` | `GET` | Main Ecosystem Command Center (repositories, goals, releases, tasks) |
| `/projects` | `GET` | Local project overview and task progress summary |
| `/project/{slug}` | `GET` | Detailed project view with phase checklists and tasks |
| `/goals` | `GET` | Multi-repository `Manager/v2` architecture goals |
| `/goal/{repo}/{slug}` | `GET` | Goal details, state vectors, gate checklist, and decomposition tree |
| `/issues` | `GET` | `Manager/v2` multi-repository issue tracker |
| `/releases` | `GET` | Cross-repository release milestones |
| `/chat` | `GET` | Integrated AI pair-programming chat view |
| `/webchat` | `GET` | Standalone AI chat application |
| `/sweagent` | `GET` | SweAgent web runner and execution trajectory viewer |
| `/host` | `GET` | Local workstation hardware metrics, storage, and home file explorer |
| `/host/files` | `GET` | Authenticated file manager for local files |
| `/host/repos` | `GET` | Local Git repository inspector and commit log |
| `/host/nodes` | `GET` | Compute mesh fleet nodes and status |

### REST & Agent Dispatch APIs
| Route | Method | Payload / Response | Description |
|---|---|---|---|
| `/api/health` | `GET` | JSON status object | Service health status |
| `/api/projects` | `GET` | JSON list of projects | All projects from local store |
| `/api/tasks/next/{slug}` | `GET` | JSON task object | Next open task in project (planner-worker loop) |
| `/api/tasks/claim` | `POST` | `{"project": str, "seq": int}` | Atomically claim task (`status -> in-progress`) |
| `/api/tasks/done` | `POST` | `{"project": str, "seq": int, "result": str}` | Mark task complete with verification evidence |
| `/api/chat/sessions` | `GET` | JSON session list | Active chat session metadata |
| `/api/chat/message` | `POST` | `{"session_id": str, "content": str}` | Dispatch message to SweAgent LLM backend |
| `/api/chat/stream/{id}` | `GET` | Server-Sent Events (`text/event-stream`) | Real-time token streaming response |
| `/api/framework/spec` | `GET` | JSON semantic spec | Navigation hierarchy and MegaMenu configuration |

---

## Cross-Platform CI Matrix

Continuous integration is maintained via GitHub Actions (`.github/workflows/ci.yml`):
- **Ubuntu Linux (Clang++)**
- **Ubuntu Linux (G++)**
- **macOS Apple Silicon (Clang++)**
- **Windows x64 (MSVC 2022)**
