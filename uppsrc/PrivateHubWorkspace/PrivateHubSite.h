#ifndef _PrivateHubWorkspace_PrivateHubSite_h_
#define _PrivateHubWorkspace_PrivateHubSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include "PrivateHubWorkspace.h"

NAMESPACE_UPP

// Installs the PrivateHub routes on a host-created EngineSite. The backend is
// file-scoped (one PrivateHub site per process, mirroring the engine singleton)
// and must outlive the site.
void InstallPrivateHubSite(EngineSite& site, PrivateHubWorkspaceBackend& backend);

END_UPP_NAMESPACE

#endif
