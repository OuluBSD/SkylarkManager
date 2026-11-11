# T06 — WP9: Verification/CI leftovers

Small, mostly independent items. Do them opportunistically.

## Items

1. **CompanySkylark in CI**: add a CompanySkylark job to
   `.github/workflows/ci.yml` (build with `-a ../SkylarkManager/uppsrc`,
   run `--test`; needs the SkylarkManager checkout the jobs already do —
   order the steps so SkylarkManager is checked out first). Mirrors the
   fence-test steps; MSVC compile coverage too.
2. **Portable fence-test fixtures** (unblocks running the fence suite on
   Windows CI): generate the fake `gh` as a `.cmd` sibling selected at
   runtime (`#ifdef PLATFORM_WIN32` → `gh.cmd`), and make the PrivateHub
   fixture's git invocations portable (`cd /d` on cmd or use
   `git -C <path>` argv form — preferred: replace `system("cd ... && git ...")`
   with LocalProcess argv-only `git -C` calls in the test itself).
   Then add the run step to the Windows job (remove the compile-only note).
3. **Route retention audit**: after T01, run one non-Blitz static build
   (`-bs` / BLITZ off) of ManagerSkylark and verify all engine catch-alls and
   SKYLARK routes are reachable (linker would drop registration-only TUs).
   Document the verified command in the package AGENTS.
4. **Fence-suite growth policy**: every new engine feature must add fence
   checks; keep the suite under ~5 s runtime (it gates every commit).
5. **GCC + CLANG + MSVC warning sweep** for the new packages (treat new
   warnings as defects; CI output is the record).

## Acceptance

- CI runs: ManagerSkylark build+test, CompanySkylark build+test,
  WebEngineTests build on all platforms + run on POSIX (and Windows after
  item 2).

## CI now runs (task 023)

- `build-posix`: ManagerSkylark, GuiExample, WebShowcase, WebEngineTests, the fence, and now
  `SiteCli` (CLANG, GCC, macOS) with a binary check.
- `agentlab` (ubuntu, clang++): builds ManagerSkylark and SiteCli, starts a plain AgentIPC node on a
  CI home (port 7440), runs four AgentLab scenarios (`privatehub-issues`, `taskfoundry-submit`,
  `channels-view`, `financials-view`), and requires a PASS line for each.
- Not in CI: `github-pr-flow` (parked), Windows SiteCli (optional, not added).
- Unproven until a GitHub run: the node config and the AgentIPC `server create` order.

