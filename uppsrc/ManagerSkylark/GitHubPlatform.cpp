#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"
#include <GitHubForge/GitHubForge.h>

NAMESPACE_UPP

String GitHubPlatformService::EscapeHtml(const String& s)
{
	String r;
	for(int i = 0; i < s.GetLength(); i++) {
		char c = s[i];
		switch(c) {
		case '&': r << "&amp;"; break;
		case '<': r << "&lt;"; break;
		case '>': r << "&gt;"; break;
		case '\"': r << "&quot;"; break;
		case '\'': r << "&#39;"; break;
		default: r.Cat(c); break;
		}
	}
	return r;
}

String GitHubPlatformService::VisibilityLabel(int is_private)
{
	if(is_private < 0)
		return "Unknown";
	return is_private ? "Private" : "Public";
}

String GitHubPlatformService::FormatFileSize(int64 bytes)
{
	if(bytes < 1024)
		return AsString(bytes) + " B";
	if(bytes < 1024 * 1024)
		return Sprintf("%.1f KB", (double)bytes / 1024.0);
	return Sprintf("%.1f MB", (double)bytes / (1024.0 * 1024.0));
}

// Local git runs argv-only; only stdout is kept, stderr is dropped.
static String GitStdout(const Vector<String>& args)
{
	LocalProcess p;
	if(!p.Start2("git", args))
		return String();
	p.CloseWrite();
	String out;
	for(;;) {
		String co, ce;
		bool more = p.Read2(co, ce);
		out << co;
		if(!more)
			break;
		Sleep(5);
	}
	return out;
}

String GitHubPlatformService::GetDevDir()
{
	String custom = GetEnv("DEV_DIR");
	if(!custom.IsEmpty() && DirectoryExists(custom))
		return custom;
	String d = GetHomeDirFile("Dev");
	if(DirectoryExists(d))
		return d;
	return GetHomeDirectory();
}

ValueMap GitHubPlatformService::LoadScrapedData(const String& owner, const String& repo)
{
	String path = AppendFileName(AppendFileName(GetHomeDirFile(".aria/sites/github"), owner + "_" + repo), "summary.json");
	if(FileExists(path)) {
		Value v = ParseJSON(LoadFile(path));
		if(v.Is<ValueMap>())
			return v;
	}
	return ValueMap();
}

bool GitHubPlatformService::IsGhCliAvailable()
{
	static bool tested = false;
	static bool available = false;
	if(!tested) {
		GitHubForgeBackend backend;
		available = backend.GhRun({"auth", "status"}).ok;
		tested = true;
	}
	return available;
}

Vector<LocalRepoInfo> GitHubPlatformService::GetLiveGitHubRepos(const String& owner)
{
	static String cached_owner;
	static Time last_fetch = Null;
	static Vector<LocalRepoInfo> cached_repos;

	if(cached_owner == owner && !IsNull(last_fetch) && (GetSysTime() - last_fetch) < 300)
		return clone(cached_repos);

	Vector<LocalRepoInfo> list;
	GitHubForgeBackend backend;
	Vector<ForgeRepo> repos;
	if(backend.ListRepos(owner, repos).ok) {
		for(const ForgeRepo& r : repos) {
			LocalRepoInfo info;
			info.owner = owner;
			info.name = r.name;
			info.description = r.description;
			info.is_private = r.is_private ? 1 : 0;
			info.stars = r.stars;
			info.forks = r.forks;
			info.language = r.language.IsEmpty() ? String("C++") : r.language;

			if(r.pushed_at.GetLength() >= 10)
				info.last_commit_time = r.pushed_at.Left(10);
			else
				info.last_commit_time = "recently";

			info.branch = "master";
			info.path = AppendFileName(GetDevDir(), info.name);
			list.Add(pick(info));
		}
	}

	if(list.IsEmpty()) {
		list = GetRepositories(owner);
	}

	cached_owner = owner;
	last_fetch = GetSysTime();
	cached_repos = clone(list);
	return list;
}

// REST timestamps for the views: "2026-11-08T12:34:56Z" (empty when unknown).
// U++ Format() does not pad like printf, so build the two-digit fields by hand.
static String TwoDigits(int v)
{
	return v < 10 ? "0" + AsString(v) : AsString(v);
}

