#include "DocsPortal.h"

#ifndef _WIN32
#include <stdlib.h>
#include <limits.h>
#endif

NAMESPACE_UPP

// Read-only, allowlisted, traversal-proof. The doc mask: the four fixed base
// names at any depth, or any .md under a docs/ segment. Rendered pages are
// escaped preformatted; nothing here executes or rewrites repository files.

static const int DOC_SIZE_CAP = 512 * 1024;
static const int DOC_WALK_CAP = 500;

static bool BaseNameAllowed(const String& name)
{
	return name == "AGENTS.md" || name == "README.md" || name == "DESIGN.md" || name == "STATUS.md";
}

static bool PathAllowed(const String& rel)
{
	Vector<String> parts = Split(rel, '/');
	if(parts.IsEmpty())
		return false;
	String base = parts[parts.GetCount() - 1];
	if(!base.EndsWith(".md"))
		return false;
	if(BaseNameAllowed(base))
		return true;
	for(int i = 0; i < parts.GetCount() - 1; i++)
		if(parts[i] == "docs")
			return true;
	return false;
}

Vector<DocsRepo> ParseDocsRepos(const String& spec)
{
	Vector<DocsRepo> out;
	Vector<String> items = Split(spec, ',');
	for(const String& item : items) {
		String s = item;
		while(s.GetCount() && (s[0] == ' ' || s[s.GetCount() - 1] == ' '))
			s = TrimBoth(s);
		if(s.IsEmpty())
			continue;
		int eq = s.Find('=');
		if(eq <= 0)
			continue;
		DocsRepo r;
		r.label = s.Left(eq);
		r.root = TrimBoth(s.Mid(eq + 1));
		// An empty or relative root would resolve against the working directory.
		if(r.label.IsEmpty() || r.root.IsEmpty() || !IsFullPath(r.root))
			continue;
		r.root = NormalizePath(r.root);
		out.Add(r);
	}
	return out;
}

String DefaultDocsReposSpec()
{
	return "SkylarkManager=" + GetHomeDirFile("Dev/SkylarkManager")
	     + ",CompanySkylark=" + GetHomeDirFile("Dev/CompanySkylark")
	     + ",AgentIPC=" + GetHomeDirFile("OuluLinux/AgentIPC");
}

// The real path of an existing file or directory, or "" when it cannot be resolved.
// POSIX realpath; on Windows the lexical path stands (see the report: symlink
// containment is not enforced there).
static String CanonicalPath(const String& path)
{
#ifndef _WIN32
	char buf[PATH_MAX];
	if(realpath(~path, buf) == NULL)
		return String();
	return String(buf);
#else
	return NormalizePath(path);
#endif
}

bool ResolveDocFile(const Vector<DocsRepo>& repos, const String& repo_label,
                    const String& rel, String& abs_path, String& error)
{
	error.Clear();
	abs_path.Clear();
	const DocsRepo* repo = NULL;
	for(const DocsRepo& r : repos)
		if(r.label == repo_label) {
			repo = &r;
			break;
		}
	if(!repo) {
		error = "unknown documentation repository";
		return false;
	}
	if(rel.IsEmpty() || rel.Find("..") >= 0) {
		error = "invalid documentation path";
		return false;
	}
	String abs = NormalizePath(AppendFileName(repo->root, rel));
	String root = NormalizePath(repo->root);
	if(!(abs.StartsWith(root) && abs.Mid(root.GetCount(), 1) == "/")) {
		error = "path escapes the repository";
		return false;
	}
	if(!PathAllowed(rel)) {
		error = "not a documentation file";
		return false;
	}
	if(!FileExists(abs)) {
		error = "file not found";
		return false;
	}
	// The lexical checks above do not see symlinks. Canonicalise both the root and
	// the file, then apply the same containment and mask rules to the real path.
	String real = CanonicalPath(abs);
	String real_root = CanonicalPath(root);
	if(real.IsEmpty() || real_root.IsEmpty()) {
		error = "file not found";
		return false;
	}
	if(!(real.StartsWith(real_root) && real.Mid(real_root.GetCount(), 1) == "/")) {
		error = "path escapes the repository";
		return false;
	}
	if(!PathAllowed(real.Mid(real_root.GetCount() + 1))) {
		error = "not a documentation file";
		return false;
	}
	if(GetFileLength(real) > DOC_SIZE_CAP) {
		error = "file exceeds the render size cap";
		return false;
	}
	abs_path = real;
	return true;
}

static void WalkDocs(const String& root, const String& rel, Vector<String>& out, int cap)
{
	if(out.GetCount() >= cap)
		return;
	String dir = rel.IsEmpty() ? root : AppendFileName(root, rel);
	if(!DirectoryExists(dir))
		return;
	Vector<String> subdirs;
	FindFile ff(AppendFileName(dir, "*"));
	while(ff) {
		if(ff.GetName() != "." && ff.GetName() != ".." && !ff.GetName().StartsWith(".")) {
			if(ff.IsFolder())
				subdirs.Add(ff.GetName());
			else {
				String rel_file = rel.IsEmpty() ? ff.GetName() : rel + "/" + ff.GetName();
				if(out.GetCount() < cap && PathAllowed(rel_file))
					out.Add(rel_file);
			}
		}
		ff.Next();
	}
	Sort(subdirs);
	for(const String& name : subdirs) {
		if(out.GetCount() >= cap)
			return;
		WalkDocs(root, rel.IsEmpty() ? name : rel + "/" + name, out, cap);
	}
}

Vector<String> ListDocFiles(const String& root, int cap)
{
	Vector<String> out;
	if(!DirectoryExists(root))
		return out;
	WalkDocs(root, "", out, cap > 0 ? cap : DOC_WALK_CAP);
	return out;
}

int DocFileCount(const String& root, int cap)
{
	return ListDocFiles(root, cap).GetCount();
}

END_UPP_NAMESPACE
