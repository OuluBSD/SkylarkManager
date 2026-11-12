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

// Document key of a map version: "pkr" + "v0.0.1" -> "pkr-v0.0.1".
String AtlasDocKey(const String& map, const String& version);

// Store access for the host and the self-tests. Load fails with error set when
// the document is missing or unreadable. Save is revision-checked: an empty
// expected_revision creates-or-overwrites, a non-empty one must match.
bool AtlasLoadMap(const String& data_root, const String& key, AtlasMap& m, String& revision, String& error);
bool AtlasSaveMap(const String& data_root, const String& key, const AtlasMap& m,
                  const String& expected_revision, String& new_revision, String& error);

// Self-tests: the pure model (validation, containment, seed parser and layout)
// and a store round trip under scratch_dir. Run from ManagerSkylark --test.
bool RunAtlasSelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
