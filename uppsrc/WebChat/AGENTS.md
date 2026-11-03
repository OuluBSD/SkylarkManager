# WebChat Package Guide

`uppsrc/WebChat` is a reusable, modular web chat framework for Ultimate++ Skylark applications. It provides a modern AI chat UI, server-sent events (SSE) real-time token streaming, human-in-the-loop pair-programming discussion, and in-browser markdown rendering for AI agent responses.

## Key Features

- **Typical Modern Chat UI**: Left sidebar for session history and active context; main conversation thread with distinct human and assistant speech bubbles; multiline auto-expanding prompt input.
- **Token / Text Streaming**: Server-Sent Events (`text/event-stream`) streaming endpoint (`/api/chat/stream/{id}`) delivering tokens progressively as generated.
- **Pair Programming Mode**: Human operator and agent discuss code, plan changes, and review suggestions before execution. Includes role switches (Pair Programmer, Code Reviewer, Architect, Debugger) and action approval controls.
- **Client-Side Markdown Rendering & Tidy Pre-processor**: Integrates Marked.js with a robust `tidyMarkdown` pre-processor that auto-normalizes unclosed fences and inline backtick code boundaries (e.g. `text: ```diff`).
- **Numbered Pagination System**: Old-school numbered page navigation (`[<<] [1] [2] ... [Live] [>>]`) with configurable window size (10 messages/page) and an auto-sliding "Live" window for streaming updates.
- **Message Bubble Controls & Branching**: Per-message action toolbar with Copy raw text, Fork into a branched session (`/api/chat/fork`), Revert/undo back to checkpoint (`/api/chat/revert`), Retry prompt, and Delete message.
- **Extensible Custom Viewers Architecture**: Pluggable `CustomViewers` registry (`CustomViewers.register(lang, handler)`) with built-in renderers:
  - `diff`: Rich line-numbered diff table with additions (`+` green), deletions (`-` red), hunk headers, diff stats, raw toggle, and clipboard copy.
  - `svg`: Interactive SVG canvas preview with source inspection and `.svg` download.
  - `html`: Sandboxed live preview with code toggle.
  - `csv` / `tsv` / `spreadsheet`: Formatted tabular data grid with row counters.
  - `obj` / `3d`: Wavefront 3D mesh geometry parser and inspector (vertices, faces, normals).
  - `chart` / `mermaid`: Visual diagram / flowchart card.
  - Default syntax highlighting via Highlight.js (`atom-one-dark`).
- **OpenRouter & Local Fallback**: Integrates directly with `uppsrc/SweAgent` provider runtime and supports offline / mock streaming for test environments.

## File Map

| File | Purpose |
|---|---|
| `WebChat.h` | Umbrella header for WebChat module |
| `Session.h` / `Session.cpp` | Data models (`ChatMessage`, `ChatSession`, `ChatSessionManager`) with JSON serialization |
| `Streaming.h` / `Streaming.cpp` | Token stream provider adapter connecting OpenRouter / local generators to callbacks |
| `ChatView.h` / `ChatView.cpp` | HTML/JS/CSS generator for the self-contained chat interface |
| `ChatRoutes.h` / `ChatRoutes.cpp` | Skylark route handlers (`/chat`, `/api/chat/sessions`, `/api/chat/message`, `/api/chat/stream/*`, `/api/chat/fork`, `/api/chat/revert`, `/api/chat/message/delete`) |

## Skylark Integration

To embed WebChat into any Skylark application:
1. Add `uses WebChat;` in the `.upp` package file.
2. WebChat automatically registers routes under `/chat` and `/api/chat/*`.
3. Use `WebChat::RenderChatPage(...)` or `WebChat::RenderChatView(...)` to embed the UI inside existing site templates.
