#ifndef _TaskFoundry_TaskFoundrySite_h_
#define _TaskFoundry_TaskFoundrySite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include "TaskFoundryService.h"

NAMESPACE_UPP

// Installs the TaskFoundry routes on a host-created EngineSite: board, candidate
// and proposal pages, evidence, validation, submission and status. The service
// is file-scoped (one TaskFoundry site per process) and must outlive the site.
void InstallTaskFoundrySite(EngineSite& site, TaskFoundryService& service);

END_UPP_NAMESPACE

#endif
