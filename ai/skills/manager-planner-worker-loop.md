# Manager Planner-Worker Loop

## Purpose

Run **two AI sessions** — one **planner**, one **worker** — coordinated
exclusively through the `bin/CLANG/Manager` executable. The human operator
relays only very short copy-paste messages between the sessions (usually
just the word "continue"). No markdown hand-off files, no scratch
documents, no chat-channel state: the `./mgr/` JSON store in the working
repo **is** the communication channel.

Use this when a job needs role separation (one mind plans with the human,
another mind executes), or when one long task must be split across context
windows without losing state.

## Prerequisites

```
bin/build -m CLANG Manager        # or your platform's method
bin/build -m CLANG ManagerTests   # optional sanity: ./bin/CLANG/ManagerTests
```

Both sessions must run with their shell cwd inside the same repository —
`Manager` resolves `./mgr/` from the calling directory. In the prompts
below `M` means the absolute path to the Manager binary (paste the real
path in when starting a session).

## Channel Contract

Tasks are the only message type. Fields carry the two directions:

| Field | Written by | Read by | Contents |
|-------|-----------|---------|----------|
| `title` | planner | both | one-line summary |
| `description` | planner | worker | context/why, links to code areas |
| `details` | planner | worker | longer spec, file lists, constraints |
| `instructions` | planner | worker | **the actionable step** — imperative, self-contained |
| `result` | worker | planner | **outcome + verification evidence** ("build OK, 70/70 checks pass") |
| `status` | both | both | lifecycle, see below |
| `link-next-task` | optional | worker | order override when seq order is wrong |

### Status Lifecycle

```
open ──(worker claims)──> in-progress ──(work done)──> done
   ^                         |
   └──(planner reopens)──────┴──(worker cannot proceed)──> blocked
```

- `next task` hands out **only exact-`open`** tasks (lowercase, free-form
  otherwise). `in-progress`, `done`, `blocked` are all skipped.
- Worker **never creates tasks** and **never writes instructions**.
- Planner **never writes `result`** and never executes repo work itself
  (it may run read-only verification: builds, tests).
- Single-writer rule: **one worker session per project.** The
  `in-progress` claim is the lock; two workers would race it.
- Tasks are small: one task = one verifiable step (minutes, not hours).
  The planner splits anything bigger up front.

## Session Bootstrap Prompts

Paste these into the two fresh sessions (adjust `M` and `<project>`).
Everything after that is driven by the operator pasting **"continue"**.

### Planner session

```
You are the PLANNER in a Manager planner-worker loop. We communicate ONLY
via the tool M=/abs/path/bin/CLANG/Manager run in /abs/path/repo — never
via files or markdown. Task fields: instructions/description/details are
yours (planner -> worker); result and status are the worker's.

On "continue":
1. Run: $M list task <project>
2. For any status=done task, run: $M print task <project> <phase> <idx>
   and review its result. If the result is unsatisfactory, revise:
   $M set task <project> <phase> <idx> instructions "<better step>"
   $M set task <project> <phase> <idx> status open
3. If a task is in-progress or blocked, wait (do not create dupes);
   answer "busy: <title>".
4. Write the next step(s) I dictate as tasks:
   $M add task <project> <phase> "<title>" instructions "<what>" \
      description "<why/context>"
5. Answer with one short line only (e.g. "queued: <title>").

New phases only when I say so: $M add phase <project> "<title>".
Never write the result field. Never edit code yourself (read-only
verification is allowed).
```

### Worker session

```
You are the WORKER in a Manager planner-worker loop. We communicate ONLY
via the tool M=/abs/path/bin/CLANG/Manager run in /abs/path/repo — never
via files or markdown.

On "continue":
1. Run: $M next task <project>
   - exit 2 ("no open tasks") → answer "no open tasks — waiting" and STOP.
2. Otherwise read the printed task. Claim it:
   $M set task <project> <phase> <idx> status in-progress
3. Execute the instructions field in this repo. Work until verifiable.
4. Report back through the SAME task (result first, status second):
   $M set task <project> <phase> <idx> result "<what+evidence>"
   $M set task <project> <phase> <idx> status done
   If you cannot proceed: result "<what is blocking>" + status blocked.
5. Answer with one short line only (e.g. "done: <title>").

Never create tasks. Never touch the instructions field. Keep the result
to one line with hard evidence (build OK / test counts / file paths).
```

## Operator Cheatsheet

1. Start both sessions with the bootstrap prompts above.
2. Discuss direction only with the **planner** session; it decomposes
   work into phases/tasks (`add project` / `add phase` happen here).
3. Paste **continue** alternately: worker acts, then planner reviews.
4. If the worker reports `blocked`, decide with the planner, not the
   worker.
5. Milestone reached: `cd <repo> && $M git commit` snapshots `./mgr/`
   into the repo history with an auto-generated message.

## Failure Modes

| Symptom | Recovery |
|---------|----------|
| Worker session died mid-task | Task stays `in-progress`; a fresh worker can't see it. Planner/operator resets: `set task ... status open`, then continue. |
| Queue empty, more work remains | Planner session: add the next task(s). `next task` exits 2 until then. |
| Result looks wrong | Planner revises `instructions` and reopens (`status open`). The old `result` stays as history until the worker overwrites it. |
| Two workers on one project | Unsupported. `in-progress` is a cooperative claim, not an atomic lock — keep one worker per project. |
| Store drifted / corrupted | `Manager` fails loudly on malformed JSON (file path in the error). Fix by hand; the store is plain text. |

## Command Card (the only ones the loop uses)

```
$M add task <proj> <phase> "<title>" instructions "..." description "..."
$M list task <proj>                         # planner overview
$M print task <proj> <phase> <idx>          # full item incl. result
$M next task <proj>                         # worker's queue pop (exit 2 = empty)
$M set task <proj> <phase> <idx> {status,instructions,result,...} <value>
$M git commit                               # snapshot ./mgr into the repo
```

## Relationship to Other Skills

- `multi-agent-coordination.md` — the *simultaneous*-agents mechanism
  (.agent-plans lock files). Not needed here: one worker at a time, and
  the `status` field is the lock.
- `ai/rules/plan-files.md` — plan files still belong to the user and live
  in the Manager repo plan tree. This skill governs the live task queue,
  not the plan documents; a PLANNER may also maintain a plan file when
  the user asks for one, but the worker never reads it.
