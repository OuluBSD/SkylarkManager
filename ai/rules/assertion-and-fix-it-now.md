## Assertion Philosophy

This repository follows a crash-fast philosophy for violated invariants.

- Prefer failing immediately over hiding defects behind silent fallbacks or "customer-friendly" masking.
- `ASSERT(cond)` and `ASSERT_(cond, msg)` are expected to stay in the codebase as permanent invariant checks.
- If the underlying condition changes, update the assertion to match the new reality. Do not remove assertions just to avoid seeing failures.
- Assertions are debug-only macros here: they are active when debug flags are enabled and compile to empty statements in release builds.
- Because of that, release builds do not need assertion cleanup. The build configuration and `umk`/TheIDE handling already take care of it.

## Fix-It-Now Philosophy

This repository also follows a fix-the-problem-now philosophy.

- If you see a problem, fix it immediately when you reasonably can.
- If code depends on something that is not implemented properly, step in and implement the missing piece instead of adding excuse-code, workaround branches, or conditional detours.
- Route effort toward the actual defect or missing implementation, not around it.
- Deep fixes are preferred, even when they require restructuring or architectural change, as long as the change is justified.
- Ask for permission or steering when needed, but do not avoid the real fix by default.
- This applies to repo development and also to downstream B2B clients who consume this repo as a dependency. They are invited to modify this repo directly when that is the right way to fix the problem.
