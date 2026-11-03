#ifndef _reference_ManagerSkylark_SourceForgePlatform_h_
#define _reference_ManagerSkylark_SourceForgePlatform_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

struct SourceForgeProjectInfo : Moveable<SourceForgeProjectInfo> {
	String name;
	String title;
	String maintainer;
	String description;
	String license;
	String operating_systems;
	String languages;
	String categories;
	String git_repo_url;
	String active_branch;
	int    downloads_weekly = 0;
	int    rating_stars = 5;
	int    rating_count = 1;
	String last_update;
	bool   is_subproject = false;
	String parent_project;

	Vector<String> features;
	Vector<String> branches;

	void Jsonize(JsonIO& io) {
		io("name", name)("title", title)("maintainer", maintainer)
		  ("description", description)("license", license)
		  ("operating_systems", operating_systems)("languages", languages)
		  ("categories", categories)("git_repo_url", git_repo_url)
		  ("active_branch", active_branch)("downloads_weekly", downloads_weekly)
		  ("rating_stars", rating_stars)("rating_count", rating_count)
		  ("last_update", last_update)("is_subproject", is_subproject)
		  ("parent_project", parent_project)("features", features)
		  ("branches", branches);
	}
};

struct SourceForgeTicket : Moveable<SourceForgeTicket> {
	int    num = 1;
	String summary;
	String status = "open";      // "open", "in-progress", "closed"
	String priority = "normal";  // "low", "normal", "high", "urgent"
	String milestone = "Phase 13";
	String author = "sppp";
	String created_at;
	String description;
	Vector<ValueMap> comments; // author, text, created_at

	SourceForgeTicket() {}
	SourceForgeTicket(const SourceForgeTicket& o) {
		num = o.num; summary = o.summary; status = o.status; priority = o.priority;
		milestone = o.milestone; author = o.author; created_at = o.created_at;
		description = o.description; comments <<= o.comments;
	}
	SourceForgeTicket& operator=(const SourceForgeTicket& o) {
		num = o.num; summary = o.summary; status = o.status; priority = o.priority;
		milestone = o.milestone; author = o.author; created_at = o.created_at;
		description = o.description; comments <<= o.comments;
		return *this;
	}
	SourceForgeTicket(SourceForgeTicket&&) = default;
	SourceForgeTicket& operator=(SourceForgeTicket&&) = default;

	void Jsonize(JsonIO& io) {
		io("num", num)("summary", summary)("status", status)
		  ("priority", priority)("milestone", milestone)("author", author)
		  ("created_at", created_at)("description", description)
		  ("comments", comments);
	}
};

struct SourceForgeWikiPage : Moveable<SourceForgeWikiPage> {
	String title;
	String slug;
	String content;
	String author = "sppp";
	String updated_at;
	int    version = 1;

	SourceForgeWikiPage() {}
	SourceForgeWikiPage(const SourceForgeWikiPage&) = default;
	SourceForgeWikiPage& operator=(const SourceForgeWikiPage&) = default;
	SourceForgeWikiPage(SourceForgeWikiPage&&) = default;
	SourceForgeWikiPage& operator=(SourceForgeWikiPage&&) = default;

	void Jsonize(JsonIO& io) {
		io("title", title)("slug", slug)("content", content)
		  ("author", author)("updated_at", updated_at)("version", version);
	}
};

struct SourceForgeDiscussionTopic : Moveable<SourceForgeDiscussionTopic> {
	int    id = 1;
	String forum = "general";
	String title;
	String author = "sppp";
	String created_at;
	Vector<ValueMap> posts; // author, text, created_at

	SourceForgeDiscussionTopic() {}
	SourceForgeDiscussionTopic(const SourceForgeDiscussionTopic& o) {
		id = o.id; forum = o.forum; title = o.title; author = o.author;
		created_at = o.created_at; posts <<= o.posts;
	}
	SourceForgeDiscussionTopic& operator=(const SourceForgeDiscussionTopic& o) {
		id = o.id; forum = o.forum; title = o.title; author = o.author;
		created_at = o.created_at; posts <<= o.posts;
		return *this;
	}
	SourceForgeDiscussionTopic(SourceForgeDiscussionTopic&&) = default;
	SourceForgeDiscussionTopic& operator=(SourceForgeDiscussionTopic&&) = default;

	void Jsonize(JsonIO& io) {
		io("id", id)("forum", forum)("title", title)("author", author)
		  ("created_at", created_at)("posts", posts);
	}
};

struct SourceForgeReleaseFile : Moveable<SourceForgeReleaseFile> {
	String name;
	String version;
	String date;
	int64  size_bytes = 0;
	String size_str;
	int    downloads = 0;
	String sha256;
	String url;

	SourceForgeReleaseFile() {}
	SourceForgeReleaseFile(const SourceForgeReleaseFile&) = default;
	SourceForgeReleaseFile& operator=(const SourceForgeReleaseFile&) = default;
	SourceForgeReleaseFile(SourceForgeReleaseFile&&) = default;
	SourceForgeReleaseFile& operator=(SourceForgeReleaseFile&&) = default;

	void Jsonize(JsonIO& io) {
		io("name", name)("version", version)("date", date)
		  ("size_bytes", size_bytes)("size_str", size_str)
		  ("downloads", downloads)("sha256", sha256)("url", url);
	}
};

class SourceForgePlatformService {
public:
	static SourceForgeProjectInfo GetProject(const String& project_slug);
	static ValueMap LoadScrapedData(const String& project_slug);
	static String RenderProjectHeader(const SourceForgeProjectInfo& info, const String& active_tab);
	static String RenderSidebar(const SourceForgeProjectInfo& info);
	static String RenderTopNav(const SourceForgeProjectInfo& info);

	// Tickets Store & Operations
	static Vector<SourceForgeTicket> GetTickets(const String& project);
	static SourceForgeTicket        GetTicket(const String& project, int num);
	static int                       CreateTicket(const String& project, const String& summary, const String& description, const String& priority, const String& milestone, const String& author);
	static bool                      AddTicketComment(const String& project, int num, const String& author, const String& comment);
	static bool                      ToggleTicketStatus(const String& project, int num, const String& new_status = "");

	// Wiki Store & Operations
	static Vector<SourceForgeWikiPage> GetWikiPages(const String& project);
	static SourceForgeWikiPage       GetWikiPage(const String& project, const String& slug);
	static bool                      SaveWikiPage(const String& project, const SourceForgeWikiPage& page);

	// Discussion Store & Operations
	static Vector<SourceForgeDiscussionTopic> GetDiscussionTopics(const String& project, const String& forum = "");
	static SourceForgeDiscussionTopic GetDiscussionTopic(const String& project, int id);
	static int                       CreateDiscussionTopic(const String& project, const String& forum, const String& title, const String& first_post, const String& author);
	static bool                      AddDiscussionPost(const String& project, int topic_id, const String& author, const String& text);

	// Files Store
	static Vector<SourceForgeReleaseFile> GetFiles(const String& project);
};

END_UPP_NAMESPACE

#endif
