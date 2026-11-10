# SkylarkManager

High-performance web management platform and REST API server implemented with U++ `Skylark`, following the `Manager/v2` multi-repository architecture and integrating with `SweAgent` for autonomous development agent workflows.

## Overview

`SkylarkManager` provides a modern web interface and REST API for project management, issue tracking, release coordination, and autonomous coding agents:
- **`uppsrc/ManagerSkylark`**: Main Skylark application with multi-repository `Manager/v2` models, host workstation inspection, local git repository monitoring, REST API endpoints, and test suite.
- **Composed engine sites**: the server also hosts PrivateHub (issues, wiki, discussions, release files over a local workspace) and Science (papers and structured article authoring), each on its own port with its own accounts, sessions, and cookies. See `uppsrc/ManagerSkylark/AGENTS.md` for the route table.
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

## Running the composed sites

```bash
# PrivateHub and Science on their own ports, with the Science corpus
./bin/CLANG/ManagerSkylark --privatehub-port 18211 --science-port 18212 \
  --science-corpus ~/Dev/AnthologyPrivate/site/data

# Create a PrivateHub account (password from MANAGERSKYLARK_PRIVATEHUB_PASSWORD or a prompt)
./bin/CLANG/ManagerSkylark --set-privatehub-user alice
```

## Multi-Platform CI

Continuous integration builds and tests across:
- Ubuntu Linux (Clang)
- Ubuntu Linux (GCC)
- macOS Apple Silicon (Clang)
- Windows x64 (MSVC)
