#ifndef _WebShowcase_WebShowcase_h_
#define _WebShowcase_WebShowcase_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <WebFramework/WebFramework.h>

NAMESPACE_UPP

class WebShowcaseApp : public SkylarkApp {
public:
	WebShowcaseApp();
	void SetPort(int p) { port = p; }
	int  GetPort() const { return port; }
};

NavigationModel BuildShowcaseNavigation();

END_UPP_NAMESPACE

#endif
