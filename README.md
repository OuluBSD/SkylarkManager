# SkylarkManager

High-performance web management platform and REST API server implemented with U++ `Skylark`, following the `Manager/v2` multi-repository architecture and integrating with `SweAgent` for autonomous development agent workflows.

## Overview

`SkylarkManager` provides a modern web interface and REST API for project management, issue tracking, release coordination, and autonomous coding agents:
- **`uppsrc/ManagerSkylark`**: Main Skylark application with multi-repository `Manager/v2` models, host workstation inspection, local git repository monitoring, REST API endpoints, and test suite.
- **`uppsrc/WebFramework`**: Reusable Skylark UI framework with templates, navigation menus, and KDE Plasma theme integration.
- **`uppsrc/WebChat`**: Reusable web chat UI module with token streaming, pair programming, and markdown rendering.
- **`examples/SkylarkGuiExample`**: Example application demonstrating bi-directional integration of a U++ GUI (`CtrlLib`) with an embedded `Skylark` web server.

## Dependencies

- **`OuluBSD/BazaarCore`**: Patched U++ standard library (`Core`, `Core/Ext`, `Skylark`, `Manager`, `Ctrl/Core`, etc.).
- **`OuluBSD/SuperIntelligence`**: Autonomous agent framework (`SweAgent`).
- **`ultimatepp/ultimatepp`**: Upstream U++ base packages (`Sql`, `CtrlLib`, etc.).
- **`OuluBSD/UppBuilder`**: Multi-assembly build tool.

## Building with UppBuilder

```bash
# Build ManagerSkylark
uppbuilder ManagerSkylark -v

# Run automated tests
./bin/ManagerSkylark --test

# Build SkylarkGuiExample
uppbuilder examples/SkylarkGuiExample -v
```

## Multi-Platform CI

Continuous integration builds and tests across:
- Ubuntu Linux (Clang)
- Ubuntu Linux (GCC)
- macOS Apple Silicon (Clang)
- Windows x64 (MSVC)
