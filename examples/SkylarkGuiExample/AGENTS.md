# SkylarkGuiExample Package Guide

`examples/SkylarkGuiExample` is a demonstration application combining a U++ GUI (`CtrlLib`) with an embedded `Skylark` web server.

## Features

- **Embedded HTTP Server**: Runs a multi-threaded `Skylark` web server within the same process.
- **Bi-Directional Communication**: GUI actions update the web interface state, and web client requests dispatch actions back into the GUI main thread via `GuiLock`.
- **Witz Template Rendering**: Uses Skylark `.witz` templates (`index.witz`) and handlers (`Handlers.icpp`).

## Verification

Build using `uppbuilder examples/SkylarkGuiExample` with sibling assemblies (`BazaarCore`, upstream `ultimatepp`).
