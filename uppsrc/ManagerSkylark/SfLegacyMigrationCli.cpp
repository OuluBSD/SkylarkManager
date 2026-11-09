#include "SfLegacyMigrationCli.h"
#include <PrivateHubWorkspace/PrivateHubMigration.h>

NAMESPACE_UPP

int RunMigrateSfLocalCommand(const Vector<String>& args)
{
	if(args.GetCount() != 2) {
		Cerr() << "usage: ManagerSkylark --migrate-sf-local <legacy-dir> <privatehub-data-root>\n";
		return 2;
	}
	PrivateHubLegacyReport rep;
	if(!PrivateHubMigrateSourceForgeLocal(args[0], args[1], rep)) {
		Cerr() << "migration refused\n";
		for(const String& n : rep.notes)
			Cerr() << "  " << n << "\n";
		return 2;
	}
	Cout() << rep.Summary();
	return rep.Clean() ? 0 : 1;
}

END_UPP_NAMESPACE
