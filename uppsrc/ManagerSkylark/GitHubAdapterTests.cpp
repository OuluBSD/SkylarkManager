#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include <GitHubForge/GitHubForge.h>

NAMESPACE_UPP

// Synthetic checks for the GitHub issue path. A fake gh script stands in for
// the CLI through GHFORGE_GH: it answers the adapter's argv shapes with canned
// JSON, so no network or credentials are involved.
static const char *kFakeGhScript = R"SH(#!/bin/sh
# Fake gh for the GitHub adapter self-test; unexpected argv fails loudly.
case "$*" in
	*"repos/acme/tools/issues?state=all"*)
		echo '[{"number":3,"title":"Open one","state":"open","user":{"login":"alice"},"created_at":"2026-10-30T18:00:00Z","comments":2},{"number":4,"title":"A pull request","state":"open","user":{"login":"bob"},"created_at":"2026-10-01T00:00:00Z","comments":0,"pull_request":{"url":"x"}}]' ;;
	*"repos/acme/tools/issues/3/comments"*)
		echo '[{"id":77,"user":{"login":"carol"},"body":"hello","created_at":"2026-10-31T10:00:00Z"}]' ;;
	*"repos/acme/tools/issues/3"*)
		echo '{"number":3,"title":"Open one","body":"Body text","state":"open","user":{"login":"alice"},"created_at":"2026-10-30T18:00:00Z","comments":1}' ;;
	*"-X POST repos/acme/tools/issues "*)
		echo '{"number":9,"title":"New","state":"open","user":{"login":"alice"},"created_at":"2026-11-01T08:00:00Z","comments":0}' ;;
	*)
		echo "unexpected gh arguments: $*" >&2
		exit 2 ;;
esac
)SH";

bool GitHubPlatformService::RunIssueAdapterSyntheticTests(const String& scratch_dir)
{
	String gh = AppendFileName(scratch_dir, "fake-gh.sh");
	if(!SaveFile(gh, kFakeGhScript)) {
		Cerr() << "FAILED: cannot write fake gh fixture\n";
		return false;
	}
	Sys("chmod", {"+x", gh});
	SetEnv("GHFORGE_GH", gh);

	String unavailable;
	Vector<ValueMap> issues = GitHubPlatformService::GetLiveIssues("acme", "tools", &unavailable);
	if(!unavailable.IsEmpty() || issues.GetCount() != 1 || (int)issues[0]["number"] != 3 ||
	   (String)issues[0]["title"] != "Open one" || (String)issues[0]["state"] != "OPEN" ||
	   (String)issues[0]["author"] != "alice" || (String)issues[0]["created_at"] != "2026-10-30" ||
	   (int)issues[0]["comments"] != 2) {
		Cerr() << "FAILED: issue listing did not map the adapter result (pull requests must be filtered): "
		       << unavailable << " rows=" << issues.GetCount() << "\n";
		return false;
	}
	Cout() << "ok    GitHub issue list maps the adapter result and filters pull requests\n";

	ValueMap detail = GitHubPlatformService::GetLiveIssueDetail("acme", "tools", 3, &unavailable);
	ValueArray thread = detail["comments"];
	if(!unavailable.IsEmpty() || (String)detail["title"] != "Open one" || (String)detail["state"] != "OPEN" ||
	   (String)detail["author"]["login"] != "alice" || thread.GetCount() != 1 ||
	   (String)thread[0]["author"]["login"] != "carol" || (String)thread[0]["body"] != "hello") {
		Cerr() << "FAILED: issue detail did not map the adapter result\n";
		return false;
	}
	Cout() << "ok    GitHub issue detail maps the issue and its comment thread\n";

	String created = GitHubPlatformService::CreateIssue("acme", "tools", "New", "Body");
	if(!created.StartsWith("https://github.com/acme/tools/issues/9")) {
		Cerr() << "FAILED: issue creation did not return the created issue URL: " << created << "\n";
		return false;
	}
	Cout() << "ok    GitHub issue creation returns the created issue URL\n";

	SetEnv("GHFORGE_GH", AppendFileName(scratch_dir, "no-such-gh"));
	unavailable.Clear();
	issues = GitHubPlatformService::GetLiveIssues("acme", "tools", &unavailable);
	if(!issues.IsEmpty() || unavailable.IsEmpty()) {
		Cerr() << "FAILED: a missing gh executable must report unavailable, not fabricate issues\n";
		return false;
	}
	Cout() << "ok    GitHub issue list reports unavailable when gh cannot run\n";

	SetEnv("GHFORGE_GH", "");
	return true;
}

END_UPP_NAMESPACE
