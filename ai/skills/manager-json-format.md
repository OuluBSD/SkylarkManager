# Manager JSON Document Format

All documents written into the Manager repo (`../Manager/2-plan/**`, `../Manager/0-goal/**`, `../Manager/3-critic/**`) are **JSON files, one file per document** — never markdown, never `.jsonl` dumps. Markdown does not scale programmatically; JSON rows are what the Manager website, the issue pipeline, and other agents consume.

## The document row

One `.json` file holds one object with exactly these keys, in this order. Absent values are `null` (never omit a key, never write `[]`):

```
id, source, kind, title,
path, date, review_status, origin, severity, urgency,
public_risk, public_cs_fit_risk, public_cs_fit_causes,
purpose, contract_provides, contract_excludes, why_return,
hierarchy_fit, children, duplicates, concepts, files,
resemblance, upward, open_questions, components,
findings, notes
```

Field semantics:

- `id` — stable identifier derived from the location (repo path without extension), e.g. `ai-upp/uppsrc/Core/ARCHITECTURE`.
- `source` — the JSON file's own repo-relative path (e.g. `2-plan/ai-upp/uppsrc/Core/ARCHITECTURE.json`).
- `kind` — `overview`, `findings`, `visit-log`, or `report` (any other structured document).
- `title` — human-readable document title.
- `path`, `date`, `review_status`, `origin`, `severity`, `urgency`, `public_risk`, `public_cs_fit_risk`, `public_cs_fit_causes` — scope facts; enum-ish values lowercase (`severity: "high"`, `review_status: "done"`), dates `YYYY-MM-DD`.
- List fields (`purpose`, `contract_provides`, `contract_excludes`, `why_return`, `hierarchy_fit`, `children`, `duplicates`, `concepts`, `files`, `resemblance`, `upward`, `open_questions`, `components`) — arrays of plain strings; one item per bullet/row; no markdown links or `**bold**` inside, use `text <target>` flattening with repo-relative targets.
- `findings` — array of `{"index": int|null, "title": str, "bullets": [str], "code": {"language": str, "source": str}|null, "links": [str]}`; findings are potential issues, the pre-issue database.
- `notes` — array of `{"heading": str, "items": [str]}`; fallback for content that fits no canonical key. Prefer canonical keys over notes; never drop content.

## Rules

1. File name mirrors the document id: `2-plan/<repo>/<area>/<NNN>-<slug>.json`.
2. ASCII-safe, valid JSON, `indent=2`, one document per file.
3. No markdown files: a `.md` file in a Manager plan/goal/critic tree is a bug — write the `.json` row instead. Human-readable rendering is generated from the JSON by the Manager website, not stored.
4. **Task tracking uses the ledger, not documents.** For anything that is a task (id, status, gate), append a row to the planning track's `tasks.jsonl` (e.g. `2-plan/<repo-name>/tasks.jsonl`, or a nested one like `2-plan/ai-upp/uppsrc/Indexer/tasks.jsonl`). Ledger format: `../Manager/docs/TASK_JSONL_FORMAT.md` — one JSON object per line with `id`, `title`, `status` (pending/in_progress/blocked/completed/cancelled), `priority` (high/medium/low), `gate`, optional `notes`/`result`; update rows in place, never reuse ids. The Manager website's editor API validates and conflict-checks these ledgers. The `NN-<task>.json` document form is only for narrative work products (designs, reviews, plans of record).
5. Reference other documents by their `.json` path.
