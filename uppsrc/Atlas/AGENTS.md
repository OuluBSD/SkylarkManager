# Atlas Package Guide

`uppsrc/Atlas` is the topological map webapp (portfolio priority 1, task 029):
a 2D top-down map of the project on a virtual plane. Scales are world plane,
continents, blocks (each with an inside-map) and places. The layout encodes
meaning: `hot` places are where work concentrates, `sea` regions are unsolved
or risky terrain, and `progress`, `communication` and `dependency` roads show
how the parts connect.

## File Map

| File | Role |
|------|------|
| `AtlasModel.h/.cpp` | Pure model: elements, roads, world, validation, containment queries, the seed parser and the deterministic seed layout. No storage, no HTTP. |
| `AtlasSite.h/.cpp` | `InstallAtlasSite`: routes, the revision-checked store helpers, server-rendered pages, and the mutations. |
| `AtlasClient.cpp` | The inline stylesheet and the one vanilla client module (pan, zoom, drag-to-move, plane entry, status colors). |
| `AtlasSelfTests.cpp` | `RunAtlasSelfTests`: model, seed and store checks, run from ManagerSkylark `--test`. |

## Invariants

1. **Authoritative for structure.** The map document is the plan of record
   for spatial structure, status and connections. Atlas does not read or
   write Manager v2 data and has no cross-app links in v1.
2. **One document per map version.** The key is `<map>-<version>` (for
   example `pkr-v0.0.1`). The elements marked into a version's map are that
   version's picked goals.
3. **Revision-checked writes.** Every mutation carries the revision the form
   was rendered from. A stale revision is refused (409), never merged. Every
   write validates the whole map first; a map that fails validation is not
   saved.
4. **Access.** Any signed-in account of the site reads. Only the `admin`
   role mutates. Mutations are fenced by the engine (exact Origin and the
   server-side CSRF token), so the handlers do not repeat that check.
5. **No external assets.** The page loads no CDN script or stylesheet. The
   client module builds SVG with `createElementNS` and sets names through
   `textContent`; the server escapes every name and embeds the map JSON with
   `<`, `>` and `&` escaped.
6. **Seed import fills an empty map only.** The parser builds into a scratch
   map and commits only when the result validates; an error leaves the map
   untouched.

## Seed format

Two spaces per containment level, then `Name | kind | status | note`. Edges
use names: `A -> B: label` (progress), `A => B: label` (dependency),
`A ~> B: label` (communication). Kind defaults to `block` when an element has
children, else `place`; status defaults to `planned`. Full rules are in
`AtlasModel.h`.

## Routes

| Route | Description |
|-------|-------------|
| `GET /atlas` | Map list; create form for admins |
| `POST /atlas/create` | Create a map version (admin) |
| `GET /atlas/{map}` | Canvas (client module) plus element and road lists; edit panel for admins |
| `GET /atlas/{map}/element/{id}` | Detail: facts, containment path, roads, goal, edit form |
| `POST /atlas/{map}/element/move` | Drag-to-move, JSON answer (admin, revision-checked) |
| `POST /atlas/{map}/element/add|edit|delete` | Element mutations (admin); delete is refused while children exist |
| `POST /atlas/{map}/road/add|delete` | Road mutations (admin) |
| `POST /atlas/{map}/seed` | Seed import into an empty map (admin) |

Manifest commands: `atlas-maps`, `atlas-map`, `atlas-element` (read-only).

## Deferred

- Outward links from elements to task tracking (decision (a) defers them).
- Map JSON export or import as a file; the store document is authoritative.
- LOD is drawn client-side from the containment structure; the server only
  supplies counts and depth.
