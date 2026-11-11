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

Run in CI (`agentlab` job in `.github/workflows/ci.yml`, plain AgentIPC node on a CI home):
`privatehub-issues`, `taskfoundry-submit`, `channels-view`, `financials-view`. The CI job sets
`AGENTLAB_AGENTIPC_PYTHON`, `AGENTLAB_AGENTIPC_CLIENT` and `AGENTLAB_AGENTIPC_HOME`; unset, the
runner uses the local defaults. The CI node's channel `SKYLARK-MANAGER` is seeded by the job, and
`MANAGERSKYLARK_AGENTIPC_PASSWORD` is the `channels` account password.

Not yet verified live: the CI job has not run on GitHub. Its node config (authority, allow list
on 127.0.0.1) follows the AgentIPC example config and is unproven until a CI run.

Local status (2026-11-11): `privatehub-issues` 12/12, `taskfoundry-submit` 23/23, `financials-view`
5/5 (S2 run), and `channels-view` 4/4 (S1 run with the documented channels identity password, set via
`MANAGERSKYLARK_AGENTIPC_PASSWORD`). The `channels` account is not recreated.
