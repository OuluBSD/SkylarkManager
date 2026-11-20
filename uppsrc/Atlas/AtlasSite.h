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

// Key of a family's map record: the family name. The version is data in the record, not part of the key.
String AtlasMapKey(const String& map);
// The family of a map key (name-vX.Y.Z): the part before the last "-v".
String AtlasFamilyOfKey(const String& key);
// The keys of every map in the data root and in each registered project's store.
void AtlasListMapKeys(const String& data_root, Vector<String>& ids);

// Store access for the host and the self-tests. Load fails with error set when
// the document is missing or unreadable. Save is revision-checked: an empty
// expected_revision creates-or-overwrites, a non-empty one must match.
bool AtlasLoadMap(const String& data_root, const String& key, AtlasMap& m, String& revision, String& error);
bool AtlasSaveMap(const String& data_root, const String& key, const AtlasMap& m,
                  const String& expected_revision, String& new_revision, String& error);

// Version lists and events, per map family ("demo"). The head document is named by the list.
String AtlasNowIso();
bool AtlasLoadFamily(const String& data_root, const String& family, AtlasFamily& f, String& revision, String& error);
bool AtlasSaveFamily(const String& data_root, const String& family, const AtlasFamily& f,
                     const String& expected_revision, String& new_revision, String& error);
// Creates a family's list; refused, with error "already exists", when one is stored.
bool AtlasCreateFamily(const String& data_root, const String& family, const AtlasFamily& f,
                       String& new_revision, String& error);
bool AtlasLoadEvents(const String& data_root, const String& family, Vector<AtlasEvent>& out, String& error);
bool AtlasPutEvent(const String& data_root, const String& family, const AtlasEvent& e, String& error);
bool AtlasPutEventGuarded(const String& data_root, const String& family, const AtlasEvent& e,
                          const AtlasMap& before, const AtlasMap& after, const Vector<AtlasEvent>& batch,
                          const Vector<AtlasClaim>& claims, const String& actor, Time now, String& error);
// Claims are separate revisioned records, keyed by family, element id and target vid; they do not append events.
bool AtlasLoadClaim(const String& data_root, const String& family, const String& element_id, const String& target_vid,
                    AtlasClaim& out, String& error);
bool AtlasLoadClaims(const String& data_root, const String& family, Vector<AtlasClaim>& out, String& error);
bool AtlasSaveClaim(const String& data_root, const String& family, const AtlasClaim& claim,
                    const String& expected_revision, String& new_revision, String& error);
bool AtlasAcquireClaim(const String& data_root, const String& family, const String& element_id, const String& target_vid,
                       const String& owner, int lease_seconds, AtlasClaim& out, String& error);
bool AtlasReleaseClaim(const String& data_root, const String& family, const String& element_id, const String& target_vid,
                       const String& owner, String& error);

// Access for the API: the role check, the map's content policy, and per-element visibility.
bool AtlasCanEdit(WebRequest& r);
// Admin, or an editor of the family's project (AtlasImport.h). Use this for the maps and their parts.
bool AtlasCanEditFamily(WebRequest& r, const String& family);
bool AtlasMapReadable(WebRequest& r, const String& head_key);
bool AtlasElementVisibleTo(WebRequest& r, const String& head_key, const String& element_id);

// Planning rules of a family: one document, revision-checked on its own.
bool AtlasLoadRules(const String& data_root, const String& family, AtlasRules& rules, String& revision, String& error);
bool AtlasSaveRules(const String& data_root, const String& family, const AtlasRules& rules,
                    const String& expected_revision, String& new_revision, String& error);
// Merge bases are immutable snapshots associated with one source work ID.
bool AtlasLoadMergeBase(const String& data_root, const String& family, const String& source_work_id,
                        AtlasMergeBase& out, String& error);
bool AtlasSaveMergeBase(const String& data_root, const String& family, const AtlasMergeBase& base,
                        const String& expected_revision, String& new_revision, String& error);
bool AtlasCaptureMergeBase(const String& data_root, const String& family, const String& source_work_id,
                           const String& actor, AtlasMergeBase& out, String& error);
// The roadmap's file records of a family, one per file, stored with the family's other records.
bool AtlasLoadFile(const String& data_root, const String& family, const String& file_id, AtlasFile& f,
                   String& revision, String& error);
bool AtlasSaveFile(const String& data_root, const String& family, const AtlasFile& f,
                   const String& expected_revision, String& new_revision, String& error);
bool AtlasListFiles(const String& data_root, const String& family, Vector<AtlasFile>& out, String& error);
// The warning comments of a family, stored with its other records. Saving replaces the comment on that warning.
bool AtlasLoadAcks(const String& data_root, const String& family, Vector<AtlasWarningAck>& out, String& error);
bool AtlasSaveAck(const String& data_root, const String& family, const AtlasWarningAck& a, String& error);
bool AtlasDeleteAck(const String& data_root, const String& family, const String& warning_id, String& error);

// The JSON API of the roadmap (AtlasApi.cpp); called by InstallAtlasSite.
void InstallAtlasApi(EngineSite& site);
// The roadmap pages of a family: versions, timeline, rules, tasks, checks (AtlasRoadmapPages.cpp).
void InstallAtlasRoadmapPages(EngineSite& site);

// The page script for the operator forms (AtlasRoadmapPages.cpp); the map page embeds it too.
String AtlasRoadmapScript();

// The subjects at one version: a split view with sortable columns, each subject's history and, when edit is
// set, the operator forms that post to the API (AtlasRoadmapPages.cpp).
String AtlasSubjectsView(WebRequest& r, const AtlasMap& head, const AtlasFamily& f, const Vector<AtlasEvent>& events,
                         const Vector<AtlasSubjectState>& states, const Vector<AtlasRoadmapMessage>& messages,
                         const String& vid, bool edit);

// The demo roadmap shipped with the server (AtlasDemo.cpp): written once, when its family is missing.
void AtlasInstallDemo(const String& data_root);
// The demo's model in memory: its family, head map, rules and events (for the self-tests).
void AtlasDemoModel(AtlasFamily& f, AtlasMap& head, AtlasRules& rules, Vector<AtlasEvent>& events);

// Self-tests: the pure model (validation, containment, seed parser and layout)
// and a store round trip under scratch_dir. Run from ManagerSkylark --test.
bool RunAtlasSelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