static String IsoTime(const Time& t)
{
	if(IsNull(t))
		return String();
	return AsString(t.year) + "-" + TwoDigits(t.month) + "-" + TwoDigits(t.day) + "T" +
	       TwoDigits(t.hour) + ":" + TwoDigits(t.minute) + ":" + TwoDigits(t.second) + "Z";
}

// Issue operations run through the argv-only GitHub adapter. A fresh backend
// per call reads GHFORGE_GH at call time; failures surface as unavailable, never
// as fabricated data.
Vector<ValueMap> GitHubPlatformService::GetLiveIssues(const String& owner, const String& repo, String* unavailable)
{
	Vector<ValueMap> list;
	GitHubForgeBackend backend;
	Vector<ForgeIssue> issues;
	ForgeResult res = backend.ListIssues(owner + "/" + repo, issues);
	if(!res.ok) {
		if(unavailable)
			*unavailable = res.error;
		return list;
	}
	for(const ForgeIssue& iss : issues) {
		ValueMap issue;
		issue.Set("number", ScanInt(iss.issue_id));
		issue.Set("title", iss.title);
		issue.Set("state", String(iss.open ? "OPEN" : "CLOSED"));
		issue.Set("author", iss.author.IsEmpty() ? owner : iss.author);
		issue.Set("created_at", IsoTime(iss.created).Left(10));
		issue.Set("comments", iss.comment_count);
		list.Add(issue);
	}
	return list;
}

ValueMap GitHubPlatformService::GetLiveIssueDetail(const String& owner, const String& repo, int number, String* unavailable)
{
	ValueMap res;
	GitHubForgeBackend backend;
	String project = owner + "/" + repo;
	ForgeIssue issue;
	ForgeResult issue_res = backend.GetIssue(project, AsString(number), issue);
	if(!issue_res.ok) {
		if(unavailable)
			*unavailable = issue_res.error;
		return res;
	}
	Vector<ForgeComment> comments;
	ForgeResult comments_res = backend.ListComments(project, AsString(number), comments);
	if(!comments_res.ok) {
		if(unavailable)
			*unavailable = comments_res.error;
		return res;
	}

	res.Set("number", number);
	res.Set("title", issue.title);
	res.Set("body", issue.body);
	res.Set("state", String(issue.open ? "OPEN" : "CLOSED"));
	ValueMap author;
	if(!issue.author.IsEmpty())
		author.Set("login", issue.author);
	res.Set("author", author);
	res.Set("createdAt", IsoTime(issue.created));
	ValueArray thread;
	for(const ForgeComment& c : comments) {
		ValueMap comment;
		ValueMap comment_author;
		if(!c.author.IsEmpty())
			comment_author.Set("login", c.author);
		comment.Set("author", comment_author);
		comment.Set("body", c.body);
		comment.Set("createdAt", IsoTime(c.created));
		thread.Add(comment);
	}
	res.Set("comments", thread);
	return res;
}

String GitHubPlatformService::CreateIssue(const String& owner, const String& repo, const String& title, const String& body)
{
	GitHubForgeBackend backend;
	ForgeIssue draft;
	draft.title = title;
	draft.body = body;
	ForgeIssue created;
	ForgeResult res = backend.CreateIssue(owner + "/" + repo, draft, created);
	if(!res.ok)
		return "error: " + res.error;
	return "https://github.com/" + owner + "/" + repo + "/issues/" + created.issue_id;
}

String GitHubPlatformService::AddComment(const String& owner, const String& repo, int number, const String& body)
{
	GitHubForgeBackend backend;
	ForgeComment comment;
	comment.body = body;
	ForgeComment posted;
	ForgeResult res = backend.AddComment(owner + "/" + repo, AsString(number), comment, posted);
	if(!res.ok)
		return "error: " + res.error;
	return "https://github.com/" + owner + "/" + repo + "/issues/" + AsString(number) + "#issuecomment-" + posted.comment_id;
}

String GitHubPlatformService::ToggleIssueState(const String& owner, const String& repo, int number, bool close_it)
{
	GitHubForgeBackend backend;
	ForgeIssue updated;
	ForgeResult res = backend.SetIssueState(owner + "/" + repo, AsString(number), !close_it, updated);
	if(!res.ok)
		return "error: " + res.error;
	return close_it ? "closed" : "reopened";
}

