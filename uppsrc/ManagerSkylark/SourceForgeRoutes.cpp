#include "ManagerSkylark.h"
#include "SourceForgePlatform.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// SKYLARK Routing Handlers
// ---------------------------------------------------------------------------

SKYLARK(SourceForgeProjectDeep, "p/*/**")
{
	String project = http[0];
	String subpath;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) subpath << "/";
		subpath << http[i];
	}
	RenderSourceForgeView(http, project, subpath);
}

SKYLARK(SourceForgeProjectRoot, "p/*")
{
	String project = http[0];
	RenderSourceForgeView(http, project, "");
}

SKYLARK(SourceForgeProjectsDeep, "projects/*/**")
{
	String project = http[0];
	String subpath;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) subpath << "/";
		subpath << http[i];
	}
	RenderSourceForgeView(http, project, subpath);
}

SKYLARK(SourceForgeProjectsRoot, "projects/*")
{
	String project = http[0];
	RenderSourceForgeView(http, project, "");
}

SKYLARK(SourceForgeDirectDeep, "sf/*/**")
{
	String project = http[0];
	String subpath;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) subpath << "/";
		subpath << http[i];
	}
	RenderSourceForgeView(http, project, subpath);
}

SKYLARK(SourceForgeDirectRoot, "sf/*")
{
	String project = http[0];
	RenderSourceForgeView(http, project, "");
}

// ---------------------------------------------------------------------------
// REST API Handlers for SourceForge Tools
// ---------------------------------------------------------------------------

SKYLARK(ApiSfTicketsList, "api/sf/tickets/*")
{
	String project = http[0];
	Vector<SourceForgeTicket> list = SourceForgePlatformService::GetTickets(project);
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiSfTicketDetail, "api/sf/ticket/*/*")
{
	String project = http[0];
	int num = ScanInt(http[1]);
	SourceForgeTicket t = SourceForgePlatformService::GetTicket(project, num);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(t));
}

SKYLARK(ApiSfCreateTicket, "api/sf/tickets/*/create:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	String summary = req["summary"];
	String description = req["description"];
	String priority = req["priority"];
	String milestone = req["milestone"];
	String author = req["author"];

	int num = SourceForgePlatformService::CreateTicket(project, summary, description, priority, milestone, author);
	ValueMap res;
	res.Set("ok", true);
	res.Set("num", num);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfCommentTicket, "api/sf/tickets/*/comment:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	int num = (int)req["num"];
	String comment = req["comment"];
	String author = req["author"];

	bool ok = SourceForgePlatformService::AddTicketComment(project, num, author, comment);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfToggleTicket, "api/sf/tickets/*/toggle:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	int num = (int)req["num"];
	String new_status = req["status"];

	bool ok = SourceForgePlatformService::ToggleTicketStatus(project, num, new_status);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfWikiPageGet, "api/sf/wiki/*/*")
{
	String project = http[0];
	String slug = http[1];
	SourceForgeWikiPage p = SourceForgePlatformService::GetWikiPage(project, slug);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(p));
}

SKYLARK(ApiSfWikiPageSave, "api/sf/wiki/*/save:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	SourceForgeWikiPage p;
	p.slug = req["slug"];
	p.title = req["title"];
	p.content = req["content"];
	p.author = req["author"];

	bool ok = SourceForgePlatformService::SaveWikiPage(project, p);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfDiscussionTopics, "api/sf/discussion/*")
{
	String project = http[0];
	String forum = http["forum"];
	Vector<SourceForgeDiscussionTopic> list = SourceForgePlatformService::GetDiscussionTopics(project, forum);
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiSfCreateTopic, "api/sf/discussion/*/topic/create:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	String forum = req["forum"];
	String title = req["title"];
	String first_post = req["first_post"];
	String author = req["author"];

	int id = SourceForgePlatformService::CreateDiscussionTopic(project, forum, title, first_post, author);
	ValueMap res;
	res.Set("ok", true);
	res.Set("id", id);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfAddPost, "api/sf/discussion/*/post/create:POST_RAW")
{
	String project = http[0];
	String raw = http.GetRequestContent();
	Value req = ParseJSON(raw);
	int topic_id = (int)req["topic_id"];
	String text = req["text"];
	String author = req["author"];

	bool ok = SourceForgePlatformService::AddDiscussionPost(project, topic_id, author, text);
	ValueMap res;
	res.Set("ok", ok);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiSfFilesList, "api/sf/files/*")
{
	String project = http[0];
	Vector<SourceForgeReleaseFile> list = SourceForgePlatformService::GetFiles(project);
	ValueArray va;
	for(int i = 0; i < list.GetCount(); i++)
		va.Add(StoreAsJsonValue(list[i]));
	http.ContentType("application/json") << AsJSON(va);
}

END_UPP_NAMESPACE
