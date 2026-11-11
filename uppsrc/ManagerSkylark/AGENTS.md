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

## Composed engine sites

The real server (not `--test` or `--demo-site`) composes five engine sites
behind `WebEngineHost`, each on its own listener port, plus the Company module:

| Site | Default port | Data root | Flag |
|---|---|---|---|
| `privatehub` | 18211 | `<site-dir>/engine/privatehub` | `--privatehub-port`, `--privatehub-workspace` (default `~/Dev`) |
| `science` | 18212 | `<site-dir>/engine/science`, corpus `~/Dev/AnthologyPrivate/site/data` | `--science-port`, `--science-corpus` |
| `taskfoundry` | 18213 | `<site-dir>/engine/taskfoundry` | `--taskfoundry-port` |
| `meta` | 18214 | `<site-dir>/engine/meta` | `--meta-port` |
| `channels` | 18215 | `<site-dir>/engine/channels` | `--channels-port`, `--agentipc-*` (see `uppsrc/Channels/AGENTS.md`) |
| `financials` | 18216 | `<site-dir>/engine/financials` | `--financials-port`, `--set-financials-user`, `--financials-role` (see `uppsrc/Financials/AGENTS.md`) |
| Company module (no site) | none | `<site-dir>/engine/company` | `--company-root`, `--company-repos` (comma list; default none, so every proposal is refused) |

**Host-compose direction only.** ManagerSkylark composes the Company module from
`uppsrc/CompanyComposed`, which opens its own store under `--company-root`. The
host reaches Company only through the broker surface (`create-proposal-draft`,
`get-proposal-status`) that `CompanyBrokerSite` registers as `company`. It never
touches the Company store, config or records. CompanySkylark never depends on
this package or on ManagerSkylark (CompanySkylark AGENTS.md rule 8).

Broker services registered in the composed process: `science` (ScienceService),
`privatehub` (PrivateHub read and write operations, including `repo.info`),
`company` (the Company module: proposal draft/status and the read-only
financial surface — ledger-summary, ledger-recent, ledger-balances,
contracts-list), and `meta` (MetaAccountService: authenticate,
request-consent, check-grant, revoke-grant, notify). TaskFoundry and the
Financials site call them; the
meta site owns the shared grant store under `<site-dir>/engine/grants`.

Build note: ManagerSkylark links the Company packages, so its build needs the
sibling source path: `uppbuilder -m CLANG -a ../CompanySkylark/uppsrc ManagerSkylark`.

Accounts are per site. Provision them with
`MANAGERSKYLARK_PRIVATEHUB_PASSWORD=... ManagerSkylark --set-privatehub-user NAME`,
`MANAGERSKYLARK_SCIENCE_PASSWORD=... ManagerSkylark --set-science-user NAME`
(science-author role), `MANAGERSKYLARK_TASKFOUNDRY_PASSWORD=... ManagerSkylark --set-taskfoundry-user NAME`,
`MANAGERSKYLARK_META_PASSWORD=... ManagerSkylark --set-meta-user NAME`,
or `MANAGERSKYLARK_CHANNELS_PASSWORD=... ManagerSkylark --set-channels-user NAME`
(or a hidden prompt); each command exits after writing the account. The
Channels site additionally reads the AgentIPC node identity's password from
`MANAGERSKYLARK_AGENTIPC_PASSWORD` at startup (see
`uppsrc/Channels/AGENTS.md`). Engine
listeners serve only their own site: a stock route claiming a path is answered
404, except the home route (see `uppsrc/WebEngine/AGENTS.md`).

