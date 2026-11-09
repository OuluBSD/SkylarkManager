# GitHub route capture harness

Captures the GitHub webapp routes from a headless ManagerSkylark and compares
two captures after normalization. Used before and after every change to the
GitHub view code (T02 brief 09).

## Files

| File | Purpose |
|---|---|
| `capture.sh` | Boots the binary on a temp site with fixture repos and the fake gh, logs in, fetches each route in `routes.txt` |
| `make-fixtures.sh` | Builds deterministic git repos (`demo-repo`, `second-repo`): fixed author, dates and content |
| `fake-gh.sh` | Stands in for `gh` with canned JSON for the `SeppoPakonen/demo-repo` shapes the views call (issues, pulls, comments, PR diff, runs, repo list). Other argv exits 2, which views report as unavailable. No network |
| `routes.txt` | The route set (21 routes): tabs, sub-pages, user, raw and an API route |
| `check-claims.sh` | Asserts the claims the security tab must not make, on a capture directory (replaces the in-process check the adapter test cannot run) |
| `make-store.sh` | Seeds the Manager local store (`site/mgr`) with the demo-repo project, 9 tasks in open, in-progress, done and blocked states |
| `normalize.sh` | Normalization applied to every file before comparison |
| `compare.sh` | Compares two capture directories after normalization |

## Regenerate and compare

```sh
cd ~/Dev/SkylarkManager
BIN=bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Mt-Posix-Shared/ManagerSkylark
sh upptst/GitHubCapture/capture.sh $BIN ~/Dev/CompanySkylark/tmp/gh/after 18410
sh upptst/GitHubCapture/compare.sh ~/Dev/CompanySkylark/tmp/gh/baseline ~/Dev/CompanySkylark/tmp/gh/after
```

`capture.sh` picks its own temp directory and deletes it on exit. The port is
the third argument; use a different port for concurrent runs.

Captures are not committed. The baseline is regenerated from the commit before
the change being measured, and `compare.sh` exits 1 on any difference.

## Normalization

Applied to bodies and headers alike (`normalize.sh`):

- `Date:`, `Set-Cookie:` and `Content-Length:` headers are removed. Date and
  Set-Cookie carry the clock and a random session id; Content-Length follows
  the body.
- `usr_<hex>` user ids become `usr_X`.
- `ms-capture.<random>` (the temp directory name, which appears in repo paths)
  becomes `ms-capture.X`.
- `dd/mm/yyyy hh:mm:ss` timestamps become `TS`.
- Relative times (`13 seconds ago`, `2 hours ago`) become `AGO`.

Fixture commit ids are stable because the dates, author and content are fixed.
Anything else that differs is a real change in the rendered output.

## Known limits

- Live data is canned. `fake-gh.sh` covers the read shapes for one repo
  (`SeppoPakonen/demo-repo`). Write routes (create/comment/merge/close) and the
  views that call other gh shapes (projects, security, settings, workflow
  editor) report unavailable in the capture.
- The route set covers the tabs and entry points but not every form submission
  or POST route.

## Baseline history

- Pre-brief-09 baseline, unavailable-only fake gh: `tmp/gh/baseline-unavailable`.
- Baseline after the fake gh gained canned data (commit of this README): the
  current `tmp/gh/baseline`. Its commits-tab routes match the pre-change output,
  shown by the identical capture at commit 1b0f1e3.
