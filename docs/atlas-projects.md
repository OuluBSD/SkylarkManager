# Atlas projects: configuration and the repository store

Atlas shows the map of each registered project. A project's map lives in its repository: the repository's files are
the source of truth, and Atlas reads and writes them directly. There is no import. The server reads the registry
from one file, so adding a repository never touches the Atlas code.

## Where the registry lives

The registry is `projects.json` in the Atlas data root, which is the `engine/atlas` directory under the ManagerSkylark
site directory:

| Server | Registry file |
|---|---|
| default site directory (`~/.config/managerskylark`) | `~/.config/managerskylark/engine/atlas/projects.json` |
| `--site-dir <dir>` | `<dir>/engine/atlas/projects.json` |

The file is operator configuration: it names repository paths and editor accounts, so it lives in the user's
configuration directory and never in a repository. It is read on each use, so edits take effect without a restart.
If the file is missing, there are no projects.

## Format

    {"projects": [
      {"name": "brownbear", "repo": "/srv/BrownBear", "store": "atlas/brownbear/store",
       "title": "BrownBear", "level": "public", "editors": ["alice"]}
    ]}

| Field | Required | Meaning |
|---|---|---|
| `name` | yes | The map family, and the first part of its map keys (`name-version`). A map id: letters, digits, `_` or `-`, at most 64 characters. Unique in the file. |
| `repo` | yes | Absolute path of the repository root. |
| `store` | no | Directory in the repository, relative to it, that holds the family's records: `<store>/data/families`, `maps`, `rules` and `events`. `..` and absolute paths are refused. Without a store the project has no records. |
| `title` | no | The project's title. Defaults to `name`. |
| `level` | no | `public`, `protected` or `private`. Default `private`. The level of a map is set when it is created in Atlas. |
| `editors` | no | Account names that may edit this project's maps without the admin role. Matched without case. |

A bad entry fails the whole file, and the project routes answer with the reason (for example `projects file: project
brownbear: store must be a relative path inside the repository`).

## The store

The store uses the same layout as Atlas's data root: one JSON file per record, with an envelope and the data. It is
written by Atlas, so a family changed in Atlas is a change to the files in the repository, and it shows up in
`git status` of that repository. Commit it there like any other file. Each event is its own file, so a change to a
map shows as a small diff.

    atlas/brownbear/store/data/families/brownbear.json
    atlas/brownbear/store/data/maps/brownbear-v1.0.0.json
    atlas/brownbear/store/data/rules/brownbear.json
    atlas/brownbear/store/data/events/brownbear.ev-0001.json  (one per event)

A family with no project store is kept in the data root, as before. Atlas's map list shows the maps of both.

## Who can do what

- **Admins** edit every map, create maps, and set content policy.
- **Editors** (listed in `editors`) edit the maps of their project. They do not create other maps or change policy.
- **Visibility**: a map's level decides who else reads it. Editors read their project's maps whatever the level. A
  `private` map is hidden from everyone else; a `public` map is shown to anonymous visitors too.
- Account roles are set with `ManagerSkylark --set-atlas-user <name>`. Listing an account as an editor does not create
  it; create the account first.

## Adding a repository

1. **Create the store in the repository.** Make the directory `<store>/data/` in the repository. A map created for the
   project (admins: `POST api/atlas/create`, with the project's family as the map name) is written into that store.
2. **Register the project.** Add an entry to `projects.json` as above, with an absolute `repo` path and the `store`
   directory. Set `level` and `editors`.
3. **Look at it.** Open `/atlas/<name>-<version>` on the Atlas site. The Projects table on `/atlas` shows each project
   you may edit, with its head map once its store has a family.

## Troubleshooting

| Message | Cause |
|---|---|
| `no project named <name>` | No entry with that `name` in `projects.json`. |
| `projects file: ...` | The JSON is malformed or an entry is invalid; the message names the field. |
| `projects file: project <name> is listed twice` | Two entries share a name. |
| `editing needs the admin role, or to be an editor of this map's project` (403) | The account is neither an admin nor listed under `editors`. |
| `no family in <store>` (Projects table) | The project's store has no family yet. |

## Demo setup

For a demo, register two projects: one public (so anonymous visitors see its map) and one private (its editors see
it, others do not). Each keeps its store in its own repository. Show the Atlas site signed out (public maps only),
signed in as an editor (both maps), and signed in as an admin.
