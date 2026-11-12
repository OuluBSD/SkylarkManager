#include <Core/Core.h>
#include "DocsPortal.h"

#ifndef _WIN32
#include <unistd.h>
#endif

NAMESPACE_UPP

// Pure resolver checks for the docs portal (task 024 item 2): the repo spec
// parser, the doc-file mask and traversal refusals, and the walk. No HTTP.

static bool Check(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

static void WriteFile(const String& path, const String& text)
{
	RealizeDirectory(GetFileFolder(path));
	SaveFile(path, text);
}

static bool Contains(const Vector<String>& v, const char *item)
{
	for(const String& s : v)
		if(s == item)
			return true;
	return false;
}

bool RunDocsPortalSelfTests(const String& scratch_dir)
{
	bool all = true;
	const String root = AppendFileName(scratch_dir, "repo-a");

	// Spec parser: malformed parts are ignored, the label and path split on the first '='.
	Vector<DocsRepo> parsed = ParseDocsRepos(" a=/x , b=/y ,,bad,=/z, d=/w");
	all &= Check(parsed.GetCount() == 3 && parsed[0].label == "a" && parsed[1].label == "b" &&
	             parsed[2].label == "d" && parsed[0].root == NormalizePath("/x") &&
	             parsed[2].root == NormalizePath("/w"),
	             "docs: the spec keeps well-formed parts and ignores the malformed ones");
	all &= Check(ParseDocsRepos("c=, e=relative/path, f=../up").IsEmpty(),
	             "docs: an empty or relative root is refused, not resolved against the working directory");

	// The fixture repo: allowed docs, a non-doc file, a dot-directory, a dot-file.
	WriteFile(AppendFileName(root, "AGENTS.md"), "agents");
	WriteFile(AppendFileName(root, "README.md"), "readme");
	WriteFile(AppendFileName(root, "NOTES.md"), "not a base name at the root");
	WriteFile(AppendFileName(root, "src/main.cpp"), "int main() {}");
	WriteFile(AppendFileName(root, "docs/guide.md"), "guide");
	WriteFile(AppendFileName(root, "docs/deep/x.md"), "deep");
	WriteFile(AppendFileName(root, "docs/.draft.md"), "dot-file");
	WriteFile(AppendFileName(root, ".hidden/AGENTS.md"), "dot-dir");

	Vector<DocsRepo> repos = ParseDocsRepos("repo-a=" + root);
	String abs, err;
	all &= Check(ResolveDocFile(repos, "repo-a", "README.md", abs, err) && abs.StartsWith(NormalizePath(root)),
	             "docs: a root base-name doc resolves inside the repo");
	all &= Check(ResolveDocFile(repos, "repo-a", "docs/deep/x.md", abs, err),
	             "docs: a nested file under docs/ resolves");
	all &= Check(!ResolveDocFile(repos, "repo-a", "src/main.cpp", abs, err) && err == "not a documentation file",
	             "docs: a non-doc file is refused by the mask");
	all &= Check(!ResolveDocFile(repos, "repo-a", "NOTES.md", abs, err) && err == "not a documentation file",
	             "docs: a .md that is no base name at the root is refused");
	all &= Check(!ResolveDocFile(repos, "repo-a", "docs/../../etc/passwd", abs, err) && err == "invalid documentation path",
	             "docs: a '..' path is refused");
	all &= Check(!ResolveDocFile(repos, "other", "README.md", abs, err) && err == "unknown documentation repository",
	             "docs: an unknown repository label is refused");
	all &= Check(!ResolveDocFile(repos, "repo-a", "docs/none.md", abs, err) && err == "file not found",
	             "docs: a missing doc file is reported as not found");
	WriteFile(AppendFileName(root, "docs/big.md"), String('x', 512 * 1024 + 1));
	all &= Check(!ResolveDocFile(repos, "repo-a", "docs/big.md", abs, err) &&
	             err == "file exceeds the render size cap",
	             "docs: a file over the size cap is refused");

	// The walk cap holds inside one directory: 10 allowed files, cap 3.
	String flat = AppendFileName(scratch_dir, "flat");
	for(int i = 0; i < 10; i++)
		WriteFile(AppendFileName(flat, "docs/f" + AsString(i) + ".md"), "x");
	all &= Check(ListDocFiles(flat, 3).GetCount() == 3, "docs: the walk cap holds inside one directory");

	// Symlinks: an escape is refused by its real path; links that stay inside
	// the repo follow the same mask rules as their target.
#ifndef _WIN32
	String outside = AppendFileName(scratch_dir, "outside.md");
	WriteFile(outside, "secret");
	String link_root = AppendFileName(scratch_dir, "repo-link");
	WriteFile(AppendFileName(link_root, "docs/placeholder.md"), "x");
	WriteFile(AppendFileName(link_root, "src/main.cpp"), "int main() {}");
	symlink(~outside, ~AppendFileName(link_root, "docs/escape.md"));
	symlink(~AppendFileName(link_root, "docs/placeholder.md"), ~AppendFileName(link_root, "docs/alias.md"));
	symlink(~AppendFileName(link_root, "src/main.cpp"), ~AppendFileName(link_root, "docs/sneak.md"));
	Vector<DocsRepo> link_repos = ParseDocsRepos("lk=" + link_root);
	all &= Check(!ResolveDocFile(link_repos, "lk", "docs/escape.md", abs, err) && err == "path escapes the repository",
	             "docs: a symlink that escapes the repository is refused by its real path");
	all &= Check(ResolveDocFile(link_repos, "lk", "docs/alias.md", abs, err),
	             "docs: a symlink that stays inside the repository resolves to its doc target");
	all &= Check(!ResolveDocFile(link_repos, "lk", "docs/sneak.md", abs, err) && err == "not a documentation file",
	             "docs: a symlink to a non-doc file inside the repository is refused by the mask");
#endif

	// The walk: allowed docs only, dot-directories and dot-files skipped.
	Vector<String> files = ListDocFiles(root, 0);
	all &= Check(Contains(files, "AGENTS.md") && Contains(files, "README.md") &&
	             Contains(files, "docs/guide.md") && Contains(files, "docs/deep/x.md") &&
	             Contains(files, "docs/big.md"),
	             "docs: the walk lists every allowed doc");
	all &= Check(!Contains(files, "NOTES.md") && !Contains(files, "src/main.cpp") &&
	             !Contains(files, "docs/.draft.md") && !Contains(files, ".hidden/AGENTS.md"),
	             "docs: the walk skips non-doc files and dotfiles");
	all &= Check(DocFileCount(root, 0) == files.GetCount(), "docs: the count matches the walk");
	all &= Check(ListDocFiles(AppendFileName(scratch_dir, "absent"), 0).IsEmpty(),
	             "docs: a missing root lists nothing");

	return all;
}

END_UPP_NAMESPACE
