# MultiAtlas worker and release operations

MultiAtlas keeps each work stream in its own repository branch and store. Workers can update separate tracks at the same time; the replay engine combines their declared events and gate facts into the ensemble view. The meta-map is the integration and release timeline. A child gate advances a dependent track or meta version only when its required proof atom is present.

## Before starting work

1. Open the ensemble in MultiAtlas and check the track's configured branch, repository, current version, and dependency requirements.
2. Pull the configured track branch in that track's worktree. For the BrownBear pilot, the worktrees are:

   | Track | Branch | Worktree |
   |---|---|---|
   | vision | `track/vision` | `/home/sblo/OuluBSD/BrownBear-vision` |
   | ai | `track/ai` | `/home/sblo/OuluBSD/BrownBear-ai` |
   | agent | `track/agent` | `/home/sblo/OuluBSD/BrownBear-agent` |
   | hud | `track/hud` | `/home/sblo/OuluBSD/BrownBear-hud` |

   Example for vision: `git -C /home/sblo/OuluBSD/BrownBear-vision pull --ff-only origin track/vision`. Confirm the worktree is clean before making changes.
3. Check the track roadmap and its required atoms. A requirement such as `vision:tested:v1` means the vision track must publish the `tested:v1` gate before this work can pass its entry check.
4. Keep code, store records, and gate evidence on the track branch. Do not write one track's events or acknowledgments into another track's store.

## Work and record events

Work on the assigned track branch only. Commit normal source changes to that branch, and record roadmap events through the MultiAtlas interface or API so each event has the correct track, version, subject, and effects. Event records are append-only and use unique event IDs. Push the track branch when the work is ready for review or another worker needs it.

Workers on independent tracks can work and commit concurrently. MultiAtlas reads each track's own store, then replays tracks in dependency order. A change on one track does not merge or rewrite another track's branch. Coordinate only where the roadmap declares an artifact or gate dependency.

## Run and publish a gate

1. Pull the latest commit for your assigned branch and verify the worktree is clean.
2. Run the track's declared checks against that commit. Record the exact source commit and check result in the gate evidence required by the project.
3. Record a `gate-pass` event for the matching track and version, with the required `tested:<version>` effect. Commit the event and proof with the track's work.
4. Push the branch. The configured bridge hook can then observe the committed gate fact and append the corresponding bridge event to the meta repository.
5. Inspect the MultiAtlas track and meta timelines. The gate should appear as passed only after replay accepts the event and its prerequisites.

Never publish a passing gate based only on a local or uncommitted result. A gate is evidence about a particular version and source commit. If the source changes, rerun the checks and publish fresh evidence as required by the track contract.

## Synchronize with the meta-map

After pushing an event or gate, refresh the ensemble status and roadmap. Check replay errors and warnings, exported gate facts, and blocked requirements. A downstream track or meta version remains pending until every declared prerequisite is available. When a bridge event is missing, verify the child proof was committed and pushed, then inspect the bridge hook and meta store before retrying. Keep bridge events attributable to the source track, gate, and commit.

The meta-map summarizes the ensemble; it does not replace track ownership. Make source changes and track-local roadmap updates in the owning track. Use the meta repository for meta-map records, bridge events, and integration trial evidence.

## Record a meta integration trial

The first configured meta version requires a baseline trial, regardless of its version suffix. Subsequent trial gates are the versions ending in `.999`. Each milestone after the baseline requires the most recent preceding trial gate. A trial milestone also requires its own passing trial.

1. Use the configured meta repository's `main` worktree. Once the child gate requirements are met, run the integration checks against the current source commit and capture its full hash.
2. Save a JSON report under `.multiatlas/meta-trials/<version>/`. Use the dotted version of the baseline or `.999` milestone. List each check by name with its actual result. The report requires at least one check, unique check names, and a `passed` result for every check and the report itself.

   ```json
   {
     "source_commit": "<full tested source commit hash>",
     "version": "1.2.999",
     "result": "passed",
     "checks": [{"name": "integration tests", "result": "passed"}]
   }
   ```

