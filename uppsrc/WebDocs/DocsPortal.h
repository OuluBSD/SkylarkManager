#ifndef _WebDocs_DocsPortal_h_
#define _WebDocs_DocsPortal_h_

#include <Core/Core.h>

NAMESPACE_UPP

// The cross-project docs portal (design 020, task 021): a read-only window
// over a fixed allowlist of local repositories' documentation. The resolver
// is pure and traversal-proof; rendering is escaped preformatted markdown.

struct DocsRepo : Moveable<DocsRepo> {
	String label;
	String root;
};

// "label=path,label=path"; empty parts ignored.
Vector<DocsRepo> ParseDocsRepos(const String& spec);

// The default allowlist, used when --docs-repos is not given.
String DefaultDocsReposSpec();

// A file is renderable when it resolves inside the repo root, matches the
// doc mask (AGENTS.md, README.md, DESIGN.md, STATUS.md at any depth, or
// docs/**.md), exists, and is within the size cap.
bool ResolveDocFile(const Vector<DocsRepo>& repos, const String& repo_label,
                    const String& rel, String& abs_path, String& error);

// Relative doc file paths under a repo root, sorted; the walk is bounded.
Vector<String> ListDocFiles(const String& root, int cap);

int DocFileCount(const String& root, int cap);

END_UPP_NAMESPACE

#endif
