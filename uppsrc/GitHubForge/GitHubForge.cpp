#include "GitHubForge.h"
#include "GitHubGhConnector.h"
#include "GitHubEmulatedBackend.h"

NAMESPACE_UPP

static GitHubForgeBackend::Config& ActiveConfig()
{
	static GitHubForgeBackend::Config cfg;
	return cfg;
}

void GitHubForgeBackend::Configure(const Config& cfg)
{
	ActiveConfig() = cfg;
}

const GitHubForgeBackend::Config& GitHubForgeBackend::GetConfig()
{
	return ActiveConfig();
}

bool GitHubForgeBackend::ParseKind(const String& name, Kind& kind)
{
	if(name == "gh") {
		kind = Kind::Gh;
		return true;
	}
	if(name == "emulated") {
		kind = Kind::Emulated;
		return true;
	}
	return false;
}

One<GitHubForgeBackend> GitHubForgeBackend::Create()
{
	const Config& cfg = GetConfig();
	if(cfg.kind == Kind::Emulated)
		return One<GitHubForgeBackend>(new GitHubEmulatedBackend(cfg.store_dir));
	return One<GitHubForgeBackend>(new GitHubGhConnector(cfg.gh_bin));
}

bool GitHubForgeBackend::IsValidRepoSlug(const String& slug)
{
	if(slug.GetCount() < 3 || slug.GetCount() > 200)
		return false;
	Vector<String> parts = Split(slug, '/');
	if(parts.GetCount() != 2 || parts[0].IsEmpty() || parts[1].IsEmpty())
		return false;
	for(int i = 0; i < slug.GetCount(); i++) {
		int c = slug[i];
		bool safe = IsAlpha(c) || IsDigit(c) || c == '.' || c == '_' || c == '-' || c == '/';
		if(!safe)
			return false;
	}
	return true;
}

bool GitHubForgeBackend::IsValidIssueId(const String& issue_id)
{
	if(issue_id.IsEmpty() || issue_id.GetCount() > 18)
		return false;
	for(int i = 0; i < issue_id.GetCount(); i++)
		if(!IsDigit(issue_id[i]))
			return false;
	return true;
}

Time GitHubForgeBackend::ParseGhTime(const String& iso)
{
	// 2026-11-08T12:34:56Z (also tolerate +hh:mm offsets by ignoring them).
	if(iso.GetCount() < 19)
		return Time();
	int y = ScanInt(iso.Mid(0, 4));
	int mo = ScanInt(iso.Mid(5, 2));
	int d = ScanInt(iso.Mid(8, 2));
	int h = ScanInt(iso.Mid(11, 2));
	int mi = ScanInt(iso.Mid(14, 2));
	int s = ScanInt(iso.Mid(17, 2));
	if(y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31)
		return Time();
	return Time(y, mo, d, h, mi, s);
}

ForgeIssue GitHubForgeBackend::IssueFromJson(const ValueMap& m, const String& project)
{
	ForgeIssue iss;
	iss.project = project;
	iss.issue_id = AsString((int)m.Get("number"));
	iss.title = m.Get("title").IsNull() ? String() : (String)m.Get("title");
	iss.body = m.Get("body").IsNull() ? String() : (String)m.Get("body");
	Value user = m.Get("user");
	if(user.Is<ValueMap>() && !user["login"].IsNull())
		iss.author = (String)user["login"];
	iss.open = ((String)m.Get("state")) != "closed";
	iss.created = ParseGhTime(m.Get("created_at").IsNull() ? String() : (String)m.Get("created_at"));
	iss.updated = ParseGhTime(m.Get("updated_at").IsNull() ? String() : (String)m.Get("updated_at"));
	Value labels = m.Get("labels");
	if(labels.Is<ValueArray>())
		for(const Value& l : (ValueArray)labels)
			if(l.Is<ValueMap>() && !l["name"].IsNull())
				iss.labels.Add((String)l["name"]);
	if(!m.Get("comments").IsNull())
		iss.comment_count = (int)m.Get("comments");
	return iss;
}

ForgeComment GitHubForgeBackend::CommentFromJson(const ValueMap& m)
{
	ForgeComment c;
	c.comment_id = m.Get("id").IsNull() ? String() : AsString(m.Get("id"));
	c.author = String();
	Value user = m.Get("user");
	if(user.Is<ValueMap>() && !user["login"].IsNull())
		c.author = (String)user["login"];
	c.body = m.Get("body").IsNull() ? String() : (String)m.Get("body");
	c.created = ParseGhTime(m.Get("created_at").IsNull() ? String() : (String)m.Get("created_at"));
	return c;
}

bool GitHubForgeBackend::IsValidLogin(const String& login)
{
	if(login.IsEmpty() || login.GetCount() > 39 || login[0] == '-')
		return false;
	for(int i = 0; i < login.GetCount(); i++) {
		int c = login[i];
		if(!(IsAlpha(c) || IsDigit(c) || c == '-'))
			return false;
	}
	return true;
}

bool GitHubForgeBackend::IsValidBranchName(const String& branch)
{
	if(branch.IsEmpty() || branch.GetCount() > 200 || branch[0] == '-')
		return false;
	for(int i = 0; i < branch.GetCount(); i++) {
		int c = branch[i];
		bool safe = IsAlpha(c) || IsDigit(c) || c == '.' || c == '_' || c == '-' || c == '/';
		if(!safe)
			return false;
	}
	return true;
}

String GitHubForgeBackend::Field(const ValueMap& m, const char *key)
{
	Value v = m.Get(key);
	return v.IsNull() ? String() : AsString(v);
}

// Reads one string from a nested object such as user.login or head.ref.
String GitHubForgeBackend::SubField(const Value& v, const char *key)
{
	if(!v.Is<ValueMap>())
		return String();
	ValueMap nested = v;
	return Field(nested, key);
}

ForgePullRequest GitHubForgeBackend::PullRequestFromJson(const ValueMap& m, const String& project)
{
	ForgePullRequest pr;
	pr.project = project;
	pr.pr_id = AsString((int)m.Get("number"));
	pr.title = Field(m, "title");
	pr.body = Field(m, "body");
	pr.author = SubField(m.Get("user"), "login");
	pr.created = ParseGhTime(Field(m, "created_at"));
	pr.head = SubField(m.Get("head"), "ref");
	pr.base = SubField(m.Get("base"), "ref");
	pr.url = Field(m, "html_url");
	// REST reports merged pulls as "closed" with a merged_at timestamp.
	if(!Field(m, "merged_at").IsEmpty())
		pr.state = "merged";
	else
		pr.state = Field(m, "state") == "closed" ? String("closed") : String("open");
	return pr;
}

ForgeRepo GitHubForgeBackend::RepoFromJson(const ValueMap& m, const String& owner)
{
	ForgeRepo r;
	r.owner = owner;
	r.name = Field(m, "name");
	r.description = Field(m, "description");
	r.is_private = (bool)m.Get("isPrivate");
	r.stars = (int)m.Get("stargazerCount");
	r.forks = (int)m.Get("forkCount");
	r.language = SubField(m.Get("primaryLanguage"), "name");
	r.pushed_at = Field(m, "pushedAt");
	return r;
}

ForgeRun GitHubForgeBackend::RunFromJson(const ValueMap& m)
{
	ForgeRun r;
	r.run_id = (int64)m.Get("id");
	r.name = Field(m, "name");
	r.status = Field(m, "status");
	r.conclusion = Field(m, "conclusion");
	r.head_branch = Field(m, "head_branch");
	r.event = Field(m, "event");
	r.created_at = Field(m, "created_at");
	return r;
}

END_UPP_NAMESPACE