// Live pull requests come from gh only. A failed or unparseable call is
// reported as unavailable; nothing is filled in from local stores or samples.
Vector<ValueMap> GitHubPlatformService::GetLivePullRequests(const String& owner, const String& repo, String* unavailable)
{
	Vector<ValueMap> list;
	GitHubForgeBackend backend;
	Vector<ForgePullRequest> prs;
	ForgeResult res = backend.ListPullRequests(owner + "/" + repo, prs);
	if(!res.ok) {
		if(unavailable)
			*unavailable = res.error;
		return list;
	}
	for(const ForgePullRequest& p : prs) {
		ValueMap pr;
		pr.Set("number", ScanInt(p.pr_id));
		pr.Set("title", p.title);
		pr.Set("state", ToUpper(p.state));
		pr.Set("author", p.author.IsEmpty() ? owner : p.author);
		pr.Set("created_at", IsoTime(p.created).Left(10));
		pr.Set("head", p.head);
		pr.Set("base", p.base);
		list.Add(pr);
	}
	return list;
}

// Detail keeps the view contract: OPEN/MERGED/CLOSED, author.login, createdAt,
// headRefName/baseRefName and a comment thread with author.login and body.
ValueMap GitHubPlatformService::GetLivePullRequestDetail(const String& owner, const String& repo, int number, String* unavailable)
{
	ValueMap res;
	GitHubForgeBackend backend;
	String project = owner + "/" + repo;
	ForgePullRequest pr;
	ForgeResult pr_res = backend.GetPullRequest(project, AsString(number), pr);
	if(!pr_res.ok) {
		if(unavailable)
			*unavailable = pr_res.error;
		return res;
	}
	Vector<ForgeComment> comments;
	ForgeResult comments_res = backend.ListComments(project, AsString(number), comments);
	if(!comments_res.ok) {
		if(unavailable)
			*unavailable = comments_res.error;
		return res;
	}

	res.Set("number", number);
	res.Set("title", pr.title);
	res.Set("body", pr.body);
	res.Set("state", ToUpper(pr.state));
	ValueMap author;
	if(!pr.author.IsEmpty())
		author.Set("login", pr.author);
	res.Set("author", author);
	res.Set("createdAt", IsoTime(pr.created));
	res.Set("headRefName", pr.head);
	res.Set("baseRefName", pr.base);
	ValueArray thread;
	for(const ForgeComment& c : comments) {
		ValueMap comment;
		ValueMap comment_author;
		if(!c.author.IsEmpty())
			comment_author.Set("login", c.author);
		comment.Set("author", comment_author);
		comment.Set("body", c.body);
		comment.Set("createdAt", IsoTime(c.created));
		thread.Add(comment);
	}
	res.Set("comments", thread);
	return res;
}

String GitHubPlatformService::GetPullRequestDiff(const String& owner, const String& repo, int number, String* unavailable)
{
	GitHubForgeBackend backend;
	String diff;
	ForgeResult res = backend.GetPullRequestDiff(owner + "/" + repo, AsString(number), diff);
	if(!res.ok) {
		if(unavailable)
			*unavailable = res.error;
		return String();
	}
	return diff;
}

String GitHubPlatformService::CreatePullRequest(const String& owner, const String& repo, const String& title, const String& body, const String& head, const String& base)
{
	GitHubForgeBackend backend;
	ForgePullRequest pr;
	ForgeResult res = backend.CreatePullRequest(owner + "/" + repo, title, body, head, base.IsEmpty() ? String("master") : base, pr);
	if(!res.ok)
		return "error: " + res.error;
	return pr.url.IsEmpty() ? "https://github.com/" + owner + "/" + repo + "/pull/" + pr.pr_id : pr.url;
}

String GitHubPlatformService::MergePullRequest(const String& owner, const String& repo, int number)
{
	GitHubForgeBackend backend;
	String message;
	ForgeResult res = backend.MergePullRequest(owner + "/" + repo, AsString(number), message);
	if(!res.ok)
		return "error: " + res.error;
	return message.IsEmpty() ? String("Merged successfully") : message;
}

