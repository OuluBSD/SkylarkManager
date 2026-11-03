## Code Readability & AI-Friendly Refactoring Philosophy

This project actively welcomes improvements to code readability and error detectability. The goal is to make problems **obvious** to both AI agents and human developers.

### Core Principles

1. **AI Readability is a Priority**
   - Code should be intuitive for pattern-matching systems to understand
   - Hidden complexity (macros, implicit conversions, shared namespaces) should be made explicit
   - Error messages should guide toward the root cause

2. **Rename Operations Are Welcome**
   - Find-and-replace across all files is a **standard, accepted operation**
   - Always commit before large renames to enable easy reversion
   - Rename to improve clarity, reduce ambiguity, or prevent common mistakes

3. **Suggestions Always Welcome**
   - If naming, structure, or error messages make problems hard to detect → propose improvements
   - If macros hide important information → consider renaming or restructuring
   - If similar names cause confusion → disambiguate them

### Examples of Good Improvements

**Example 1: Disambiguating Shared Namespaces**
- **Problem**: `REGISTER_COMPONENT` and `REGISTER_SYSTEM_ECS` both register into the same global factory with `eon_name` as the key, but different macros falsely suggest separate namespaces
- **Solution**: Renamed to `REGISTER_EON_COMPONENT` and `REGISTER_EON_SYSTEM` to emphasize the shared `EON_*` namespace
- **Why**: Makes it obvious they share the same key space and can collide

**Example 2: Type-Specific Lookups**
- **Problem**: `FindFactoryEon(eon_name)` could match both components and systems with the same name
- **Solution**: Added type parameter: `FindFactoryEon(eon_name, type)` to disambiguate
- **Why**: Prevents incorrect matches and makes intent explicit at call site

**Example 3: Better Error Messages**
- **Bad**: `"failed to create component 'physics'"`
- **Good**: `"failed to create component 'physics': found factory with eon_name='physics' but type=VFSEXT_SYSTEM_ECS (expected VFSEXT_COMPONENT)"`
- **Why**: Immediately reveals the root cause (name collision between types)

### When to Propose Refactoring

Propose refactoring when:
- **Naming is misleading**: Variable/class/macro names that suggest wrong behavior
- **Errors are cryptic**: Error messages that don't explain what went wrong
- **Collisions are possible**: Shared namespaces without type disambiguation
- **Macros hide information**: Critical details buried in macro expansion
- **Similar names differ subtly**: `GetAdd()` vs `AstGetAdd()` with different semantics

### How to Propose Changes

1. **Explain the confusion**: What made the problem hard to detect?
2. **Show the fix**: Concrete rename or restructure proposal
3. **Verify scope**: Use `grep -r` to find all affected locations
4. **Commit first**: Always commit before find-and-replace operations
5. **Test after**: Rebuild and verify tests pass after rename

### Meta-Awareness for AI Agents

When you encounter a bug that was hard to detect, ask:
- Why did the code structure hide this problem?
- Would renaming make the issue obvious?
- Would better error messages catch this earlier?
- Are there other places with similar ambiguity?

**Remember**: This codebase prioritizes clarity over brevity. Making problems obvious is more valuable than elegant abstraction.

Related: `naming-conventions.md`.
