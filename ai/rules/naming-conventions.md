## Package & Symbol Naming Rules

- Challenge and critique sub-optimal or nested package names (e.g. `Core/*`, `Ctrl/*`, `Vfs/*`, `AI/*`, `Eon/*`).
- Traditional U++ names (e.g., Core, Ctrl, Lib) start with a capital letter. Historical exceptions (lowercase) are `ide`, `umk`, `pkg`, and `dbg`.
- Avoid abbreviations and acronyms unless they are extremely common and widely understood.
- Names should be **pronounceable** words (lausuttavia sanoja); they should feel easy to speak aloud, rather than just being read visually.
- **Acronym Casing**: Acronyms should NOT have all their letters in uppercase. Uppercase letters are reserved for separating boundaries in compound words (e.g., write `Ogl` instead of `OpenGL` or `OGL`, `D3d` instead of `D3D`, `Vfs` instead of `VFS`, `Fol` instead of `FOL`).
  - *Exceptions*: Well-known acronyms like `VM` (Virtual Machine) and `XML` (Extensible Markup Language) are exempt and may be written in all-caps if desired.
  - *SQL Exemption*: Symbols/classes inside SQL packages (e.g. `SQL`, `SQLCLASS`, `SQLID`) are allowed to be in UPPER_CASE style to simulate database query syntax, making it easier for developers to bridge C++ and SQL.
- **Macro Casing**: Preprocessor macros must be written in `UPPER_CASE` style (e.g., `SOME_MACRO` or `flagWIN32` for config flags). Preprocessor macros and constants are fully allowed to be UPPER_CASE.
- **Platform-Agnostic Paths**: Do not hardcode raw path separators like `\\` or `/` in code. Use standard U++ macros/constants (`SEP_STR`, `SEP_CHR`, `PATH_SEP`) for platform-agnostic path formatting.
- Non-traditional conventions (e.g., `Soft*`) are not locked and should be challenged. For example, `CtrlCore` and `CtrlLib` are planned to be refactored into sub-packages `Ctrl/Core` and `Ctrl/Lib`.
- Proposed names should be intuitive to computer science professionals and aligned with standard naming patterns in widely adopted open-source software libraries.
- **Grep for the exact name before introducing a class**: especially short/generic ones (`Octree`, `Node`, `Grid`, ...). BLITZ and the shared `Upp` namespace make same-named classes from unrelated packages link together instead of erroring (ODR violation surfaces as SIGSEGV or silent misbehavior, not a compile error). Example incident: a new `Octree` class silently linked against an unrelated package's `Upp::Octree`; renamed to `WorldOctree` to resolve it. File names are safe — only C++ symbol names are namespace-global.

