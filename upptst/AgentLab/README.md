# AgentLab

Multi-agent rig for the Skylark sites. Scenarios are data; agents reach sites only through
`skylarkctl` (the SiteCli binary), and the oversight client checks the milestones they post on
AgentIPC.

## Files

- `agentlab.py`: scenario runner. Provisions accounts in a temporary data root, starts the
  composed ManagerSkylark on two ports, replays each step through SiteCli with one cookie jar
  per agent, and asserts rc and output. Prints `ANNOUNCE <agent>: <body>` for each milestone
  that passes; `--announce FILE` also appends those lines for the oversight client.
- `oversight.py`: oversight client. Reads the scenario channel with `agentipc wait` (or a
  `--replay` file) and matches each expected {sender, body} exactly, with the
  `@scenario-oversight` mention. Exit 0 when all arrived, 1 when one is missing.
- `scenarios/*.json`: one scenario each. Fields: `agents` (user, role, instructions),
  `provision`, and `steps`. A step is a `login` or a `run` (SiteCli words) with `expect`
  (`rc`, `contains`, `not_contains`), an optional `capture` (regex group reused as `{name}` in
  later steps), and an optional `announce` body posted only when the step passes.

## Run

    python3 upptst/AgentLab/agentlab.py \
      --binary bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Mt-Posix-Shared/ManagerSkylark \
      --site-cli bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/SiteCli \
      --scenario upptst/AgentLab/scenarios/privatehub-issues.json

## Status

Passing (run by default): `privatehub-issues` (5/5), `science-authoring` (5/5),
`taskfoundry-submit` (16/16, with a grant the operator approves through the ManagerSkylark CLI).

Blocked, parked in `scenarios/blocked/`: `github-pr-flow`. The stock app signs in with a host
form or a bearer token, and SiteCli has no bearer support and no stock `session/login` route, so
the scenario cannot log in through SiteCli. The role negative (contributor cannot merge) also needs
a second account role on the stock site, which it does not have. Direct push to the project repo
is not a SiteCli operation and is not asserted.

Not yet: announcers with their own AgentIPC identities (the runner prints and optionally
writes `ANNOUNCE` lines only).
