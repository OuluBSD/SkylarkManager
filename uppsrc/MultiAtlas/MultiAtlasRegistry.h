#ifndef _MultiAtlas_MultiAtlasRegistry_h_
#define _MultiAtlas_MultiAtlasRegistry_h_

#include "MultiAtlasModel.h"

NAMESPACE_UPP

// Missing configuration is valid on a fresh installation: returns true with
// an empty output vector. Malformed or unsafe configured entries return false.
bool MultiAtlasLoadRegistry(const String& site_dir, Vector<MultiAtlasEnsembleDef>& out, String& err);
bool MultiAtlasFindEnsemble(const String& site_dir, const String& name,
	                        MultiAtlasEnsembleDef& out, String& err);

END_UPP_NAMESPACE

#endif
