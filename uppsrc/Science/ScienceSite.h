#ifndef _Science_ScienceSite_h_
#define _Science_ScienceSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include "ScienceModel.h"

NAMESPACE_UPP

// Installs the Science read routes on a host-created EngineSite. The corpus
// reader is file-scoped (one Science site per process, mirroring the engine
// singleton).
void InstallScienceSite(EngineSite& site, ScienceCorpus& corpus);

END_UPP_NAMESPACE

#endif
