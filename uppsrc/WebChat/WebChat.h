#ifndef _WebChat_WebChat_h_
#define _WebChat_WebChat_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <SweAgent/SweAgent.h>

#include "Session.h"
#include "Streaming.h"
#include "ChatView.h"
#include "ChatRoutes.h"

NAMESPACE_UPP

using WebChatAuthorizationCallback = bool (*)(Http&);

void SetWebChatAuthorizationCallback(WebChatAuthorizationCallback callback);

END_UPP_NAMESPACE

#endif
