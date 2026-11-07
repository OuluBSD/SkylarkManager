# WebChat Package Guide

`uppsrc/WebChat` is a reusable chat UI and session module for Ultimate++ Skylark applications. The current implementation is a local conversation/demo transport; it is **not connected to a model provider**. Every embedding application must install an authorization callback before the routes are usable.

## Key Features

- **Typical Modern Chat UI**: Left sidebar for session history and active context; main conversation thread with distinct human and assistant speech bubbles; multiline auto-expanding prompt input.
- **Local text streaming**: Server-Sent Events (`/api/chat/stream/{id}`) deliver deterministic local demo text; no model call is made.
- **Conversation roles**: Role metadata is restricted to known UI values; it grants neither execution nor approval authority.
- **Plain-text message rendering**: Stored user/assistant content is HTML-escaped and displayed as text. Do not re-enable raw HTML/SVG/Markdown rendering without a reviewed sanitizer or isolated preview boundary.
- **Numbered Pagination System**: Old-school numbered page navigation (`[<<] [1] [2] ... [Live] [>>]`) with configurable window size (10 messages/page) and an auto-sliding "Live" window for streaming updates.
- **Message Bubble Controls & Branching**: Per-message action toolbar with Copy raw text, Fork into a branched session (`/api/chat/fork`), Revert/undo back to checkpoint (`/api/chat/revert`), Retry prompt, and Delete message.
- **Legacy custom viewers**: Old viewer implementation is not used for untrusted conversation content. Do not expose it without a sanitizer and isolated rendering boundary.
- **Provider status**: External dispatch is disabled pending explicit workspace/provider policy, training-eligibility checks, and a real job pipeline. Do not describe local demo text as model-generated work.

## File Map

| File | Purpose |
|---|---|
| `WebChat.h` | Umbrella header for WebChat module |
| `Session.h` / `Session.cpp` | Data models (`ChatMessage`, `ChatSession`, `ChatSessionManager`) with JSON serialization |
| `Streaming.h` / `Streaming.cpp` | Deterministic local demo text stream; no external provider dispatch |
| `ChatView.h` / `ChatView.cpp` | HTML/JS/CSS generator for the self-contained chat interface |
| `ChatRoutes.h` / `ChatRoutes.cpp` | Skylark route handlers (`/chat`, `/api/chat/sessions`, `/api/chat/message`, `/api/chat/stream/*`, `/api/chat/fork`, `/api/chat/revert`, `/api/chat/message/delete`) |

## Skylark Integration

To embed WebChat into any Skylark application:
1. Add `uses WebChat;` in the `.upp` package file.
2. WebChat registers routes under `/chat` and `/api/chat/*`; routes return unavailable until the host installs an authorization callback.
3. Use `WebChat::RenderChatPage(...)` or `WebChat::RenderChatView(...)` to embed the UI inside existing site templates.

## Safety Requirements

- Authorization is fail-closed when no callback is configured. A private host must enforce authorization on UI, JSON, mutation, and SSE routes.
- Unsafe requests require same-origin validation. Do not add wildcard CORS to private streams.
- Do not persist credentials/session tokens or send conversation/source content to a provider without explicit policy and a recorded job.
- Self-tests use synthetic in-memory sessions and deterministic local output; they must not read ambient chat stores, browser profiles, or network providers.
