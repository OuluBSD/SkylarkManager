#ifndef _Channels_ChannelsSite_h_
#define _Channels_ChannelsSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include "AgentIpcClient.h"

NAMESPACE_UPP

// Installs the read-only Channels routes on a host-created EngineSite. The
// config is copied internally; the site owns its cache. The site never writes
// to the node (no `say` in v1).
void InstallChannelsSite(EngineSite& site, const AgentIpcConfig& cfg);

END_UPP_NAMESPACE

#endif
