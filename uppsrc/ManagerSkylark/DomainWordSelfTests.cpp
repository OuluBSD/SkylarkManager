#include <Core/Core.h>

NAMESPACE_UPP

// Strict separation (SkylarkManager stays generic): no domain words in the shipped trees. A word hit is a
// case-insensitive match delimited by characters outside [A-Za-z0-9_-], so a compound class such as
// `masthead-hero` is not a hit, while `hero` as a standalone word is. This file is the one place the words
// are listed, so it is skipped by name.

static bool DomainWordCheck(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

static String FindSkylarkRepoRoot(String start)
{
	String dir = NormalizePath(start);
	for(int i = 0; i < 16 && !dir.IsEmpty(); i++) {
		if(DirectoryExists(AppendFileName(dir, "uppsrc/ManagerSkylark")))
			return dir;
		String parent = GetFileFolder(dir);
		if(parent.IsEmpty() || parent == dir)
			break;
		dir = parent;
	}
	return String();
}

static bool IsDomainWordChar(int c)
{
	return IsAlNum(c) || c == '_' || c == '-';
}

static bool ContainsDomainWord(const String& lower, const String& word)
{
	for(int at = lower.Find(word); at >= 0; at = lower.Find(word, at + 1)) {
		bool left = at == 0 || !IsDomainWordChar(lower[at - 1]);
		int end = at + word.GetLength();
		bool right = end >= lower.GetLength() || !IsDomainWordChar(lower[end]);
		if(left && right)
			return true;
	}
	return false;
}

static void CollectScanFiles(const String& dir, Vector<String>& files)
{
	for(FindFile ff(AppendFileName(dir, "*")); ff; ff.Next()) {
		String name = ff.GetName();
		if(name.StartsWith("."))
			continue;
		if(ff.IsFolder())
			CollectScanFiles(ff.GetPath(), files);
		else if(ff.IsFile() && name != "DomainWordSelfTests.cpp")
			files.Add(ff.GetPath());
	}
}

bool RunDomainWordSelfTests()
{
	bool all = true;
	static const char *const words[] = { "poker", "brownbear", "pkr", "holdem", "omaha", "nlth", "obfu", "seat", "hero", "dealer" };

	String root = FindSkylarkRepoRoot(GetCurrentDirectory());
	if(root.IsEmpty())
		root = FindSkylarkRepoRoot(GetFileFolder(GetExeFilePath()));
	static const char *const trees[] = { "uppsrc", "upptst", "examples", "docs" };
	bool roots_found = !root.IsEmpty();
	for(const char *tree : trees)
		roots_found = roots_found && DirectoryExists(AppendFileName(root, tree));
	all &= DomainWordCheck(roots_found, "domain words: uppsrc, upptst, examples and docs are found under the repository root");
	if(!roots_found)
		return false;

	Vector<String> files;
	for(const char *tree : trees)
		CollectScanFiles(AppendFileName(root, tree), files);

	int scanned = 0;
	int hits = 0;
	for(const String& path : files) {
		String text = LoadFile(path);
		if(text.Find('\0') >= 0)
			continue; // binary
		scanned++;
		String lower = ToLower(text);
		for(const char *word : words) {
			if(ContainsDomainWord(lower, word)) {
				hits++;
				Cerr() << "FAILED: domain word \"" << word << "\" in " << path << "\n";
			}
		}
	}
	all &= DomainWordCheck(hits == 0 && scanned > 0,
	        Format("domain words: zero hits in uppsrc, upptst, examples and docs (%d text files scanned)", scanned));
	return all;
}

END_UPP_NAMESPACE
