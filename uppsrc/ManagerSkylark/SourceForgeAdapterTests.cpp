#include "ManagerSkylark.h"
#include "SourceForgePlatform.h"
#include <SourceForgeForge/SourceForgeForge.h>

NAMESPACE_UPP

// Synthetic checks for the SourceForge webapp wiring. No network: the backend
// is the real SourceForgeForgeBackend over a stub Allura transport, with no
// credentials configured (read-only). The default backend is restored on exit.

static bool SfFail(const char *what)
{
	Cerr() << "FAILED: SourceForge adapter: " << what << "\n";
	return false;
}

// Canned Allura responses. reachable=false simulates a transport failure.
struct StubAlluraTransport : ISfTransport {
	bool reachable = true;

	SfHttpResponse Request(const String& method, const String& path,
	                       const String& form_body) override {
		SfHttpResponse r;
		if(!reachable) {
			r.status = 503;
			r.error = "stub: unreachable";
			return r;
		}
		if(path.Find("/tickets?") >= 0) {
			r.ok = true;
			r.status = 200;
			r.body = "{\"tickets\":[{\"ticket_num\":5,\"summary\":\"Crash on start\",\"status\":\"open\",\"reported_by\":\"alice\",\"ticket_date\":\"2026-11-08T09:00:00Z\",\"mod_date\":\"2026-11-08T09:30:00Z\"}],\"count\":1}";
			return r;
		}
		r.status = 404;
		r.error = "stub: no such path";
		return r;
	}
};

bool SourceForgePlatformService::RunSourceForgeSyntheticTests()
{
	SfConfig cfg;
	cfg.project = "topside";
	StubAlluraTransport *stub = new StubAlluraTransport;
	SourceForgeForgeBackend backend(cfg, stub);
	SetBackend(&backend);

	bool ok = true;
	ForgeResult r;

	Vector<SourceForgeTicket> tickets = GetTickets("topside", &r);
	if(!r.ok || tickets.GetCount() != 1 || tickets[0].num != 5 ||
	   tickets[0].summary != "Crash on start" || tickets[0].status != "open" || tickets[0].author != "alice")
		ok = SfFail("read-only ticket list maps Allura tickets");

	int created = CreateTicket("topside", "new", "body", "normal", "", "alice", &r);
	if(r.ok || created != -1 || r.error.Find("api credentials") < 0)
		ok = SfFail("create without credentials is unavailable, not faked");

	if(AddTicketComment("topside", 5, "alice", "hi", &r) || r.ok)
		ok = SfFail("comment without credentials is unavailable");

	if(ToggleTicketStatus("topside", 5, "closed", &r) || r.ok)
		ok = SfFail("status change without credentials is unavailable");

	if(ToggleTicketStatus("topside", 5, "bogus", &r) || r.ok)
		ok = SfFail("unknown ticket status is rejected");

	Vector<SourceForgeWikiPage> wiki = GetWikiPages("topside", &r);
	if(r.ok || wiki.GetCount() != 0)
		ok = SfFail("wiki reports unsupported with no pages");

	Vector<SourceForgeDiscussionTopic> topics = GetDiscussionTopics("topside", "", &r);
	if(r.ok || topics.GetCount() != 0)
		ok = SfFail("discussion reports unsupported with no topics");

	Vector<SourceForgeReleaseFile> files = GetFiles("topside", &r);
	if(r.ok || files.GetCount() != 0)
		ok = SfFail("files report unsupported with no entries");

	stub->reachable = false;
	tickets = GetTickets("topside", &r);
	if(r.ok || tickets.GetCount() != 0 || r.error.IsEmpty())
		ok = SfFail("unreachable backend yields an explicit unavailable result, not an empty success");

	SetBackend(NULL);
	return ok;
}

END_UPP_NAMESPACE
