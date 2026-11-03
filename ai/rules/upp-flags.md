## U++ Preprocessor Flags

### Positive Feature Flags Convention (Replaces Legacy `flagV1`)

Previously, `flagV1` was used with inverted logic (`#ifndef flagV1`) to enable custom extensions, while original upstream code was marked with `#ifdef flagV1`. This created merge friction and exploded `mainconfig` combinations.

The modern convention replaces inverted `flagV1` checks with **positive feature flags**:
- **Baseline / Upstream code is the default**: Pristine upstream Ultimate++ code is unconditional and requires zero extra flags (`"" = "GUI"`).
- **Custom enhancements are gated by positive flags**:
  - `flagAI` (mainconfig `AI`): AI Assistant dialogs, chat panels, and model providers.
  - `flagSHELL` (mainconfig `SHELL`): Embedded interactive developer shell and host.
  - `flagDROPTERM` (mainconfig `DROPTERM`): Quake-style dropdown terminal.
  - `flagVFS` (mainconfig `VFS`): Virtual filesystem AST, indexers, code generators, and VfsBuilder.
  - `flagEDIT3D` (mainconfig `EDIT3D`): 3D scene previewer.
  - `flagMCP` (mainconfig `MCP`): Model Context Protocol server endpoints.
- **Manifest Dependencies**: In `.upp` package files, use positive flags:
  `uses(AI) ide/AI;`, `uses(SHELL) ide/Shell;`, `uses(DROPTERM) DropTerm;`, etc., instead of legacy `uses(GUI & !V1)`.
- **Legacy `flagV1` Status**: Deprecated. Retained only in transitional compatibility macros where `!defined(flagV1)` enables the positive feature flags by default.

### Preprocessor Flag Mapping

U++ mainconfig flags are converted into preprocessor symbols with the `flag` prefix.

- A mainconfig flag named `SOMEFLAG` becomes the preprocessor symbol `flagSOMEFLAG`.
- That means `#ifdef flagSOMEFLAG` can be true when the flag is enabled.
- That does **not** make `#ifdef SOMEFLAG` true.
- When checking build flags in code, always use the `flag...` form.

### X11 and NOGTK on POSIX (DM3-003 decision)

`NOGTK` and `X11` select the same backend on POSIX: `CtrlCore.h` maps
`flagNOGTK -> flagX11`, and `flagX11` selects the X11 platform instead of GTK.
The reverse direction is deliberately not defined.

- **Spell a POSIX GUI without GTK as `X11`** in mainconfig values (`"GUI X11"`),
  not `NOGTK`. `X11` is the canonical spelling; the desktop packages
  (`DesktopManager`, `TaskBar`, `DisplayManager`) all use it.
- **`flagNOGTK -> flagX11` in `CtrlCore.h` must stay.** Eighteen packages spell
  `NOGTK` in mainconfig values (ScriptIDE and GameLauncher spell both spellings
  in one value, `"X11" = "GUI NOGTK"`), and removing the mapping would silently
  flip those builds to the GTK backend.
- **Never add `flagX11 -> flagNOGTK`.** It is a zero-benefit change: an X11
  build already links no GTK library (measured with `ldd` on the largest GUI
  binary, which shows a pure X11/Xft stack), and no code in the tree tests
  `flagNOGTK`, so the mapping would define a flag nobody reads -- inside the
  most-included header in the repo.
- **Never test `flagNOGTK` to mean "GTK available".** The flag says nothing on
  Cocoa or Win32; it is only an alias for X11 selection on POSIX.
