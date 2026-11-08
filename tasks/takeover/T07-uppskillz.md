# T07 — WP10: UppSkillz curation (OuluBSD/UppSkillz)

## Goal

Create the `OuluBSD/UppSkillz` repository: curated, reviewed U++ knowledge
that is both Claude-skills-compatible (`skills/<id>/SKILL.md` with
name/description frontmatter) and usable with this ecosystem's own
`ai/**/*.md` method. One authoritative source per procedure.

## Inputs

- Historical reference: `/home/sblo/Dev/ai-upp/ai/**` (46 direct skills,
  24 direct rules, plus info/docs/plans/handoffs). **Historical material is
  non-binding** until reviewed; record provenance (source path, review date,
  verdict) per item.
- Current repo-local policy: `SkylarkManager/ai/**` and
  `CompanySkylark/ai/**` (binding, stays repo-local; UppSkillz links to it
  or absorbs the generic parts).

## Known stale guidance to fix during migration

- "MT required" in `ai-upp`-derived skills — multithreading is unconditional
  (upstream `Core.h:6–7`); mainconfigs are being removed (keep only for
  dual-main packages, guarded by `flagMAIN`).
- Monorepo build paths (`bin/build`, `stdsrc/**`) — replaced by uppbuilder
  assembly model.
- "Listing CSS/JS in .upp deploys them" — false (Skylark copy recipes are
  `custom(_NEVER)`); deployment is explicit.
- POST/POST_RAW CSRF caveat: stock Skylark skips identity checks for
  POST_RAW (Dispatch.cpp) — the engine enforces its own fence; skills must
  say so.
- `ValueMap::operator[](int)` key-lookup trap; `Vector` pick-only +
  `clone()` copy idiom; `StaticMutex` member trap; `Connect` EINPROGRESS;
  accepted-socket zero-wait reads — all documented in
  `tasks/takeover/T00-context-and-rules.md`, ready to become skill content.

## Structure

```
skills/<skill-id>/SKILL.md      # frontmatter name+description; <500 lines
skills/<skill-id>/references/   # deep material
ai/rules|skills|info/*.md       # ecosystem method (or pointers into skills/)
HISTORY.md                      # per-item provenance from ai-upp
```

## Work items

1. Create the repo (coordinate with whoever owns OuluBSD repo creation).
2. Migrate in review order: U++ fundamentals (containers, Value/Jsonize,
   flags, formats) → web (Skylark/witz/routing/sessions/fences — updated for
   the engine) → verification (headless/--test patterns) → coordination
   (planner-worker loop) → domain references (opt-in).
3. Keep `ai-upp/ai` untouched (it is being split by another agent; do not
   edit it).
4. Point `SkylarkManager/ai` and `CompanySkylark/ai` rule files at UppSkillz
   where a skill supersedes local text; keep repo-specific policy local.

## Acceptance

- `SKILL.md` files have valid frontmatter and relative references; no
  duplicated authoritative procedure exists in two places; stale guidance
  fixed; provenance recorded.
