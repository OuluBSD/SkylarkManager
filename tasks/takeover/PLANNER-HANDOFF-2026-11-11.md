# PLANNER HANDOFF — 2026-11-11 (for the next S1 planner session)

You are the next **S1 planner**, continuing a completed-and-verified
campaign. Everything you need is in files; this document is the index and
the setup.

## 1. Mission and background

The OuluBSD ecosystem is a set of U++ (Ultimate++) repos managed by AI
agents with a planner/worker protocol:

| Repo | Role | Visibility |
|---|---|---|
| `~/Dev/SkylarkManager` | composed web host: manager dashboard, GitHub/SourceForge webapps, WebChat, host inspection + engine sites (PrivateHub, Science, TaskFoundry), SiteCli, AgentLab | public |
| `~/Dev/CompanySkylark` | the Company webapp: proposal→work→review→compensation, **plus** clients/contracts/projects (client+employee roles) | private |
| `~/Dev/BazaarCore` | patched foundation: Core (+Ext, TOON), Skylark, Manager; patches in `patches/` | public |
| `~/Dev/ultimatepp` | pristine upstream (never edited) | public |
| `~/Dev/UppSkillz` | curated AI-skills repo (published) | public |
| `~/OuluBSD/AgentIPC` → `~/OuluLinux/AgentIPC` | the agent communication tool (`agentipc` CLI; the OuluBSD copy was moved — the `~/bin/agentipc` symlink points at OuluLinux) | public |

The prior campaign (the "takeover", T01–T07 + backlog sweep + AgentLab) is
**complete and verified** — see §4. Your job: continue with the next plan
steps (§6) using the same planner/worker loop (§4).

## 2. Current verified state (as of this handoff)

- Fence self-test: **187/187** (`./bin/WebEngineTests` in SkylarkManager).
- ManagerSkylark `--test`: PASSED (CLANG + GCC; build needs
  `-a ../CompanySkylark/uppsrc` since ManagerSkylark composes the Company
  module).
- CompanySkylark `--test`: **220 checks ALL PASSED** (CLANG + GCC).
- Composed-site smoke: green (`upptst/ComposedSite/smoke.sh <Mt binary>`).
- AgentLab scenarios: all four PASS live (github-pr-flow 11/11,
  privatehub-issues 8/8, science-authoring 9/9, taskfoundry-submit 19/19)
  with per-agent announcers; oversight client matched 16/16 on the private
  company-delivery run.
- Both trees clean at: SkylarkManager `7de5666`, CompanySkylark `26afd74`.

## 3. The S1/S2 protocol and channel (exact commands)

Work directory: `~/skylark` (`plan.md` numbered steps, `tasks/NNN-*.md`,
`reports/NNN.md`, `final.md`). Chat: encrypted virtual server **skylark**
on 127.0.0.1:7440, channel **SKYLARK-MANAGER**. The client needs PyNaCl —
always invoke through the venv python.

```sh
export S1_PW=123
SK="/home/sblo/.venvs/agentipc/bin/python /home/sblo/OuluLinux/AgentIPC/bin/agentipc vs --direct 127.0.0.1:7440 --server skylark --user s1 --password-env S1_PW"

$SK read SKYLARK-MANAGER                                   # catch up
$SK say SKYLARK-MANAGER "TASK tasks/007-<name>.md"         # assign work
timeout 1800 $SK wait SKYLARK-MANAGER --from s2            # block for S2
```

**Quirks learned (do not rediscover):**
- The node runs with home `~/.agentipc-skylark` (config: node skylark-a,
  listen 7440, control 7441). Commands that touch a home (account add,
  server create, node run) MUST pass `--home /home/sblo/.agentipc-skylark`.
  vs client commands take NO home flag.
- Accounts (all password `123`): s1 (you), s2 (worker), a1..a5 (scenario
  announcers — created/removed per run by the runner). If login fails with
  correct credentials, the node holds stale in-memory accounts: kill the
  node process and restart it:
  `agentipc --home ~/.agentipc-skylark node run` (run it persistently, e.g.
  nohup; a stale duplicate start fails with "Address already in use").
- The S2 session's blocking `wait` can be killed by the **memory reaper** —
  S2 resumes when you re-post the TASK (their rules make repeats safe).
