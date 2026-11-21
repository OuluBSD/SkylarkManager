#include "ScienceApp.h"
#include "ScienceSite.h"
#include "ScienceService.h"

NAMESPACE_UPP

Vector<String> ScienceApp::Roles() const
{
	Vector<String> r;
	r.Add(SCIENCE_AUTHOR_ROLE);
	return r;
}

bool ScienceApp::Install(EngineSite& site, WebAppContext& ctx, String& error)
{
	String root = ctx.Setting("corpus");
	if(root.IsEmpty()) {
		error = "science: no corpus setting";
		return false;
	}
	if(!ctx.grants) {
		error = "science: no shared grant store";
		return false;
	}
	corpus.Create(root, root);
	InstallScienceSite(site, *corpus, ctx.grants);
	return true;
}

bool ScienceApp::RegisterBroker(WebAppContext& ctx, String& error)
{
	if(!corpus) {
		error = "science broker: the corpus is not installed";
		return false;
	}
	ctx.broker->Register(Id(), new ScienceService(*corpus));
	return true;
}

END_UPP_NAMESPACE
