#ifndef _Atlas_AtlasSite_h_
#define _Atlas_AtlasSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include "AtlasModel.h"

NAMESPACE_UPP

// Installs the Atlas routes on a host-created EngineSite. The site's own
// accounts decide access: any signed-in account reads, the "admin" role edits.
// Maps are stored one document per map version (key "<map>-<version>") in the
// site's JsonFileStore collection; every write is revision-checked.
void InstallAtlasSite(EngineSite& site);

// The Atlas page shell: navigation for the map family, breadcrumbs and the page body. section names
// the open roadmap page: versions | timeline | rules | tasks | checks; empty for the map pages.
void AtlasPage(WebRequest& r, const String& title, const String& body, const String& body_attrs = String(),
               const String& family = String(), const String& section = String());
String AtlasEsc(const String& value);
bool AtlasFamilyReadable(WebRequest& r, const String& family);

// Document key of a map version: "pkr" + "v0.0.1" -> "pkr-v0.0.1".
String AtlasDocKey(const String& map, const String& version);

// Store access for the host and the self-tests. Load fails with error set when
// the document is missing or unreadable. Save is revision-checked: an empty
// expected_revision creates-or-overwrites, a non-empty one must match.
bool AtlasLoadMap(const String& data_root, const String& key, AtlasMap& m, String& revision, String& error);
bool AtlasSaveMap(const String& data_root, const String& key, const AtlasMap& m,
                  const String& expected_revision, String& new_revision, String& error);

// Version lists and events, per map family ("pkr"). The head document is named by the list.
String AtlasNowIso();
bool AtlasLoadFamily(const String& data_root, const String& family, AtlasFamily& f, String& revision, String& error);
bool AtlasSaveFamily(const String& data_root, const String& family, const AtlasFamily& f,
                     const String& expected_revision, String& new_revision, String& error);
// Creates a family's list; refused, with error "already exists", when one is stored.
bool AtlasCreateFamily(const String& data_root, const String& family, const AtlasFamily& f,
                       String& new_revision, String& error);
bool AtlasLoadEvents(const String& data_root, const String& family, Vector<AtlasEvent>& out, String& error);
bool AtlasPutEvent(const String& data_root, const String& family, const AtlasEvent& e, String& error);

// Access for the API: the role check, the map's content policy, and per-element visibility.
bool AtlasCanEdit(WebRequest& r);
bool AtlasMapReadable(WebRequest& r, const String& head_key);
bool AtlasElementVisibleTo(WebRequest& r, const String& head_key, const String& element_id);

// Planning rules of a family: one document, revision-checked on its own.
bool AtlasLoadRules(const String& data_root, const String& family, AtlasRules& rules, String& revision, String& error);
bool AtlasSaveRules(const String& data_root, const String& family, const AtlasRules& rules,
                    const String& expected_revision, String& new_revision, String& error);

// The JSON API of the roadmap (AtlasApi.cpp); called by InstallAtlasSite.
void InstallAtlasApi(EngineSite& site);
// The roadmap pages of a family: versions, timeline, rules, tasks, checks (AtlasRoadmapPages.cpp).
void InstallAtlasRoadmapPages(EngineSite& site);

// The demo roadmap shipped with the server (AtlasDemo.cpp): written once, when its family is missing.
void AtlasInstallDemo(const String& data_root);
// The demo's model in memory: its family, head map, rules and events (for the self-tests).
void AtlasDemoModel(AtlasFamily& f, AtlasMap& head, AtlasRules& rules, Vector<AtlasEvent>& events);

// Self-tests: the pure model (validation, containment, seed parser and layout)
// and a store round trip under scratch_dir. Run from ManagerSkylark --test.
bool RunAtlasSelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
