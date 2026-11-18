#ifndef _MultiAtlas_MultiAtlasReplayLoader_h_
#define _MultiAtlas_MultiAtlasReplayLoader_h_

#include "MultiAtlasReplay.h"
#include "MultiAtlasWarningLedger.h"

NAMESPACE_UPP

// Loads the complete registry-selected ensemble snapshot (families, maps,
// rules, events and scoped acknowledgments) without an HTTP dependency.
bool MultiAtlasLoadReplayInput(const MultiAtlasEnsembleDef& ensemble,
	                           MultiAtlasReplayInput& input,
	                           Vector<MultiAtlasWarningAck>& acknowledgments,
	                           String& error);

END_UPP_NAMESPACE

#endif
