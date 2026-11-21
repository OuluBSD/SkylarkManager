#ifndef _Science_ScienceApp_h_
#define _Science_ScienceApp_h_

#include <WebAppHost/WebAppHost.h>
#include "ScienceModel.h"

NAMESPACE_UPP

// The Science webapp for WebServer. Registered as "science" by ScienceApp.icpp. Install opens the
// read-only corpus over the "corpus" setting (article sidecars are written beside the records);
// RegisterBroker registers the Science broker service. The site is InstallScienceSite (ScienceSite.h).
class ScienceApp : public WebApp {
public:
	String         Id() const override { return "science"; }
	String         Title() const override { return "Science"; }
	int            DefaultPort() const override { return 18212; }
	Vector<String> Roles() const override;
	bool           Install(EngineSite& site, WebAppContext& ctx, String& error) override;
	bool           RegisterBroker(WebAppContext& ctx, String& error) override;

private:
	One<ScienceCorpus> corpus;
};

END_UPP_NAMESPACE

#endif
