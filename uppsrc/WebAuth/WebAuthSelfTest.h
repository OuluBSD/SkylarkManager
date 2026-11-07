#ifndef _WebAuth_WebAuthSelfTest_h_
#define _WebAuth_WebAuthSelfTest_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Standalone round-trip checks for WebAuthCrypto/WebAuthService, independent
// of any embedding app's storage (uses an in-memory mock IWebAuthStore).
// Intended to be called from a host app's own --test suite.
bool WebAuthSelfTest(String& error);

END_UPP_NAMESPACE

#endif
