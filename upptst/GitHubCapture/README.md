# GitHub route capture harness

Captures the GitHub webapp routes from a headless ManagerSkylark and compares
two captures after normalization. Used before and after every change to the
GitHub view code (T02 brief 09).

## Files

| File | Purpose |
|---|---|
| `capture.sh` | Boots the binary on a temp site with fixture repos and the fake gh, logs in, fetches each route in `routes.txt` |
| `make-fixtures.sh` | Builds deterministic git repos (`demo-repo`, `second-repo`): fixed author, dates and content |
| `fake-gh.sh` | Stands in for `gh`; always exits 1, so live views take their "unavailable" path. No network |
| `routes.txt` | The route set (21 routes): tabs, sub-pages, user, raw and an API route |
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

- Live GitHub data is not exercised. `fake-gh.sh` makes every live view report
  unavailable, so the issues, pulls, actions and workflow views are captured in
  their unavailable state, not with live content.
- The route set covers the tabs and entry points but not every form submission
  or POST route.