3. Put `evidence.json` beside the report, using the ensemble name and exact meta milestone ID from the registry and roadmap. Set `report_sha256` to the SHA-256 of the report's exact file bytes. The report name must be a plain JSON filename in this same directory.

   ```json
   {
     "schema": "multiatlas-meta-trial/v1",
     "ensemble": "<ensemble name>",
     "track_id": "meta",
     "version": "1.2.999",
     "meta_vid": "<meta milestone ID>",
     "result": "passed",
     "source_commit": "<full tested source commit hash>",
     "report": "report.json",
     "report_sha256": "<64 hexadecimal characters>"
   }
   ```

4. Commit only those two regular files in one commit directly atop the tested source commit. The evidence commit must be the current `main` HEAD when invoking `--meta-trial-pass 1.2.999`. If code changed in the meantime, rerun the checks and prepare fresh evidence.
5. Run `--meta-trial-pass 1.2.999` for the next unrecorded meta milestone. The command checks the child prerequisites in replay and writes a meta-owned `tested:1.2.999` event to the meta store. Run `--meta-advance <meta milestone ID>` separately to advance the milestone. The trial and later milestones depend on the recorded meta gate.

## Warning acknowledgments

Acknowledgments are scoped to a track and warning identity. Acknowledge a warning from its owning track after reviewing it, and include a short reason. Updating another track must not move or clear that acknowledgment. If the warning's identity changes because its underlying finding changed, review the new finding and acknowledge it separately; inspect stale acknowledgments rather than assuming the old one still applies.

## Branch and worktree hygiene

- Keep one worktree per active track branch so parallel workers do not share a checkout.
- Pull before beginning and before publishing; resolve conflicts only in the affected track branch.
- Do not force-push or rewrite shared branch history. Preserve the legacy Atlas family during a pilot until the operator approves its transition.
- Check the ensemble status and the branch's full commit before declaring work complete.

## Operator checks

The examples below use the installed `MultiAtlasVersion` binary and site directory. Run them from the SkylarkManager checkout; substitute the current task ID, gate version, and milestone shown by the ensemble.

```sh
SITE="$HOME/.config/managerskylark"
CLI="$(pwd)/bin/MultiAtlasVersion"
ENSEMBLE=brownbear
TASK_ID=replace-with-task-id
GATE_VERSION=replace-with-gate-version
NEXT_MILESTONE=replace-with-next-milestone

# Overview and next unblocked task
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --status
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --track vision --next

# Record completed task evidence in its owning track
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --track vision \
  --task-done "$TASK_ID" --note "Implemented and verified; commit $(git -C /home/sblo/OuluBSD/BrownBear-vision rev-parse HEAD)"

# Gate checks remain BLOCKED, and pending-gate warnings are expected,
# until the committed gate event and proof marker exist.
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --track vision --gate-check "$GATE_VERSION"
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --track vision --gate-pass "$GATE_VERSION" \
  --commit "$(git -C /home/sblo/OuluBSD/BrownBear-vision rev-parse HEAD)"

# Once child gate proof is committed, validate and record its meta bridge.
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --meta-record-gate vision "$GATE_VERSION"
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --meta-check
# For the initial milestone or a .999 milestone, record the passing trial before advancing.
# "$CLI" "$SITE" --ensemble "$ENSEMBLE" --meta-trial-pass 1.2.999
"$CLI" "$SITE" --ensemble "$ENSEMBLE" --meta-advance "$NEXT_MILESTONE"
```

The worker bridge is the preferred gate path: it runs checks, writes and commits the event plus `.multiatlas/gates/<gate>.json`, then calls `record-gate-pass.sh` to validate committed provenance and record the meta bridge. To retry after a bridge delivery failure, requeue the completed task file as its original `.task.txt` and run the configured adapter with `--once`. The adapter recognizes the committed event and marker and resends the meta notification without creating another child commit. Do not hand-create a second bridge event. The CLI `--meta-record-gate` is an operator path for a proof that is already committed and verified.

Run the configured MultiAtlas version checker for the ensemble after track or registry changes. Confirm the check reports no errors, and inspect warnings as well as gate status. In the web interface, review the meta-map canvas, each track detail page, and the synchronized timeline. Keep the legacy Atlas family intact during the pilot; remove or retire it only after the operator approves the transition. For a pilot deployment, separately verify the live endpoint and capture branch/worktree and UI evidence in the rollout record; synthetic test success alone does not establish production readiness.
