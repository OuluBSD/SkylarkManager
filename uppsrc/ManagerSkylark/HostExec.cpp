#include "HostExec.h"

NAMESPACE_UPP

namespace HostExec {

static bool AllChars(const String& s, int max_len, bool (*allowed)(int c))
{
	if(s.IsEmpty() || s.GetCount() > max_len) return false;
	for(int i = 0; i < s.GetCount(); i++)
		if(!allowed((byte)s[i])) return false;
	return true;
}

static bool IsAlnum(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }
static bool IsNameChar(int c) { return IsAlnum(c) || c == '.' || c == '_' || c == '-'; }
static bool IsHostChar(int c) { return IsAlnum(c) || c == '.' || c == '-'; }
static bool IsPathChar(int c) { return IsNameChar(c) || c == '/' || c == '~'; }
static bool IsGitTokenChar(int c) { return IsAlnum(c) || c == '.' || c == '_' || c == '/' || c == '=' || c == '@' ||
                                          c == '%' || c == '+' || c == ':' || c == ',' || c == '~' || c == '^' || c == '{' || c == '}' || c == '-'; }
static bool IsHexChar(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static bool IsMethodChar(int c) { return IsAlnum(c) || c == '_'; }
static bool IsPackageChar(int c) { return IsNameChar(c) || c == '/'; }
static bool IsFlagLetter(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

bool SafeUser(const String& user)
{
	return AllChars(user, 64, IsNameChar) && user[0] != '-';
}

bool SafeHost(const String& host)
{
	return AllChars(host, 253, IsHostChar) && host[0] != '-' && host[0] != '.';
}

bool SafeRemotePath(const String& path)
{
	return AllChars(path, 512, IsPathChar) && path[0] != '-' && path.Find("..") < 0;
}

bool SafeRepoName(const String& name)
{
	return AllChars(name, 128, IsNameChar) && name[0] != '.' && name[0] != '-' && name.Find("..") < 0;
}

bool SafePackageName(const String& name)
{
	return AllChars(name, 256, IsPackageChar) && name[0] != '-' && name.Find("..") < 0;
}

bool SafeBuildMethod(const String& method)
{
	return AllChars(method, 32, IsMethodChar);
}

bool SafeHash(const String& hash)
{
	return hash.GetCount() >= 7 && hash.GetCount() <= 40 && AllChars(hash, 40, IsHexChar);
}

// Options each git subcommand may take. Anything else that starts with '-' is
// refused, so the set of options a request can reach git with is fixed here.
static bool IsDigits(const String& s, int from)
{
	if(from >= s.GetCount())
		return false;
	for(int i = from; i < s.GetCount(); i++)
		if(s[i] < '0' || s[i] > '9')
			return false;
	return true;
}

static bool GitOptionAllowed(const String& sub, const String& tok)
{
	if(tok == "--")
		return true;
	static const char *const status[] = { "--short", "-s", "--branch", "-b", "--porcelain", "--no-color", NULL };
	static const char *const log[] = { "--oneline", "--no-color", "--stat", "--decorate", "--no-decorate",
	                                   "--date-order", "--no-patch", NULL };
	static const char *const show[] = { "--oneline", "--no-color", "--stat", "--name-only", "--no-patch", "-s", NULL };
	static const char *const diff[] = { "--no-color", "--stat", "--name-only", "--cached", "--staged", NULL };
	static const char *const branch[] = { "--list", "-a", "-r", "-v", "--show-current", "--no-color", NULL };
	static const char *const rev_parse[] = { "--short", "--abbrev-ref", "--verify", "--show-toplevel",
	                                         "--is-inside-work-tree", NULL };
	static const char *const fetch[] = { "--prune", "--tags", "--no-tags", "--quiet", "-q", "--all", NULL };
	static const char *const pull[] = { "--ff-only", "--quiet", "-q", NULL };
	const char *const *allowed = NULL;
	if(sub == "status") allowed = status;
	else if(sub == "log") allowed = log;
	else if(sub == "show") allowed = show;
	else if(sub == "diff") allowed = diff;
	else if(sub == "branch") allowed = branch;
	else if(sub == "rev-parse") allowed = rev_parse;
	else if(sub == "fetch") allowed = fetch;
	else if(sub == "pull") allowed = pull;
	if(!allowed)
		return false;
	for(const char *const *o = allowed; *o; o++)
		if(tok == *o)
			return true;
	// Log takes a count: -5, -n5, --max-count=5.
	if(sub == "log") {
		if(tok.StartsWith("-n") && IsDigits(tok, 2))
			return true;
		if(tok.StartsWith("--max-count=") && IsDigits(tok, 12))
			return true;
		if(tok.StartsWith("-") && IsDigits(tok, 1))
			return true;
	}
	return false;
}

bool SafeGitArgs(const String& args, Vector<String>& tokens)
{
	static const char *const subcommands[] = { "status", "log", "branch", "show", "diff", "rev-parse", "fetch", "pull" };
	tokens.Clear();
	Vector<String> parts = Split(args, ' ', false);
	for(const String& p : parts) {
		if(p.IsEmpty()) continue;
		if(tokens.GetCount() >= 16 || !AllChars(p, 256, IsGitTokenChar)) {
			tokens.Clear();
			return false;
		}
		tokens.Add(p);
	}
	if(tokens.IsEmpty()) {
		tokens.Clear();
		return false;
	}
	bool known = false;
	for(const char *s : subcommands)
		if(tokens[0] == s)
			known = true;
	if(!known) {
		tokens.Clear();
		return false;
	}
	// Options must be on the subcommand's allowlist. Other tokens are revisions or
	// paths: they never start with '-' (AllChars allows it, so check here).
	for(int i = 1; i < tokens.GetCount(); i++) {
		const String& t = tokens[i];
		if(t[0] == '-' && !GitOptionAllowed(tokens[0], t)) {
			tokens.Clear();
			return false;
		}
	}
	return true;
}

bool SafeBuildFlags(const String& flags, Vector<String>& tokens)
{
	tokens.Clear();
	Vector<String> parts = Split(flags, ' ', false);
	for(const String& p : parts) {
		if(p.IsEmpty()) continue;
		if(tokens.GetCount() >= 8 || p.GetCount() < 2 || p.GetCount() > 16 || p[0] != '-') {
			tokens.Clear();
			return false;
		}
		for(int i = 1; i < p.GetCount(); i++)
			if(!IsFlagLetter((byte)p[i])) {
				tokens.Clear();
				return false;
			}
		tokens.Add(p);
	}
	return true;
}

Vector<String> SshArgs(int port, const String& user, const String& host, int connect_timeout)
{
	Vector<String> args;
	args.Add("-o"); args.Add("BatchMode=yes");
	args.Add("-o"); args.Add("ConnectTimeout=" + AsString(connect_timeout));
	args.Add("-p"); args.Add(AsString(port));
	args.Add(user + "@" + host);
	return args;
}

String Run(const char *program, const Vector<String>& args, bool merge_stderr, const char *cwd)
{
	LocalProcess p;
	bool started = merge_stderr ? p.Start(program, args, NULL, cwd) : p.Start2(program, args, NULL, cwd);
	if(!started) return String();
	String out;
	p.Finish(out);
	return out;
}

}

END_UPP_NAMESPACE