| Route | Site | Description |
|---|---|---|
| `GET /` | privatehub | Workspace overview (login required) |
| `GET /session/login` | privatehub, science | Sign-in form |
| `POST /session/login` | privatehub, science | Sign in (JSON for API callers, redirect for forms) |
| `POST /session/logout` | privatehub, science | Sign out |
| `GET /privatehub/{org}/{repo}` | privatehub | Project overview |
| `GET /privatehub/{org}/{repo}/issues` | privatehub | Issue list and create form |
| `POST /privatehub/{org}/{repo}/issues/create` | privatehub | Create issue (CSRF required) |
| `GET /privatehub/{org}/{repo}/issues/{id}` | privatehub | Issue detail and comments |
| `POST /privatehub/{org}/{repo}/issues/{id}/comment` | privatehub | Add comment (CSRF required) |
| `GET /privatehub/{org}/{repo}/wiki[/{slug}]` | privatehub | Wiki list and page |
| `GET /privatehub/{org}/{repo}/discussions[/{slug}]` | privatehub | Discussion topics and posts |
| `POST /privatehub/{org}/{repo}/discussions/{slug}/post` | privatehub | Post to a topic (CSRF required) |
| `GET /privatehub/{org}/{repo}/files[/{name}]` | privatehub | Release files; the source URL is shown as text, never linked |
| `GET /` | science | Science home |
| `GET /papers`, `GET /papers/{id}` | science | Paper list and detail |
| `GET /papers/{id}/author` | science | Authoring form (science-author role or caller-bound grant) |
| `GET /api/papers`, `GET /api/papers/{id}` | science | Paper JSON |
| `GET /api/papers/{id}/article` | science | Structured article JSON |
| `POST /api/papers/{id}/article` | science | Save article; `expected_paper_revision` gives 409 on drift; sidecar write is atomic |
| `GET /` | taskfoundry | Board: candidates and proposals (login required) |
| `POST /candidates/create` | taskfoundry | Candidate from a Science paper (CSRF required) |
| `GET /candidates/{id}`, `POST /candidates/{id}/propose` | taskfoundry | Candidate detail; start a proposal |
| `GET /proposals/{id}` | taskfoundry | Proposal editor: fields, evidence, validation, submit |
| `POST /proposals/{id}/edit`, `/evidence`, `/validate`, `/submit` | taskfoundry | Mutations (CSRF required); submit returns the same submission on repeat |
| `GET /submissions/{id}` | taskfoundry | Submission snapshot and Company status |
| `GET /grants`, `POST /grants/request` | taskfoundry | Caller's TaskFoundry grants; request one by linking a meta identity (meta credentials + CSRF) |
| `GET /` | meta | Meta-account home (login required) |
| `GET/POST /session/login`, `POST /session/logout` | meta | Sign in/out |
| `POST /totp/begin`, `/totp/confirm`, `/totp/disable` | meta | Enroll or disable the TOTP second factor |
| `GET /consent`, `POST /consent/decide` | meta | Consent grants of the signed-in identity; decide (approve/deny/revoke) as the subject |
| `GET /inbox`, `POST /inbox/read-all` | meta | The identity inbox (consent and client-site lifecycle events); read-only for the subject, CSRF on the mutation |
| `GET /financials`, `GET /ledger`, `GET /contracts` | financials | The Company money flows read-only through the broker; ledger rows and contracts need the finance role |
| `GET /console` | composed sites | The per-site command console: the site's manifest commands as links and CSRF-bearing forms; the routes stay the authority (see `uppsrc/WebEngine/AGENTS.md`) |
| `GET /overview` | channels | The AgentIPC node's channels and their last activity (read-only) |
| `GET /channels/{server}/{chan}` | channels | A channel's recent messages through the argv-only client (read-only) |

TaskFoundry access needs an approved grant bound to the signed-in user. The
user links their meta identity on the TaskFoundry grants page (meta username,
password, TOTP code when enrolled) and the pending request is decided by its
subject on the MetaAccount site consent page (approve/deny, revoke at any
time; the next TaskFoundry request sees the change). The operator CLI stays as
a fallback: `ManagerSkylark --site-dir DIR --approve-taskfoundry-grant wag_ID`
or `--revoke-taskfoundry-grant wag_ID`; both run without composing the engine.

Authoring is caller-bound: a `x-science-grant` id authorizes a write only from
the authenticated session of the grant's subject. The same id from another
session, or with no session, gets 403.

The stock routes are unchanged on the main port, and still require login.

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
