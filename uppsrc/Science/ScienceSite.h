#ifndef _Science_ScienceSite_h_
#define _Science_ScienceSite_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include <WebAccounts/WebAccountsGrants.h>
#include "ScienceModel.h"

NAMESPACE_UPP

// Role that lets a site user author structured articles.
#define SCIENCE_AUTHOR_ROLE  "science-author"
// Grant scope that lets a caller author structured articles.
#define SCIENCE_AUTHOR_SCOPE "science.author"
// Request header carrying a meta-account grant id (bearer-style).
#define SCIENCE_GRANT_HEADER "x-science-grant"

// Installs the Science routes on a host-created EngineSite. The corpus
// reader and grant store are file-scoped (one Science site per process,
// mirroring the engine singleton). grants may be NULL: then only the
// session-role path authorizes authoring.
void InstallScienceSite(EngineSite& site, ScienceCorpus& corpus, WebGrants *grants = NULL);

END_UPP_NAMESPACE

#endif
