#include "GitHubPlatform.h"
#include "GitHubRepoPage.h"
#include <GitHubForge/GitHubTemplates.h>
#include <WebFramework/WebFramework.h>

NAMESPACE_UPP

// Code tab: branch bar, blob viewer or directory tree, and About sidebar.
// Each part is its own template: github-code-branch-bar, github-code-blob,
// github-code-tree and github-code-sidebar.
void RenderRepoCodeTab(GitHubRepoPage& page)
{
	const String& owner = page.owner;
	const String& repo = page.repo;
	const String& branch = page.branch;
	const String& file_or_folder = page.file_or_folder;
	const String& mode = page.mode;
	const LocalRepoInfo& info = page.info;
	const ValueMap& scraped = page.scraped;
	String& content = page.content;
	String& sidebar = page.sidebar;

	String base_url = "/repo/" + owner + "/" + repo;

	// Path breadcrumb: every segment but the last links to its folder.
	ValueArray crumbs;
	String last_name;
	if(!file_or_folder.IsEmpty()) {
		Vector<String> segs = Split(file_or_folder, '/');
		String accum;
		for(int i = 0; i < segs.GetCount(); i++) {
			if(!accum.IsEmpty()) accum << "/";
			accum << segs[i];
			if(i == segs.GetCount() - 1) {
				last_name = segs[i];
				break;
			}
			ValueMap crumb;
			crumb.Add("href", base_url + "/tree/" + branch + "/" + accum);
			crumb.Add("name", segs[i]);
			crumbs.Add(crumb);
		}
	}

	Renderer bar;
	bar("icon_git_branch", VividIcon::Get("git-branch", 14))
		("branch", branch)
		("base_url", base_url)
		("repo", repo)
		("crumbs", crumbs)
		("has_file", !file_or_folder.IsEmpty())
		("last_name", last_name)
		("icon_clock", VividIcon::Get("clock", 14));
	content << RenderGitHubTemplate(bar, "github-code-branch-bar");

	if(mode == "blob" && !file_or_folder.IsEmpty()) {
		// Blob File Viewer
		ValueMap blob = GitHubPlatformService::GetBlob(info.path, file_or_folder);
		String file_content = blob["content"];
		int line_count = blob["lines"];
		String size_str = blob["size_str"];
		bool is_markdown = file_or_folder.EndsWith(".md") || file_or_folder.EndsWith(".markdown") || file_or_folder.EndsWith(".MD");

		ValueArray lines;
		Vector<String> code_lines = Split(file_content, '\n');
		for(int i = 0; i < code_lines.GetCount(); i++) {
			ValueMap row;
			row.Add("num", i + 1);
			row.Add("text", code_lines[i]);
			lines.Add(row);
		}

		Renderer vars;
		vars("file_name", GetFileName(file_or_folder))
			("line_count", line_count)
			("size_str", size_str)
			("is_markdown", is_markdown)
			("owner", owner)
			("repo", repo)
			("file_path", file_or_folder)
			("file_text", file_content)
			("code_box_style", String(is_markdown ? "display:none; " : "") +
				"overflow-x:auto; background:var(--wf-bg-view,#0d1117); font-family:SFMono-Regular,Consolas,'Liberation Mono',Menlo,monospace; font-size:0.82rem; line-height:1.5;")
			("lines", lines);
		content << RenderGitHubTemplate(vars, "github-code-blob");
	}
	else {
		// Directory Tree Browser
		String commit_author = info.last_commit_author.IsEmpty() ? String("sblo") : info.last_commit_author;
		String commit_msg = info.last_commit_msg.IsEmpty() ? String("Update repository files and documentation") : info.last_commit_msg;
		String commit_hash = info.last_commit_hash.IsEmpty() ? String("master") : info.last_commit_hash;
		String commit_time = info.last_commit_time.IsEmpty() ? String("recent") : info.last_commit_time;

		// Up directory link if inside subfolder
		bool has_parent = !file_or_folder.IsEmpty();
		String parent_url = base_url;
		if(has_parent) {
			int slash = file_or_folder.ReverseFind('/');
			if(slash > 0)
				parent_url << "/tree/" << branch << "/" << file_or_folder.Left(slash);
		}

		ValueArray entries;
		Vector<ValueMap> tree = GitHubPlatformService::GetTree(info.path, file_or_folder);
		for(const ValueMap& e : tree) {
			String type = e["type"];
			String rel_path = e["path"];
			String link_url = type == "dir" ? base_url + "/tree/" + branch + "/" + rel_path
			                                : base_url + "/blob/" + branch + "/" + rel_path;
			ValueMap row;
			row.Add("icon", VividIcon::Get(type == "dir" ? "folder" : "file-text", 16));
			row.Add("link", link_url);
			row.Add("name", (String)e["name"]);
			row.Add("msg", (String)e["msg"]);
			row.Add("age", (String)e["age"]);
			entries.Add(row);
		}

		// README rendered box
		String readme_full = AppendFileName(info.path, file_or_folder.IsEmpty() ? "README.md" : file_or_folder + "/README.md");
		String readme_body;
		if(FileExists(readme_full))
			readme_body = LoadFile(readme_full);
		else if(file_or_folder.IsEmpty() && !scraped.IsEmpty() && !((String)scraped["overview"]["readme_snippet"]).IsEmpty())
			readme_body = scraped["overview"]["readme_snippet"];
		else if(file_or_folder.IsEmpty())
			readme_body = "### " + info.name + "\n\nRepository mirrored from `" + info.path + "`.\nAutonomous SweAgent and Aria automation enabled.";

		Renderer vars;
		vars("commit_author", commit_author)
			("commit_msg", commit_msg)
			("commit_hash", commit_hash)
			("commit_time", commit_time)
			("has_parent", has_parent)
			("parent_url", parent_url)
			("icon_corner_left_up", VividIcon::Get("corner-left-up", 16))
			("entries", entries);
		content << RenderGitHubTemplate(vars, "github-code-tree");
		if(!readme_body.IsEmpty()) {
			Renderer readme;
			readme("icon_book", VividIcon::Get("book", 16))
				("readme_text", readme_body);
			content << RenderGitHubTemplate(readme, "github-code-readme");
		}
	}

	// Sidebar About
	Renderer side;
	side("description", info.description.IsEmpty() ? String("No description provided.") : info.description)
		("icon_star", VividIcon::Get("star", 14))
		("stars", info.stars < 0 ? String("not loaded") : AsString(info.stars))
		("icon_fork", VividIcon::Get("git-fork", 14))
		("forks", info.forks < 0 ? String("not loaded") : AsString(info.forks))
		("language", info.language);
	sidebar << RenderGitHubTemplate(side, "github-code-sidebar");
}

END_UPP_NAMESPACE
