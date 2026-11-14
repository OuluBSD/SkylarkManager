# Atlas projects: configuration and adding a repository

Atlas shows the roadmap of each registered project as a map. A project is a
repository whose topology is kept as a seed file inside that repository. The
server reads the registry from one file, so adding a repository never touches
the Atlas code.

## Where the registry lives

The registry is `projects.json` in the Atlas data root, which is the
`engine/atlas` directory under the ManagerSkylark site directory:

| Server | Registry file |
|---|---|
| default site directory (`~/.config/managerskylark`) | `~/.config/managerskylark/engine/atlas/projects.json` |
| `--site-dir <dir>` | `<dir>/engine/atlas/projects.json` |

The file is operator configuration: it names repository paths and editor
accounts, so it lives in the user's configuration directory and never in a
repository. It is read on each use, so edits take effect without a restart. If
the file is missing, there are no projects.

## Format

    {"projects": [
      {"name": "brownbear", "repo": "/srv/BrownBear", "seed": "atlas/pkr/goal-map.seed",
       "roadmap": "atlas/brownbear/roadmap.json",
       "version": "v1.0.0", "title": "BrownBear", "level": "private",
       "editors": ["alice"]},
      {"name": "managerskylark", "repo": "/home/me/OuluBSD/SkylarkManager",
       "seed": "atlas/managerskylark/map.seed", "version": "v1.0.0",
       "title": "ManagerSkylark", "level": "public", "editors": ["alice"]}
    ]}

| Field | Required | Meaning |
|---|---|---|
| `name` | yes | The map family, and the first part of its map keys (`name-version`). A map id: letters, digits, `_` or `-`, at most 64 characters. Unique in the file. |
| `repo` | yes | Absolute path of the repository root. |
| `seed` | yes | Path of the seed file, relative to `repo`. `..` and absolute paths are refused. |
| `version` | yes | The version of the imported map, `v<a.b.c>`. Later versions are added in Atlas. |
| `title` | no | The map's title. Defaults to `name`. |
| `level` | no | `public`, `protected` or `private`. Default `private`. Applied when the import creates the map. |
| `roadmap` | no | A JSON file, relative to `repo`, that gives the family's versions and each element's first version. Without it the import makes one version, the head. `..` and absolute paths are refused. See below. |
| `editors` | no | Account names that may edit this project's maps without the admin role. Matched without case. |

A bad entry fails the whole file, and the project routes answer with the
reason (for example `project brownbear: seed must be a relative path inside the
repository`).

## Who can do what

- **Admins** edit every map, create maps, run any import, and set content
  policy.
- **Editors** (listed in `editors`) edit the maps of their project and run
  its import. They do not create other maps or change policy.
- **Visibility**: a map's level decides who else reads it. Editors read their
  project's maps whatever the level. A `private` map is hidden from everyone
  else; a `public` map is shown to anonymous visitors too.
- Account roles are set with `ManagerSkylark --set-atlas-user <name>`.
  Listing an account as an editor does not create it; create the account
  first.

## Adding a repository by hand

1. **Write the seed in the repository.** Put it under `atlas/<name>/` (any
   file name). Format, one element or edge per line:
   - Containment: two spaces per level, then `Name | kind | status | note`.
     `kind` is `block` or `place` (defaults to `block` when the element has
     children, else `place`). `status` is one of `hot`, `active`, `planned`,
     `not-ready`, `future`, `settled`, `trouble` (default `planned`).
   - Edges use names: `A -> B: label` (progress), `A => B: label`
     (dependency), `A ~> B: label` (communication). The label is optional.
     Both ends must sit on the same plane (the same parent block); an edge
     across blocks is refused, and the import reports which road it was.
   - `#` starts a comment. Names are unique within the seed and may not
     contain `:` or `|`.
   - The seed in `atlas/managerskylark/map.seed` is a working example.
2. **Register the project.** Add an entry to `projects.json` as above. Set
   `level` and `editors`. Leave the `repo` path absolute.
   With a `roadmap`, the seed is the head topology and the roadmap is the history:

       {"versions": ["0.0.1", "0.0.2", "1.0.0"],
        "elements": {"MainComputer": {"from": "0.0.1", "note": "desktop app root"},
                     "Leaf": {"from": "0.0.2"}}}

   Every element of the seed must be named, with `from`, the version it first
   appears in. `versions` ascend, and the last one must be the project's
   `version`. A block may not start after an element it contains. `note` is
   optional and is kept as a note event. The import checks all of this before it
   writes anything, so a bad roadmap leaves no family and no map behind.
3. **Import it.** A signed-in admin or editor runs, either from the Projects table
   on `/atlas` (an Import button, shown only to the people who may import the
   project) or with the account's session
   (`skylarkctl` login is shown in `skylarkctl --help`):

       skylarkctl --server http://<host>:<main-port> --site atlas=http://<host>:<atlas-port> \
         --session <jar> --password-env ATLAS_PASSWORD atlas atlas-project-import project=<name>

   The answer gives the element and road counts. Compare them with the seed:
   elements are the lines with `|`, roads are the lines with an arrow.
4. **Look at it.** Open `/atlas/<name>-<version>` on the Atlas site. The
   Versions page lists the family's one version, which is the head.

The import is one-shot. It refuses with 409 when the family already exists,
so a retry never leaves a half-made family. To change the map afterwards, edit
it in Atlas (editors) rather than re-importing. To re-seed from scratch, the
family and its maps must be removed first, which only an operator can do.

## Troubleshooting

| Message | Cause |
|---|---|
| `no project named <name>` | No entry with that `name` in `projects.json`. |
| `seed not found: <path>` | The seed path is wrong, or the repository is not checked out at `repo`. |
| `the family <name> already exists` (409) | The project was imported already. |
| `editing needs the admin role, or to be an editor of this map's project` (403) | The account is neither an admin nor listed under `editors`. |
| `seed import needs an empty map` | The map already has elements; import starts from a new version. |
| `projects file: ...` | The JSON is malformed or an entry is invalid; the message names the field. |
| `roadmap <path>: versions must ascend ...` | The roadmap's versions are out of order or repeat. |
| `roadmap <path>: element <name> is not in the goal map` | The roadmap names an element the seed does not have. |
| `roadmap <path>: element <name> has no entry` | An element of the seed is missing from the roadmap. |
| `roadmap <path>: block <name> starts after its element <name>` | A block's `from` is later than one of its elements. |

## Demo setup

For a demo, register two projects: `managerskylark` (public, so anonymous
visitors see the map) and one private project such as `brownbear` (its editors see
it, others do not). The `managerskylark` seed in this repository is the demo
map. Import each once, then show the Atlas site signed out (public map only),
signed in as an editor (both maps), and signed in as an admin.
