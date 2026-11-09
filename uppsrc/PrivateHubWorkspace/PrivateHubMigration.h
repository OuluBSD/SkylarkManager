#ifndef _PrivateHubWorkspace_PrivateHubMigration_h_
#define _PrivateHubWorkspace_PrivateHubMigration_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Outcome of one legacy import. Clean() is true only when nothing was
// rejected, no store was corrupt, and no write failed.
struct PrivateHubLegacyReport {
	String project;
	int    issues = 0;            // tickets imported as issue records
	int    issue_comments = 0;
	int    wiki_pages = 0;
	int    discussion_topics = 0;
	int    discussion_posts = 0;
	int    release_files = 0;
	int    already_present = 0;   // records skipped because the migrated id exists
	int    rejected = 0;          // entries that failed validation and were not guessed at
	bool   failed = false;        // a write failed
	Vector<String> absent;        // store files that are not present
	Vector<String> corrupt;       // store files that are not readable JSON arrays
	Vector<String> notes;         // one line per rejected entry or failure

	bool Clean() const { return !failed && corrupt.IsEmpty() && rejected == 0; }
	String Summary() const;
};

// One-shot import of the legacy SourceForge local stores
//   <legacy_dir>/{tickets,wiki,discussion,files}_store.json
// into the PrivateHub site store rooted at data_root. The project is the
// legacy directory name. Each imported record carries data.provenance
// "migrated-from-legacy" and an envelope provenance_ref of
// "<store file>#<key>". Re-running is safe: existing migrated ids are skipped.
//
// The legacy files are only read. A data_root inside legacy_dir is refused.
// Returns false, with a note, when the import cannot start. Returns true when
// it ran, even if the report is not Clean().
bool PrivateHubMigrateSourceForgeLocal(const String& legacy_dir, const String& data_root,
                                       PrivateHubLegacyReport& rep);

END_UPP_NAMESPACE

#endif
