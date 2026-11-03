## Logging Rules (U++ Debug Macros)

Use the correct macro for the build context:

| Macro | Output location | Debug build | Release build | Use when |
|-------|----------------|-------------|---------------|----------|
| `LOG(msg)` | `~/.local/state/u++/log/<AppName>.log` | yes | yes | General-purpose; safe in any build |
| `RLOG(msg)` | same log file | yes | yes | Same as LOG; prefer LOG |
| `DLOG(msg)` | same log file | yes | **compile error** | Never use — breaks release builds |
| `LLOG(msg)` | same log file | yes | yes | Verbose/loop logging (can be filtered) |

**Rules**:
- **Always use `LOG(msg)`** for debug output in this codebase.
- **Never use `DLOG`** — it causes a compile error in release builds.
- `RLOG` is equivalent to `LOG` but is considered noisy; prefer `LOG`.
- Log file location is platform-specific (see `uppsrc/Core/Log.cpp`):
  - **POSIX**: `~/.local/state/u++/log/<AppName>.log` (e.g. `ScriptIDE.log`)
  - **Windows**: `<exe-directory>\<AppName>.log` — e.g. `bin\<AppName>.log` when the binary runs from `bin/`
- Remove temporary debug `LOG` calls before committing, or mark with `// TODO: remove`.

Related: `gui-automation.md` (LOG-based verification of GUI runs).