String GitHubPlatformService::ClosePullRequest(const String& owner, const String& repo, int number)
{
	GitHubForgeBackend backend;
	ForgePullRequest pr;
	ForgeResult res = backend.SetPullRequestState(owner + "/" + repo, AsString(number), false, pr);
	if(!res.ok)
		return "error: " + res.error;
	return "Closed successfully";
}

// Pull request conversation comments are issue comments on GitHub.
String GitHubPlatformService::AddPullRequestComment(const String& owner, const String& repo, int number, const String& body)
{
	GitHubForgeBackend backend;
	ForgeComment comment;
	comment.body = body;
	ForgeComment posted;
	ForgeResult res = backend.AddComment(owner + "/" + repo, AsString(number), comment, posted);
	if(!res.ok)
		return "error: " + res.error;
	return "https://github.com/" + owner + "/" + repo + "/issues/" + AsString(number) + "#issuecomment-" + posted.comment_id;
}

Vector<LocalRepoInfo> GitHubPlatformService::GetRepositories(const String& owner)
{
	Vector<LocalRepoInfo> list;
	String dev_dir = GetDevDir();

	FindFile ff(AppendFileName(dev_dir, "*"));
	while(ff) {
		if(ff.IsFolder()) {
			String repo_path = ff.GetPath();
			String git_dir = AppendFileName(repo_path, ".git");
			if(DirectoryExists(git_dir)) {
				LocalRepoInfo info;
				info.name = ff.GetName();
				info.path = repo_path;
				info.owner = owner;

				String head = LoadFile(AppendFileName(git_dir, "HEAD"));
				if(head.StartsWith("ref: refs/heads/"))
					info.branch = TrimBoth(head.Mid(16));
				else
					info.branch = "master";

				if(FileExists(AppendFileName(repo_path, info.name + ".upp")) || DirectoryExists(AppendFileName(repo_path, "uppsrc")))
					info.language = "C++";
				else if(FileExists(AppendFileName(repo_path, "CMakeLists.txt")))
					info.language = "C++";
				else if(FileExists(AppendFileName(repo_path, "requirements.txt")) || FileExists(AppendFileName(repo_path, "setup.py")))
					info.language = "Python";
				else if(FileExists(AppendFileName(repo_path, "package.json")))
					info.language = "JavaScript";
				else
					info.language = "C++";

				String desc_file = AppendFileName(git_dir, "description");
				if(FileExists(desc_file)) {
					String d = TrimBoth(LoadFile(desc_file));
					if(!d.StartsWith("Unnamed repository"))
						info.description = d;
				}

				// Visibility is not known for a local checkout; it stays unknown.
				list.Add(pick(info));
			}
		}
		ff.Next();
	}

	Sort(list, [](const LocalRepoInfo& a, const LocalRepoInfo& b) {
		return a.name < b.name;
	});

	return list;
}

LocalRepoInfo GitHubPlatformService::GetRepo(const String& owner, const String& repo)
{
	LocalRepoInfo info;
	info.owner = owner;
	info.name = repo;
	info.path = AppendFileName(GetDevDir(), repo);
	if(!DirectoryExists(info.path) && repo == "ai-upp-priv") {
		String alt = AppendFileName(GetDevDir(), "ai-upp");
		if(DirectoryExists(alt))
			info.path = alt;
	}
	info.branch = "master";

	if(DirectoryExists(info.path)) {
		String git_dir = AppendFileName(info.path, ".git");
		if(DirectoryExists(git_dir)) {
			String head = LoadFile(AppendFileName(git_dir, "HEAD"));
			if(head.StartsWith("ref: refs/heads/"))
				info.branch = TrimBoth(head.Mid(16));

			String out = GitStdout({"-C", info.path, "log", "-1", "--format=%h|%an|%cr|%s"});
			Vector<String> parts = Split(TrimBoth(out), '|');
			if(parts.GetCount() >= 4) {
				info.last_commit_hash = parts[0];
				info.last_commit_author = parts[1];
				info.last_commit_time = parts[2];
				info.last_commit_msg = parts[3];
			}
		}
	} else {
		ValueMap sc = LoadScrapedData(owner, repo);
		if(!sc.IsEmpty()) {
			ValueMap ov = sc["overview"];
			info.branch = ov["branch"];
			info.description = ov["description"];
			info.is_private = ov["is_private"].IsNull() ? -1 : ((bool)ov["is_private"] ? 1 : 0);
		}
	}

	return info;
}

