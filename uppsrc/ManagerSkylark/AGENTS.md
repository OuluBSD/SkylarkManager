# ManagerSkylark Package Guide

`reference/ManagerSkylark` is a high-performance web application and REST API server implemented with U++ `Skylark`, following the `../Manager/v2` multi-repository architecture and integrating with `uppsrc/SweAgent` for autonomous development agent workflows.

## Features

- **Multi-Repository Architecture**: Loads goals, phases, tasks, state vectors, issues, and releases adhering to `../Manager/v2` schemas (`v2/goals`, `v2/issues`, `v2/releases`, `v2/trackers`).
- **Local Manager Integration**: Seamlessly inspects and modifies `./mgr/projects/` (`uppsrc/Manager` store) for local developer task management.
- **REST & JSON APIs**: Endpoints for programmatic access, agent task dispatch (`/api/tasks/next`, `/api/tasks/claim`, `/api/tasks/done`), and model querying.
- **SweAgent Web Runner & Inspector**: Web-based execution and trajectory viewing connecting directly to `uppsrc/SweAgent` OpenRouter runtime.
- **Headless Dual-Purpose & Automated Tests**: Supports both standalone server mode and automated test mode (`--test`) performing internal HTTP validation.

## Command Line Usage

```bash
# Start web server on default port (8080)
./bin/CLANG/ManagerSkylark

# Specify port
./bin/CLANG/ManagerSkylark --port 8081

# Run automated HTTP test suite
./bin/CLANG/ManagerSkylark --test
```

## Routes & Endpoints

| Route | Description |
|---|---|
| `GET /` | Dashboard overview: repositories, goals, releases, and local tasks |
| `GET /projects` | List of tracked projects with progress summaries |
| `GET /project/{slug}` | Project details view with phase checklists and tasks |
| `GET /goals` | Manager v2 goals list across repositories |
| `GET /goal/{repo}/{slug}` | Goal details: decomposition tree, gate dependencies, state vector |
| `GET /issues` | Manager v2 issues view |
| `GET /releases` | Manager v2 cross-domain releases view |
| `GET /sweagent` | SweAgent web runner and trajectory browser |
| `GET /api/health` | Service health status |
| `GET /api/projects` | JSON listing of projects and progress |
| `GET /api/tasks/next/{slug}` | Next open task in project |
| `POST /api/tasks/claim` | Set task status to `in-progress` |
| `POST /api/tasks/done` | Mark task `done` and record result |
| `GET /api/goals` | JSON list of v2 goals |
| `GET /api/issues` | JSON list of v2 issues |
| `GET /api/releases` | JSON list of v2 releases |
| `POST /api/sweagent/run` | Execute SweAgent task and return trajectory |

## Verification

Build with `bin/build -m CLANG reference/ManagerSkylark` and run `./bin/CLANG/ManagerSkylark --test`.
