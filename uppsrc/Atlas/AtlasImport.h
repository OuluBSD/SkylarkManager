#ifndef _Atlas_AtlasImport_h_
#define _Atlas_AtlasImport_h_

#include "AtlasSite.h"

NAMESPACE_UPP

// Projects are the server's registry of repositories whose Atlas data is kept in them. The file is projects.json in
// the Atlas data root (site directory engine/atlas, next to its data/ store). A project's name is its map family.
//
//   {"projects": [{"name": "brownbear", "repo": "/home/me/BrownBear", "store": "atlas/brownbear",
//                  "title": "BrownBear", "level": "public", "editors": ["alice"]}]}
//
// store is a directory in the repository, relative to it, that holds the family's records in the store layout
// (store/data/families, maps, rules and events). Atlas reads and writes those files directly: the repository is the
// source of truth, and there is no import. level is the visibility of the project's maps (public, protected or
// private); editors may edit the project's maps without the admin role. The file is read on each use, so edits take
// effect without a restart.
struct AtlasProject : Moveable<AtlasProject> {
	String         name;
	String         repo;
	String         store;
	String         title;
	String         level;
	Vector<String> editors;
};

bool AtlasLoadProjects(const String& data_root, Vector<AtlasProject>& out, String& error);
// Index of the named project in all, or -1.
int  AtlasFindProject(const Vector<AtlasProject>& all, const String& name);

// True when username is listed as an editor of the family's project. Fails closed when the file is unreadable.
bool AtlasProjectEditor(const String& data_root, const String& family, const String& username);

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
