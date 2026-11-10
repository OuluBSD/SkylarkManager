#include "HostExec.h"

NAMESPACE_UPP

// Synthetic checks for the git argument allowlist (brief 13 item 3). They need no
// repository and run no git.

static bool Check(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

bool RunHostExecSelfTests()
{
	bool all = true;
	Vector<String> tokens;

	const char *allowed[] = {
		"status", "status --short", "status -s --branch", "log --oneline -n5", "log -5 --stat",
		"log --max-count=3", "log -- src/file.cpp", "branch --show-current", "branch -a",
		"diff --cached --name-only", "rev-parse --short HEAD", "rev-parse --abbrev-ref HEAD",
		"fetch --prune origin", "pull --ff-only", "show --stat HEAD~1", "log -1 HEAD^",
	};
	for(const char *a : allowed)
		all &= Check(HostExec::SafeGitArgs(a, tokens) && tokens.GetCount() >= 1,
		             String("git allowlist accepts: ") + a);

	const char *refused[] = {
		"", "   ", "push origin", "commit -m x", "reset --hard",
		"log --exec=sh", "status -c core.pager=sh", "log -c x", "log --output=/tmp/x",
		"diff --ext-diff", "show --textconv", "fetch --upload-pack=sh", "pull --receive-pack=sh",
		"status --git-dir=/x", "status --work-tree=/x", "branch -D main", "rev-parse --git-dir",
		"log --format=%H", "log --since=2020-01-01", "log -5x", "status ;rm", "status $(id)",
		"status `id`", "status 'x'", "status \"x\"", "status |x", "-c core.pager=sh status",
		"--exec=sh status",
	};
	for(const char *r : refused)
		all &= Check(!HostExec::SafeGitArgs(r, tokens) && tokens.IsEmpty(),
		             String("git allowlist refuses: ") + r);

	return all;
}

END_UPP_NAMESPACE