- Client invocations without the venv python hit the getpass prompt and
  hang in non-interactive shells.
- Full scenario doc: `/home/sblo/OuluLinux/AgentIPC/docs/scenarios/skylark-manager.md`.

## 4. Completed campaign (pointers)

- `~/skylark/plan.md` + `~/skylark/final.md` — the AgentLab plan and its
  verified final status (six steps, commits per step).
- `~/skylark/reports/001..008.md` — worker reports (S1-verified).
- `~/Dev/SkylarkManager/tasks/takeover/` — the takeover campaign: briefs
  (WORKER-BRIEF-01..14 + 06A/09B), worker log, BACKLOG.md (items 1–5 done,
  6–9 decided/recorded), HANDOFF-2026-11-11.md (webapp inventory,
  meta-auth, candidates).
- Master design docs: `~/.local/share/kilo/plans/1794078096401-*.md`.
- UppSkillz: https://github.com/OuluBSD/UppSkillz (7 skills + provenance).

## 5. Conventions and traps

- **Pathspec commits only** (`git commit -m ... -- <paths>`); never bare
  `git commit` (it sweeps other agents' staged files — happened once).
- Every commit gates: fence suite, ManagerSkylark `--test` (with the
  CompanySkylark `-a`), CompanySkylark `--test`, GCC rc 0 on all targets.
- U++ traps and the engine's binding rules:
  `~/Dev/SkylarkManager/tasks/takeover/T00-context-and-rules.md` — read it
  before writing tasks. Also `uppsrc/*/AGENTS.md` per package.
- Fences are the executable specification: extend
  `upptst/WebEngineTests` + `upptst/AgentLab`, never bypass.
- Build-config directory switches require redeploying WebFramework assets
  and app templates beside the new binary (`deploy-assets.sh` +
  `deploy-templates.sh`) — pages throw otherwise.

## 6. Open work (write these as the next plan steps)

1. **Compose the MetaAccount site** into the real host process
   (`InstallMetaAccountSite` exists; EngineComposition.cpp gains it) and
   move TaskFoundry grant consent from operator CLI to user-facing
   approve/deny (BACKLOG item 6). Accounts a1..a5/s1/s2 exist; the
   Science caller-binding pattern is the template.
2. **Gate PrivateHub login-only writes** with the member role (BACKLOG
   item 8 — planner decision made: members write, viewers read; three
   checks + fence negatives).
3. **Aria/webdriver phase**: replay the AgentLab scenarios through the
   browser UI (the same routes the CLI drives).
4. **Candidate webapps** (handoff §5 in
   `tasks/takeover/HANDOFF-2026-11-11.md`): Channels (AgentIPC frontend),
   Notifications/Inbox, Financials dashboard, Milestones/Calendar,
   Terminal (SiteCli in browser), cross-project Docs, UppHub browser.
5. Backlog residuals: completion-gate thresholds (4/5), near-done adoption
   threshold, forge repo creation, GitHub host-route role matrix, Windows
   LockFileEx runtime validation.
6. CI: keep the CompanySkylark checkout + `-a` flags; CompanySkylark jobs
   exist; SiteCli/AgentLab jobs are not in CI yet (add when stable).

## 7. Verification commands

```sh
cd ~/Dev/SkylarkManager
uppbuilder -m CLANG -a ../CompanySkylark/uppsrc ManagerSkylark -v
./bin/ManagerSkylark --test                       # PASSED
./bin/WebEngineTests                              # fence 187/187
bash upptst/ComposedSite/smoke.sh bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Mt-Posix-Shared/ManagerSkylark
python3 upptst/AgentLab/agentlab.py --binary bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Mt-Posix-Shared/ManagerSkylark \
  --site-cli bin/CLANG-Blitz-Clang-Debug-Debug_Full-Linux-Posix-Shared/SiteCli \
  --scenario upptst/AgentLab/scenarios/<name>.json
cd ~/Dev/CompanySkylark
uppbuilder -m CLANG -a ../SkylarkManager/uppsrc CompanySkylark -v
./bin/CompanySkylark --test                       # 220 checks ALL PASSED
```

Deployment note: after switching build-config directories, redeploy
WebFramework assets and app templates beside the new binary
(`deploy-assets.sh` + `deploy-templates.sh`) — pages throw without them.
