#include "ManagerSkylark.h"

NAMESPACE_UPP

static void SourceDataNotConfigured(Http& http, bool api)
{
	if(api) {
		ValueMap response;
		response.Add("error", "source_data_not_configured");
		http.ContentType("application/json; charset=utf-8");
		http.Response(503, "Service Unavailable");
		http << AsJSON(response);
	}
	else {
		http.ContentType("text/html; charset=utf-8");
		http.Response(503, "Service Unavailable");
		http << "<!doctype html><html><body><h1>Service Unavailable</h1>"
		     << "<p>source_data_not_configured</p></body></html>";
	}
}

SKYLARK(SourceForgeProjectDeep, "p/*/**")
{
	SourceDataNotConfigured(http, false);
}

SKYLARK(SourceForgeProjectRoot, "p/*")
{
	SourceDataNotConfigured(http, false);
}

SKYLARK(SourceForgeProjectsDeep, "projects/*/**")
{
	SourceDataNotConfigured(http, false);
}

SKYLARK(SourceForgeProjectsRoot, "projects/*")
{
	SourceDataNotConfigured(http, false);
}

SKYLARK(SourceForgeDirectDeep, "sf/*/**")
{
	SourceDataNotConfigured(http, false);
}

SKYLARK(SourceForgeDirectRoot, "sf/*")
{
	SourceDataNotConfigured(http, false);
}

SKYLARK(ApiSfTicketsList, "api/sf/tickets/*")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfTicketDetail, "api/sf/ticket/*/*")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfCreateTicket, "api/sf/tickets/*/create:POST_RAW")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfCommentTicket, "api/sf/tickets/*/comment:POST_RAW")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfToggleTicket, "api/sf/tickets/*/toggle:POST_RAW")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfWikiPageGet, "api/sf/wiki/*/*")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfWikiPageSave, "api/sf/wiki/*/save:POST_RAW")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfDiscussionTopics, "api/sf/discussion/*")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfCreateTopic, "api/sf/discussion/*/topic/create:POST_RAW")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfAddPost, "api/sf/discussion/*/post/create:POST_RAW")
{
	SourceDataNotConfigured(http, true);
}

SKYLARK(ApiSfFilesList, "api/sf/files/*")
{
	SourceDataNotConfigured(http, true);
}

END_UPP_NAMESPACE
