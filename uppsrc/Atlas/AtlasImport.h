#ifndef _Atlas_AtlasImport_h_
#define _Atlas_AtlasImport_h_

#include "AtlasSite.h"

NAMESPACE_UPP

// Projects are the server's registry of repositories whose roadmap Atlas keeps. The file is projects.json in the
// Atlas data root (site directory engine/atlas, next to its data/ store). A project's name is its map family; its seed is a path relative to the repository root.
//
//   {"projects": [{"name": "pkr", "repo": "/home/me/BrownBear", "seed": "atlas/pkr/goal-map.seed",
//                  "version": "v1.0.0", "title": "PKR goal map", "level": "private", "editors": ["alice"]}]}
//
// roadmap, when given, is a format 2 JSON file (scripts/roadmap.py in the BrownBear repository writes it): versions
// with their contracts, the rules (atoms, actions, constraints) and the events that place each element. The import
// replays all of it and refuses on any finding, so a family that exists has been checked clean. level is the
// visibility of the imported map (public, protected or private); editors may edit the project's maps without the
// admin role. The file is read on each use, so edits take effect without a restart.
struct AtlasProject : Moveable<AtlasProject> {
	String         name;
	String         repo;
	String         seed;
	String         version;
	String         title;
	String         level;
	String         roadmap;   // optional: the roadmap file, relative to the repo (see AtlasImport.cpp)
	Vector<String> editors;
};

bool AtlasLoadProjects(const String& data_root, Vector<AtlasProject>& out, String& error);
// Index of the named project in all, or -1.
int  AtlasFindProject(const Vector<AtlasProject>& all, const String& name);

// True when username is listed as an editor of the family's project. Fails closed when the file is unreadable.
bool AtlasProjectEditor(const String& data_root, const String& family, const String& username);

// The result of an import step: status is the HTTP status to answer with (200 when done).
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

// An empty map document at m.map and m.version. level, when not empty, is stored before the document exists, so a
// private map is never readable as public, not even empty.
AtlasImportResult AtlasCreateMap(const String& data_root, const AtlasMap& m, const String& level);

// Fills an existing map document from a seed text. The map must be empty (AtlasParseSeed refuses otherwise).
AtlasImportResult AtlasSeedMap(const String& data_root, const String& key, const String& seed);

// Creates, seeds and starts the family of a project from its seed file. Refuses when the family already exists.
AtlasImportResult AtlasImportProject(const String& data_root, const AtlasProject& p);

END_UPP_NAMESPACE

#endif
