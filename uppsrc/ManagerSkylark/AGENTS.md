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

## GitHub views and witz templates

The GitHub repo views render from witz templates in `templates/` (T02 brief 09
and 09B). Interim state:

- Templates render through the app's default template root, which is the
  executable's directory. `deploy-templates.sh <bin_dir>` copies `templates/*.witz`
  there. Run it before a binary renders GitHub pages.
- `GitHubTemplates.cpp` is the string bridge. `RenderGitHubTemplate` returns the
  rendered text with `\n` line ends (witz produces `\r\n`), and throws if the
  template does not compile. `GitHubJsEscape` escapes values for single-quoted
  JavaScript literals inside `<script>` blocks.
- Witz escapes values by default. Values the old literal code wrote raw are now
  escaped, which is identical for the captured fixtures.
- Documented raw slots (`$raw(...)`). Only these, never user data unescaped:
  - icons from `VividIcon::Get`, pre-rendered in the data model (static set);
  - JavaScript literals from `GitHubJsEscape` inside `<script>` blocks;
  - error text from `GitHubPlatformService::EscapeHtml`, which keeps newlines
    the way the old code did.
- Template authoring rules found while converting (witz 1.x in this tree):
  - a directive that stands alone on its line emits its line terminator. Join
    `$endif`, `$else`, `$endfor` to the next content line, or end the last
    content line with `$endif`;
  - a `$variable` directly before `$else`, `$endif`, `$` or `.` does not
    evaluate as expected. Put text or a newline after the variable, or use
    sibling `$for` loops, as the pulls body placeholder does;
  - a template ending in a bare `$endif` with no newline adds a blank line;
  - a value containing a newline is escaped as `&#10;`, which changes a
    `<textarea>` or `<pre>` body. Pass `EscapeHtml(value)` through `$raw`, as the
    workflow editor's YAML body and the actions error row do;
  - `&` in literal template text stays `&`; `&` inside a value becomes `&amp;`.
    Put a link's `&run=` separator in the template text, not in a value;
  - `$endfor$if(...)` on one line chains correctly, and a loop body can hold a
    `$if` for a per-row flag (`has_preview`, `has_stars`).
- The site-scoped template root (engine composition) comes with T04. Until then
  these templates are not served by a WebEngine site.
- Converted (T02 complete): commits, issues, pulls, code, projects, security,
  settings, fallback, actions (dispatch and runs), agents (dashboard, run
  inspector, diff), user profile (sub-nav, overview, repositories, tabs,
  sidebar), actions catalog, workflow editor, TPP doc site and the operator
  warning bar. `GitHubActions.cpp` has no HTML and needed no change.
- Stays server-side on purpose: QTF-to-HTML and TPP text extraction. The
  renderers pass their output in as one raw value.
- Former gaps, fixed in brief 10 step 1:
  - the workflow editor script takes `owner` and `repo` through `GitHubJsEscape`;
  - the marketplace Insert button carries its snippet in an escaped `data-snippet`
    attribute and reads it with `this.dataset`, so quotes are safe;
  - the TPP preview reads topics from `GetDevDir()/ai-upp` (honors `DEV_DIR`), and
    the capture harness writes two fixture topics there.
- Capture check: `upptst/GitHubCapture/` (see its README) before and after each
  change. Tests that call a view in process need the app's template root and
  cannot render templates; route-level checks belong in the capture harness.

## Verification

Build with `bin/build -m CLANG reference/ManagerSkylark` and run `./bin/CLANG/ManagerSkylark --test`.
