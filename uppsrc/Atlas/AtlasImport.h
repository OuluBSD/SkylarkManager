#ifndef _Atlas_AtlasImport_h_
#define _Atlas_AtlasImport_h_

#include "AtlasSite.h"

NAMESPACE_UPP

// Projects are the server's registry of repositories whose Atlas data is kept in them. The file is projects.json in
// the Atlas data root (site directory engine/atlas, next to its data/ store). A project's name is its map family.
//
//   {"projects": [{"name": "project", "repo": "/home/me/Project", "store": "atlas/project",
//                  "title": "Project", "level": "public", "editors": ["alice"]}]}
//
// store is a directory in the repository, relative to it, that holds the family's records in the store layout
// (store/data/families, maps, rules and events). Atlas reads and writes those files directly: the repository is the
// source of truth, and there is no import. level is the visibility of the project's maps (public, protected or
// private); editors may edit the project's maps without the admin role. The file is read on each use, so edits take
// effect without a restart.
// A named root of a project's source code: the files a roadmap file refers to are read below it. path is absolute.
struct AtlasSource : Moveable<AtlasSource> {
	String alias;
	String path;
};

struct AtlasProject : Moveable<AtlasProject> {
	String             name;
	String             repo;
	String             store;
	String             title;
	String             level;
	Vector<String>     editors;
	Vector<AtlasSource> sources;
};

bool AtlasLoadProjects(const String& data_root, Vector<AtlasProject>& out, String& error);
// The absolute root of a project's source alias, or "" when the project or the alias is not registered.
String AtlasSourceRoot(const String& data_root, const String& family, const String& alias);
// Index of the named project in all, or -1.
int  AtlasFindProject(const Vector<AtlasProject>& all, const String& name);

// True when username is listed as an editor of the family's project. Fails closed when the file is unreadable.
bool AtlasProjectEditor(const String& data_root, const String& family, const String& username);

// The text of a repository file, read live from a source root. AtlasResolveSource refuses a path that leaves the root
// (also through a symlink), a path that is not a code or document file, and a file over the size cap.
bool AtlasResolveSource(const String& root, const String& rel, String& abs_path, String& error);
bool AtlasReadSourceLines(const String& root, const String& rel, Vector<String>& lines, String& error);
// The record as a viewer may see it: spans and notes for every reader, the text of the edits for an editor only.
AtlasFile AtlasFileForViewer(const AtlasFile& f, bool editor);
// The text of a file at version vid: its base lines with the edits of that version and of those before it applied.
bool AtlasFileTextAt(const AtlasFile& f, const Vector<String>& base, const AtlasFamily& fam, const String& vid,
                     Vector<String>& out, String& error);

// The result of a map step: status is the HTTP status to answer with (200 when done).
struct AtlasImportResult {
	int    status = 200;
	String error;
	String key;
	String revision;
	int    elements = 0;
	int    roads = 0;
	int    events = 0;

	bool   Ok() const { return status == 200; }
};

// The directory whose data/ holds a family's records: a registered project's store (repository and store path), or
// the data root when no project of that name has a store.
String AtlasStoreRoot(const String& data_root, const String& family);

// An empty map document at m.map and m.version. level, when not empty, is stored before the document exists, so a
// private map is never readable as public, not even empty.
AtlasImportResult AtlasCreateMap(const String& data_root, const AtlasMap& m, const String& level);

// Fills an existing map document from a seed text. The map must be empty (AtlasParseSeed refuses otherwise).
AtlasImportResult AtlasSeedMap(const String& data_root, const String& key, const String& seed);

END_UPP_NAMESPACE

#endif
