#ifndef _TaskFoundry_TaskFoundryApp_h_
#define _TaskFoundry_TaskFoundryApp_h_

#include <WebAppHost/WebAppHost.h>
#include "TaskFoundryService.h"

NAMESPACE_UPP

// The TaskFoundry webapp for WebServer. Registered as "taskfoundry" by TaskFoundryApp.icpp. Install
// creates the service over the broker and the site root and calls InstallTaskFoundrySite with the
// shared grant store (the consent grants of MetaAccount). Its first role is "member".
class TaskFoundryApp : public WebApp {
public:
	String         Id() const override { return "taskfoundry"; }
	String         Title() const override { return "TaskFoundry"; }
	int            DefaultPort() const override { return 18213; }
	Vector<String> Roles() const override { Vector<String> r; r.Add("member"); return r; }
	bool           Install(EngineSite& site, WebAppContext& ctx, String& error) override;

private:
	One<TaskFoundryService> service;
};

END_UPP_NAMESPACE

#endif
