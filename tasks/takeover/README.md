# Takeover handoff: work packages for the next agent

This directory hands the remaining `OuluBSD/SkylarkManager` takeover plan
(`/home/sblo/.local/share/kilo/plans/1794078096401-skylark-webapp-engine-plan.md`)
to the next agent as self-contained task files.

## Working arrangement

The planner (S1) issues work orders; the worker (S2) executes them and
reports in `WORKER-LOG.md` — over the AgentIPC channel SKYLARK-MANAGER
(see PLANNER-HANDOFF-2026-11-11.md for the full protocol, current state,
and the next planner's setup). The AgentLab plan (steps 1-6 in
`~/skylark/plan.md`) is complete and verified; the next planner writes
steps 7+ (MetaAccount composition, member-role gate, Aria phase,
candidate webapps).

## Reading order

| File | Work package | Status |
|------|-------------|--------|
| `T00-context-and-rules.md` | Read first: state, constraints, U++ traps, commands | required |
| `WORKER-BRIEF-01.md` | Current planner order: land staged work + T01 slice 1 | active |
| `T01-module-split-and-host.md` | WP4 ManagerSkylark module split + engine host wiring | next (after your handover) |
| `T02-witz-assets.md` | WP5 Witz/JS/CSS extraction + per-site static (BazaarCore hooks) | after T01 |
| `T03-science-authoring.md` | WP6b Science authoring endpoints + real corpus binding | after T01 |
| `T04-taskfoundry.md` | WP7 TaskFoundry webapp + Company submission via broker | after T01 |
| `T05-company-dual-use.md` | WP8 CompanySkylark standalone+module dual-use | independent, after T04 |
| `T06-verification-ci.md` | WP9 CI hardening leftovers | small, anytime |
| `T07-uppskillz.md` | WP10 UppSkillz curation | independent, anytime |
| `T08-deferred.md` | Explicitly deferred stubs (TCP broker, OAuth, per-site auth federation) | backlog |

## Done so far (do not redo)

Commits on `OuluBSD/SkylarkManager` main (all verified, 116/116 fence checks,
both apps' `--test` green): `7ad72e1` engine foundations (WebEngine,
WebStorage, WebBroker, WebAccounts), `e41e5a1` meta-account app (real TOTP,
recovery codes, consent grants), `a67bfcc` ForgeCore, `c5b2bc1` GitHubForge,
`01adc69` SourceForgeForge, `9e3d6cb` PrivateHubWorkspace, `36b8aa9` Science,
`bf72de3` CI coverage for WebEngineTests.
BazaarCore `f73fb72`: Skylark cookie SameSite support + httponly forwarding.

## Hard rules for every task

1. **Commit with explicit pathspecs** (`git commit -m ... -- <paths>`). A bare
   `git commit` sweeps the whole index — the first agent's staged
   `ManagerSkylark/Assessment*` work was once accidentally committed this way
   (caught and redone).
2. **Keep everything green**: `uppbuilder -m CLANG <target> -v` then
   `./bin/<target> --test` for ManagerSkylark and CompanySkylark (the latter
   with `-a ../SkylarkManager/uppsrc`), plus
   `./bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/WebEngineTests`
   (exit 0, "N/N checks passed") after every change. GCC build
   (`-m GCC`) before pushing; CI also runs MSVC.
3. **Never commit another agent's staged files**, never `git add -A`.
4. **Do not weaken the fences**: site-stamped sessions, exact-origin + CSRF
   on mutations, provenance-labelled results, candidate writes stay disabled.
   The fence suite (`upptst/WebEngineTests`) is the executable specification —
   extend it, don't bypass it.
