#ifndef _reference_ManagerSkylark_SfLegacyMigrationCli_h_
#define _reference_ManagerSkylark_SfLegacyMigrationCli_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ManagerSkylark --migrate-sf-local <legacy-dir> <privatehub-data-root>
// args holds the two paths. Returns the process exit code:
//   0 import ran and was clean; 1 import ran but was not clean
//   (rejected entries, corrupt stores, or a failed write); 2 usage or refusal.
int RunMigrateSfLocalCommand(const Vector<String>& args);

// Synthetic checks for the migration, run from the --test suite. scratch_dir
// is a temp directory the checks may create and remove.
bool RunSfLegacyMigrationSelfTests(const String& scratch_dir);

END_UPP_NAMESPACE

#endif
