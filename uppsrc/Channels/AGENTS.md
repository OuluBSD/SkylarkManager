# Channels Package Guide

`uppsrc/Channels` is the read-only frontend over the AgentIPC message board
(the "Channels" webapp). It composes as an engine site and shows the node's
channels and their recent messages. Design: the planner handoff task 011
(`~/skylark/tasks/design-011-channels-webapp.md`); decision: reads run
through the argv-only `agentipc` client, the same pattern GitHubForge uses
for `gh`.

## File Map

| File | Role |
|------|------|
| `AgentIpcClient.h/.cpp` | `AgentIpcConfig` (node address, server, identity, records path, in-memory password), the argv-only client runner (`LocalProcess::Start2`, bounded read, timeout kill, child env block), message parsing, and the plaintext-metadata overview over the node's `mesh/records.jsonl` |
| `ChannelsSite.h/.cpp` | `InstallChannelsSite`: `GET /` and `GET /overview` (channel overview), `GET /channels/{server}/{chan}` (recent messages), a 20 s TTL cache with `?fresh=1` bypass, read-only manifest commands |

## Invariants

1. **Read-only**: the site never posts to the node (`say` does not exist
   here). A future write surface is a separate decision.
2. **Argv only, never shell**: channel names are validated
   (`AgentIpcValidChannel`) and passed as single argv entries; no shell
   metacharacters, no interpolation.
3. **The password never appears in argv or logs**: it is held in memory and
   reaches the child as an environment variable (`AGENTIPC_CHANNELS_PW`)
   through a per-call env block. The client is invoked with
   `--password-env <name>`.
4. **Encrypted bodies are only ever read by the client**: the node's local
   record file holds ciphertext bodies on an encrypted server; the overview
   uses only the plaintext metadata (`server`, `chan`, `ts`, `user`) and
   never reads or renders the body field.
5. **Deny by default**: every page needs a session. A failing client call
   answers 503 with a clear page, never a partial render. Cache-then-fetch:
   the cache serves up to 20 s old data; `?fresh=1` bypasses it.
6. **Credentials missing is not a crash**: without
   `MANAGERSKYLARK_AGENTIPC_PASSWORD` the site still composes; channel pages
   report the missing credentials state. The overview needs no credentials
   (record metadata only).

## Composition

`EngineComposition.cpp` composes the site on `--channels-port` (default
18215). The bridge is configured with `--agentipc-direct`, `--agentipc-server`,
`--agentipc-user`, `--agentipc-python`, `--agentipc-client`,
`--agentipc-home` (defaults: `127.0.0.1:7440`, `skylark`, `channels`, the
venv python, the OuluLinux client, `~/.agentipc-skylark`). The node identity
itself (an AgentIPC account on the node, e.g. `channels`) is created by the
operator with `agentipc account add`, not by this package; the server reads
its password from `MANAGERSKYLARK_AGENTIPC_PASSWORD` at startup.

Accounts of the site (viewers) are provisioned with
`MANAGERSKYLARK_CHANNELS_PASSWORD=... ManagerSkylark --set-channels-user NAME`.

## Testing

The fence (`upptst/WebEngineTests`) drives the site through an argv shim: a
shell script that dumps its argv (asserting the password is only there as
the env variable name), counts its calls (cache TTL and `?fresh=1`), and can
be made to fail (the 503 path). The composed smoke runs the site against the
shim with the same assertions; the `channels-view` AgentLab scenario reads
the real node through the real client.
