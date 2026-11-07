#ifndef _WebAuth_WebAuth_h_
#define _WebAuth_WebAuth_h_

#include <Core/Core.h>
#include <Core/SSL/SSL.h>
#include <Skylark/Skylark.h>

// Each header below already wraps itself in NAMESPACE_UPP/END_UPP_NAMESPACE
// (so it stays independently includable, e.g. CompanyStore includes just
// WebAuthCrypto.h) -- do not wrap NAMESPACE_UPP around these includes here
// too, or Upp gets nested inside itself.
#include "WebAuthModel.h"
#include "WebAuthCrypto.h"
#include "WebAuthService.h"
#include "WebAuthViews.h"
#include "WebAuthSelfTest.h"

#endif