Vector<String> GitHubPlatformService::GetBranches(const String& repo_path)
{
	Vector<String> branches;
	if(DirectoryExists(repo_path)) {
		String out = GitStdout({"-C", repo_path, "branch", "--format=%(refname:short)"});
		Vector<String> lines = Split(TrimBoth(out), '\n');
		for(const String& l : lines) {
			String b = TrimBoth(l);
			if(!b.IsEmpty())
				branches.Add(b);
		}
	}
	if(branches.IsEmpty())
		branches.Add("master");
	return branches;
}

Vector<ValueMap> GitHubPlatformService::GetTree(const String& repo_path, const String& subpath)
{
	Vector<ValueMap> list;
	String full_dir = repo_path;
	if(!subpath.IsEmpty())
		full_dir = AppendFileName(repo_path, subpath);

	if(DirectoryExists(full_dir)) {
		FindFile ff(AppendFileName(full_dir, "*"));
		Vector<String> dirs, files;
		while(ff) {
			String n = ff.GetName();
			if(n != ".git" && n != "_out" && n != "tmp") {
				if(ff.IsFolder()) dirs.Add(n);
				else files.Add(n);
			}
			ff.Next();
		}
		Sort(dirs);
		Sort(files);

		for(const String& d : dirs) {
			ValueMap m;
			m.Set("name", d);
			m.Set("type", "dir");
			String rel = subpath.IsEmpty() ? d : subpath + "/" + d;
			m.Set("path", rel);
			m.Set("msg", "Directory snapshot");
			m.Set("age", "recent");
			list.Add(m);
		}
		for(const String& f : files) {
			ValueMap m;
			m.Set("name", f);
			m.Set("type", "file");
			String rel = subpath.IsEmpty() ? f : subpath + "/" + f;
			m.Set("path", rel);
			m.Set("msg", "Source artifact");
			m.Set("age", "recent");
			list.Add(m);
		}
	}
	return list;
}

ValueMap GitHubPlatformService::GetBlob(const String& repo_path, const String& filepath)
{
	ValueMap res;
	String full = AppendFileName(repo_path, filepath);
	if(FileExists(full)) {
		String c = LoadFile(full);
		int lines = 1;
		for(int i = 0; i < c.GetLength(); i++)
			if(c[i] == '\n') lines++;
		res.Set("content", c);
		res.Set("lines", lines);
		res.Set("size", (int64)c.GetLength());
		res.Set("size_str", FormatFileSize(c.GetLength()));
		res.Set("name", GetFileName(filepath));
		res.Set("path", filepath);
	}
	return res;
}

Vector<ValueMap> GitHubPlatformService::GetCommits(const String& repo_path, int limit)
{
	Vector<ValueMap> commits;
	if(DirectoryExists(repo_path)) {
		String out = GitStdout({"-C", repo_path, "log", "-n", AsString(limit), "--format=%h|%an|%ae|%cr|%s"});
		Vector<String> lines = Split(TrimBoth(out), '\n');
		for(const String& l : lines) {
			Vector<String> p = Split(TrimBoth(l), '|');
			if(p.GetCount() >= 5) {
				ValueMap m;
				m.Set("hash", p[0]);
				m.Set("author", p[1]);
				m.Set("email", p[2]);
				m.Set("time", p[3]);
				m.Set("msg", p[4]);
				commits.Add(m);
			}
		}
	}
	return commits;
}

