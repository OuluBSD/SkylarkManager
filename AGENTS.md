# AGENTS - OuluBSD/SkylarkManager Guide

**Scope**: Applies to the root of `OuluBSD/SkylarkManager` and all packages within.

## Repository Overview
`OuluBSD/SkylarkManager` is the web-based project management and agent dispatch platform for the U++ ecosystem:
1. `uppsrc/ManagerSkylark`: Web application following `Manager/v2` multi-repo design with REST API, host inspection, and SweAgent web runner.
2. `uppsrc/WebFramework`: Modular web UI framework (navigation, themes, templates, density control).
3. `uppsrc/WebChat`: SSE streaming chat interface connecting to SweAgent LLM backends.
4. `examples/SkylarkGuiExample`: GUI + embedded web server example combining `CtrlLib` and `Skylark`.

## Key Rules & Conventions
- Headless dual-purpose: `ManagerSkylark` runs either as a web server or in automated self-test mode via `--test`.
- Positive feature flags: never introduce `flagV1` or legacy negative flags.
- Build using `uppbuilder`. Sibling assemblies (`BazaarCore`, `SuperIntelligence`, `ultimatepp`) are auto-discovered.
- Code style: follow `/CODESTYLE.md` and `ai/rules/naming-conventions.md`.
