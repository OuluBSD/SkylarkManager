#include "AtlasImport.h"

#ifndef _WIN32
#include <limits.h>
#include <stdlib.h>
#endif

NAMESPACE_UPP

// The text of a repository file is read live from the source root, and only for the file types that are code or
// documents. A path must resolve, through symlinks as well, to a file inside the root. The rules mirror the docs
// portal (WebDocs/DocsPortal.cpp), with a code mask in place of the doc mask.

static const int SOURCE_SIZE_CAP = 512 * 1024;

static bool CodeMask(const String& rel)
{
	static const char *exts[] = { ".cpp", ".h", ".hpp", ".c", ".py", ".upp", ".md", ".txt", ".sh", ".cfg", ".json", ".witz", NULL };
	bool ext_ok = false;
	for(int i = 0; exts[i]; i++)
		if(rel.EndsWith(exts[i]))
			ext_ok = true;
	if(!ext_ok)
		return false;
	// No directory or file name may start with '.': that keeps .git and any other dot-entry out.
	for(int i = 0; i < rel.GetCount(); i++)
		if((i == 0 || rel[i - 1] == '/') && rel[i] == '.')
			return false;
	return true;
}

// The real path of an existing file, or "" when it cannot be resolved.
static String CanonicalSourcePath(const String& path)
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

bool AtlasResolveSource(const String& root, const String& rel, String& abs_path, String& error)
{
	error.Clear();
	abs_path.Clear();
	if(root.IsEmpty() || !IsFullPath(root)) {
		error = "source root is not registered";
		return false;
	}
	if(!AtlasSafeSourcePath(rel)) {
		error = "invalid source path";
		return false;
	}
	if(!CodeMask(rel)) {
		error = "not a source file";
		return false;
	}
	String abs = NormalizePath(AppendFileName(root, rel));
	String lex_root = NormalizePath(root);
	if(!(abs.StartsWith(lex_root) && abs.Mid(lex_root.GetCount(), 1) == "/")) {
		error = "path escapes the source root";
		return false;
	}
	if(!FileExists(abs)) {
		error = "file not found";
		return false;
	}
	// The lexical checks do not see symlinks: canonicalise the root and the file, then repeat the containment and the
	// mask on the real path.
	String real = CanonicalSourcePath(abs);
	String real_root = CanonicalSourcePath(lex_root);
	if(real.IsEmpty() || real_root.IsEmpty()) {
		error = "file not found";
		return false;
	}
	if(!(real.StartsWith(real_root) && real.Mid(real_root.GetCount(), 1) == "/")) {
		error = "path escapes the source root";
		return false;
	}
	if(!CodeMask(real.Mid(real_root.GetCount() + 1))) {
		error = "not a source file";
		return false;
	}
	if(GetFileLength(real) > SOURCE_SIZE_CAP) {
		error = "file exceeds the size cap";
		return false;
	}
	abs_path = real;
	return true;
}

bool AtlasReadSourceLines(const String& root, const String& rel, Vector<String>& lines, String& error)
{
	String abs;
	if(!AtlasResolveSource(root, rel, abs, error))
		return false;
	String text = LoadFile(abs);
	for(int i = 0; i < text.GetCount(); i++)
		if(text[i] == '\0') {
			error = "file is not text";
			return false;
		}
	lines = AtlasSplitLines(text);
	return true;
}

// The record as a viewer may see it: spans and notes for every reader, the text of the edits for an editor only.
AtlasFile AtlasFileForViewer(const AtlasFile& f, bool editor)
{
	AtlasFile g;
	g.id = f.id;
	g.source = f.source;
	g.path = f.path;
	g.kind = f.kind;
	g.commit = f.commit;
	for(const AtlasFileSpan& sp : f.spans) {
		AtlasFileSpan q = sp;
		g.spans.Add(pick(q));
	}
	for(const AtlasFileEdit& e : f.edits) {
		AtlasFileEdit q;
		q.vid = e.vid;
		q.element = e.element;
		q.start = e.start;
		q.end = e.end;
		q.text = editor ? e.text : String();
		q.note = e.note;
		g.edits.Add(pick(q));
	}
	return g;
}

// The text of a file as it stands at version vid: the base with every edit of that version and the ones before it.
bool AtlasFileTextAt(const AtlasFile& f, const Vector<String>& base, const AtlasFamily& fam, const String& vid,
                     Vector<String>& out, String& error)
{
	int at = fam.Find(vid);
	if(at < 0) {
		error = "version " + vid + " is not in the family";
		return false;
	}
	Vector<AtlasFileEdit> upto;
	for(const AtlasFileEdit& e : f.edits) {
		int ei = fam.Find(e.vid);
		if(ei >= 0 && ei <= at)
			upto.Add(e);
	}
	return AtlasApplyEdits(base, upto, out, error);
}

END_UPP_NAMESPACE
