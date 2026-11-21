#include "ChannelsApp.h"

NAMESPACE_UPP

// The Channels adapter refuses an install with no AgentIPC config and a site root that is not a directory,
// installs on a valid root, has no broker service to register, and keeps the content policy routes.
// The valid install uses a throwaway site id, so its routes and manifest entries stay out of the real site.

static bool ChannelsCheck(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

bool RunChannelsAppSelfTests(const String& scratch_dir)
{
	RealizeDirectory(scratch_dir);
	String good_dir = AppendFileName(scratch_dir, "good");
	String good_root = AppendFileName(AppendFileName(good_dir, "engine"), "channels");
	RealizeDirectory(good_root);
	String bad_dir = AppendFileName(scratch_dir, "bad");
	RealizeDirectory(bad_dir);

	AgentIpcConfig ipc;
	ipc.direct = "127.0.0.1:1";
	ipc.client_argv.Add("/nonexistent/agentipc");
	ipc.records_path = AppendFileName(scratch_dir, "records.jsonl");

	EngineSite& site = TheWebEngine().AddSite("channels-selftest", "Channels self-test",
		"http://127.0.0.1:1", good_root);
	ChannelsApp app;
	String error;
	bool ok = true;

	WebAppContext no_ipc;
	no_ipc.site_dir = good_dir;
	ok &= ChannelsCheck(!app.Install(site, no_ipc, error) && error.Find("no AgentIPC config") >= 0,
	                    "channels: an Install without the AgentIPC config is refused (" + error + ")");

	error.Clear();
	WebAppContext bad;
	bad.site_dir = bad_dir;
	bad.agentipc = &ipc;
	ok &= ChannelsCheck(!app.Install(site, bad, error) && error.Find("not a directory") >= 0,
	                    "channels: an Install on a root that is not a directory is refused (" + error + ")");

	error.Clear();
	WebAppContext ctx;
	ctx.site_dir = good_dir;
	ctx.agentipc = &ipc;
	ok &= ChannelsCheck(app.Install(site, ctx, error), "channels: an Install on a valid root succeeds (" + error + ")");
	ok &= ChannelsCheck(app.RegisterBroker(ctx, error),
	                    "channels: the broker step succeeds (the site registers no broker service)");
	ok &= ChannelsCheck(app.ContentPolicy(), "channels: the site keeps its content policy routes");

	DeleteFolderDeep(scratch_dir);
	return ok;
}

END_UPP_NAMESPACE
