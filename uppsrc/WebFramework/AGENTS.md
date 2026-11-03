# WebFramework Package Guide

`uppsrc/WebFramework` provides reusable, modern web framework infrastructure for Ultimate++ Skylark applications. It establishes a clean separation between **Templates** (layout and density configuration) and **Themes** (KDE Plasma-inspired modular styling), alongside a scalable multi-category **Mega-Menu** navigation engine.

## Core Tenets

1. **Templates vs Themes Separation**:
   - **Templates** govern structure, slot layout, container widths, and padding density (`Dense`, `Compact`, `Comfortable`, `Spacious`).
   - **Themes** govern visual aesthetics: colors, widget/application style, frosted-glass plasma blur, window decorations, SVG icon sets, audio cues, and loading splash screens.
2. **Scalable Navigation & Base Tree View Navigator**:
   - Replaces flat tab rows with a hover-activated **Mega-Menu** that collapses to a compact bar when inactive and expands into rich multi-column category directories.
   - Built-in **Tree View Navigator** for left, right, or dual sidebars with **automatic content generation** (from NavigationModel, file/workspace directory trees, or heading-based Table of Contents outline). Features quick search filter (`/`), expand/collapse all, keyboard navigation, and dock collapse state persistence.
3. **Vivid Plasma Theming**:
   - First-class implementation of the KDE Vivid Cyan Dark and Vivid Light design specifications (`L4ki/Vivid-Plasma-Themes`) featuring deep navy grounds, vivid cyan accents, emerald/crimson/purple state indicators, and translucent frosted glass.
4. **Zero-Asset Audio & Vector Icons**:
   - Web Audio API procedural UI sound effects (clicks, toggles, success chimes) synthesized dynamically.
   - Built-in SVG vector icon registry.
5. **Agent Discoverability**:
   - Every view exposes its semantic navigation structure and state to AI agents (such as SweAgent) via semantic attributes and JSON APIs.

## File Map

| File | Purpose |
|---|---|
| `WebFramework.h` | Umbrella package header |
| `Navigation.h` / `Navigation.cpp` | Navigation models (`NavCategory`, `NavItem`, `NavigationModel`) & `MegaMenuRenderer` |
| `Tree.h` / `Tree.cpp` | Base Tree View Navigator (`TreeNode`, `TreeModel`, `TreeRenderer`, `TreeGenerators` for navigation, directories, and page TOC outline) |
| `Template.h` / `Template.cpp` | Base `WebTemplate`, dual-dock sidebars (`left_sidebar` / `right_sidebar`), density controller, and concrete templates (`MegaMenuTemplate`, `DashboardTemplate`, `AppWorkspaceTemplate`) |
| `Theme.h` / `Theme.cpp` | Modular `WebTheme` engine, KDE Plasma color palettes, and `VividDarkTheme` / `VividLightTheme` |
| `Icons.h` / `Icons.cpp` | `VividIconRegistry` providing theme-integrated SVG vector icons |
| `Audio.h` / `Audio.cpp` | `WebAudioSystem` generating procedural UI sound feedback |
| `PageBuilder.h` / `PageBuilder.cpp` | Unified page assembly pipeline uniting template, theme, navigation, and splash loader |
