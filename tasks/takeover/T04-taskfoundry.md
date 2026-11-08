# T04 — WP7: TaskFoundry webapp

## Goal

Build **TaskFoundry** (name confirmed by the user; alternatives were
ProposalStudio/ResearchWorkbench): the webapp where candidates become
**proposal-ready packages** that are submitted to CompanySkylark.

## Preconditions

- T01 merged (composed host); T03's Science broker service available.

## Deliverable definition (settled with the user)

A **proposal-ready package**: structured task specification, source/evidence
references (incl. Science paper revision + node ids, PrivateHub branch/fork
refs), validation results. **Not** a runnable implementation — that is an
explicit later readiness level. Submission is idempotent and returns the
Company proposal id; later TaskFoundry edits never mutate the submitted
version.

## Work items

1. **New package `uppsrc/TaskFoundry`** (engine site module + broker service):
   - Data model (`WebStorage` store, site-scoped): candidates, proposals,
     evidence links, validation results, submissions. Envelope-style records
     (`schema_id "taskfoundry/proposal"` v1 etc.).
   - Routes: candidate planning board, proposal editor (minimal HTML until
     T02), validation view, submission status.
   - **PrivateHub integration via broker**: request branch/local fork on a
     repo (`PrivateHubWorkspace` gets explicit guarded ops for
     branch/fork creation in T01 scope or here — argv-only git, refuse on
     dirty worktree, never auto-stash).
   - **Science integration via broker**: cite papers (revision + node id).
   - **Company submission via broker**: CompanySkylark registers a broker
     service (T05 prerequisite: Company becomes a dual-use module — do the
     minimal slice of T05 first: module registration + a submission op that
     creates a Company proposal draft). Idempotency key = proposal content
     hash; duplicate submissions return the prior Company proposal id.
2. **Broker contracts** (versioned, in `WebBroker` style): document ops,
   payloads, error semantics in the package AGENTS; correlation ids recorded
   by the broker automatically.
3. **Fence test additions**: candidate → proposal → validate → submit →
   Company proposal id; duplicate submit returns same id; edits after
   submission do not change the submitted snapshot.

## Acceptance

- End-to-end through the fence test: Science cite + PrivateHub branch ref +
  proposal validation + idempotent Company submission, all visible in broker
  diagnostics.
- Both repos green; CI green.

## Notes

- Company's review/compensation lifecycle remains authoritative on the
  Company side; TaskFoundry only creates a draft proposal and tracks its id.
  No compensation logic here.
- Keep proposal records envelope-style with revisions; submissions snapshot
  the whole package (content-addressed).
