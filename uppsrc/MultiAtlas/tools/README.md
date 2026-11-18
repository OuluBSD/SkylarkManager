# MultiAtlas gate bridge

`record-gate-pass.sh` sends a gate proof only after the API's replay, configured
worktree branch/head, committed marker, and committed child event all agree.
It needs an authenticated MultiAtlas session in `MA_COOKIE` and the matching
`MA_CSRF`, plus a configured meta-family version in `MA_META_VID`. The script
uses a mode-0600 temporary header file and retries bounded API requests.

The worker bridge adapter is intended to run once per trusted child track. Set
`MA_ENSEMBLE`, `MA_TRACK_ID`, `MA_TRACK_REPO`, `MA_TRACK_BRANCH`,
`MA_TRACK_FAMILY`, and `MA_TRACK_STORE` in its service environment, along with
`MA_GATE_TESTER` (an executable invoked with argv `<repo> <gate>`), API origin,
session cookie, CSRF token, meta version, and queue directory. Task files are
data only and contain exactly:

```text
ensemble=<registered ensemble>
track_id=<the adapter's configured track>
gate_version=<configured version id or major.minor.patch code>
git_commit_hash=<full current source commit>
```

The adapter runs the configured tester, writes the child gate event through
the scoped track API, writes `.multiatlas/gates/<gate>.json`, commits only
those two paths, then calls `record-gate-pass.sh`. A delivery retried after
that commit checks the matching committed event and marker and resends the
meta notification without creating another child commit. Failed tasks remain
queued. Run `multiatlas-gate-adapter.sh --test` for the parser-only checks;
`--once` processes a queue selected with `MA_GATE_QUEUE_DIR`.

Install `git-hooks/post-commit-gate-check.sh` at the hook path returned by
`git -C <child-repo> rev-parse --git-path hooks/post-commit` to dispatch commits
whose message contains `gate-<version>-pass`. Git has no post-tag hook, so
after creating a matching tag run that same hook path once to dispatch the
tagged `HEAD` from the child repository root:

```sh
hook=$(git rev-parse --git-path hooks/post-commit)
"$hook"
```

The hook ignores ordinary commits and sets `MA_TRACK_REPO` from the actual
worktree. Set `MA_ENSEMBLE`, `MA_TRACK_ID`, and optionally
`MA_RECORD_GATE_SCRIPT`/`MA_SKYLARKMANAGER_ROOT` in the hook environment.
