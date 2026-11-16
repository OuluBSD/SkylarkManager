# Atlas worker tutorial

This is the general guide for an AI or human worker who implements a project's planned work from its Atlas map. The
project-specific instructions (which versions to implement, what the work means) live in the project's own repository.

## What Atlas is

Atlas is a roadmap held as a simulation. Each **version** (written `a.b.c`) is a point in the plan. The map says which
work is planned in which version, what that work needs before it starts (its **entry**), and what it is expected to
produce (its **outcome**). Nothing in Atlas is ever marked done by a person. Your job is to make the real project match
the plan, and to tell the plan's owner where the plan is wrong.

## Versions

- Work in version `a.b.c` is part of release `a.b`'s group. Items in `a.b.0` to `a.b.998` are the work of that group.
- `a.b.999` holds **test gates only**. It is a beta of the next minor version, never a release. A gate says the group's
  work is tested together. Passing a gate means every entry of the gate holds.
- Version `0.0.0` holds **work items only**, and the tasks needed for the first group. Tasks are never logged in a `.999`.
- A **task** is a piece of work done by a person or a machine run, such as "Implement OmahaTrainerCLI". A task is logged
  in the version just before the work that needs it, so you can see the workload of each version.
- A **user action** is one thing a user can do with a feature, such as "train-omaha4 (cli)". Each user action names a
  feature or a task. It is an element of its own, with its own entry and outcome.

## Reading the plan from the command line

The `atlasver` program prints the plan. It only reads the store and never writes.

    bin/AtlasVersion <store data dir>                    the versions, one line each
    bin/AtlasVersion <store data dir> --version 0.0.1    one version in full
    bin/AtlasVersion <store data dir> --items 0.0        every version of a group: the items to implement

For each work item it prints:

- `entry`: what must hold before the work starts. `task:<id>:done` is a task that a person or machine must finish first.
  `built:<Package>` is a U++ package that must exist. `done:<name>` is a feature from an earlier version.
- `outcome`: what the work is expected to produce once it is done.
- `user action`: a user action this item delivers, with its medium (cli, gui, api, overlay) and feature.
- `NOT DECLARED`: the plan does not say. Report it; do not guess.

## How to implement an item

1. Read the item and its entry. Check that each entry holds in the real project before you start. If one does not hold,
   report it instead of building around it.
2. Build the outcome in the real project, in the package the item names. Keep to the package layout the project uses.
3. Run the project's tests. If the item has a test gate, the gate's entry must all hold before the gate passes.
4. Note what you could not finish, and why, in your report.

## What to report back

The plan's owner wants feedback, not only code. For each version you implement, report:

- **Incoherent:** items whose entry or outcome does not match the real project, duplicates, items that cannot be done in
  their version, and any item that needs a task it does not name.
- **Good:** items that were clear, correctly ordered and easy to build.
- **Ideas:** changes that would make the next version easier: a split or a merge of items, a missing task or user action,
  a better name.

Write the feedback where the project's instructions say. Do not edit the Atlas store yourself: changes to the plan are the
owner's decision, made with the planner tool or the Atlas pages.

## Checks on the live portal

The Atlas check lists what the plan gets wrong. A check error that is an unacknowledged warning is a warning the owner has
not reviewed yet. Report any other error, with its text.