String GitHubPlatformService::RenderRepoHeader(const LocalRepoInfo& info)
{
	String h;
	h << "<div class=\"wf-gh-repohead\">\n"
	  << "  <div class=\"wf-gh-repohead-row\">\n"
	  << "    <div class=\"wf-gh-title\">\n"
	  << "      " << VividIcon::Get("git-repo", 20) << "\n"
	  << "      <a href=\"/user/" << info.owner << "\">" << info.owner << "</a> / "
	  << "      <strong><a href=\"/repo/" << info.owner << "/" << info.name << "\">" << info.name << "</a></strong>\n"
	  << "      <span class=\"wf-gh-badge\">" << VisibilityLabel(info.is_private) << "</span>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-gh-actions\">\n"
	  << "      <a href=\"/projects/" << info.name << "/\" class=\"wf-gh-btn\" title=\"Switch to SourceForge Platform Style\" style=\"color:#ee6524; font-weight:600; text-decoration:none;\">\n"
	  << "        <svg width=\"14\" height=\"14\" viewBox=\"0 0 24 24\" fill=\"currentColor\" style=\"vertical-align:-2px;\"><path d=\"M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5\"/></svg>\n"
	  << "        <span>SourceForge View</span>\n"
	  << "      </a>\n"
	  << "      <div class=\"wf-gh-btn-group\">\n"
	  << "        <button class=\"wf-gh-btn\">" << VividIcon::Get("eye", 14) << " Watch <span class=\"wf-gh-counter\">1</span></button>\n"
	  << "      </div>\n"
	  << "      <div class=\"wf-gh-btn-group\">\n"
	  << "        <button class=\"wf-gh-btn\">" << VividIcon::Get("star", 14) << " Star <span class=\"wf-gh-counter\">" << (info.stars < 0 ? String("n/a") : AsString(info.stars)) << "</span></button>\n"
	  << "      </div>\n"
	  << "      <div class=\"wf-gh-btn-group\">\n"
	  << "        <button class=\"wf-gh-btn\">" << VividIcon::Get("git-fork", 14) << " Fork <span class=\"wf-gh-counter\">" << (info.forks < 0 ? String("n/a") : AsString(info.forks)) << "</span></button>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n";
	return h;
}

String GitHubPlatformService::RenderRepoTabs(const String& owner, const String& repo, const String& active_tab, const LocalRepoInfo& info)
{
	String base = "/repo/" + owner + "/" + repo;
	String h;
	h << "  <nav class=\"wf-gh-nav-tabs\" aria-label=\"Repository\">\n"
	  << "    <a href=\"" << base << "?tab=code\" class=\"wf-gh-tab" << (active_tab == "code" ? " active" : "") << "\">"
	  << VividIcon::Get("code", 16) << " Code</a>\n"
	  << "    <a href=\"" << base << "?tab=issues\" class=\"wf-gh-tab" << (active_tab == "issues" ? " active" : "") << "\">"
	  << VividIcon::Get("bug", 16) << " Issues <span class=\"wf-gh-counter\">2</span></a>\n"
	  << "    <a href=\"" << base << "?tab=pulls\" class=\"wf-gh-tab" << (active_tab == "pulls" ? " active" : "") << "\">"
	  << VividIcon::Get("git-pull-request", 16) << " Pull requests <span class=\"wf-gh-counter\">1</span></a>\n"
	  << "    <a href=\"" << base << "?tab=actions\" class=\"wf-gh-tab" << (active_tab == "actions" ? " active" : "") << "\">"
	  << VividIcon::Get("play-circle", 16) << " Actions</a>\n"
	  << "    <a href=\"" << base << "?tab=projects\" class=\"wf-gh-tab" << (active_tab == "projects" ? " active" : "") << "\">"
	  << VividIcon::Get("layout-dashboard", 16) << " Projects <span class=\"wf-gh-counter\">3</span></a>\n"
	  << "    <a href=\"" << base << "?tab=security\" class=\"wf-gh-tab" << (active_tab == "security" ? " active" : "") << "\">"
	  << VividIcon::Get("shield", 16) << " Security</a>\n"
	  << "    <a href=\"" << base << "?tab=dependencies\" class=\"wf-gh-tab" << (active_tab == "dependencies" ? " active" : "") << "\">"
	  << VividIcon::Get("network", 16) << " Insights</a>\n"
	  << "    <a href=\"" << base << "?tab=settings\" class=\"wf-gh-tab" << (active_tab == "settings" ? " active" : "") << "\">"
	  << VividIcon::Get("settings", 16) << " Settings</a>\n"
	  << "    <a href=\"" << base << "?tab=agents\" class=\"wf-gh-tab" << (active_tab == "agents" ? " active" : "") << "\" style=\"color:var(--wf-color-positive, #00aa7f); font-weight:600;\">"
	  << VividIcon::Get("robot", 16) << " SweAgent Pair</a>\n"
	  << "  </nav>\n"
	  << "</div>\n";
	return h;
}

END_UPP_NAMESPACE
