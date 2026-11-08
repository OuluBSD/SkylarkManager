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

bool SafeGitArgs(const String& args, Vector<String>& tokens)
{
	static const char *const subcommands[] = { "status", "log", "branch", "show", "diff", "rev-parse", "fetch", "pull" };
	static const char *const refused[] = { "-c", "--config-env", "--exec", "--upload-pack", "--receive-pack",
	                                       "--output", "--ext-diff", "--textconv" };
	tokens.Clear();
	Vector<String> parts = Split(args, ' ', false);
	for(const String& p : parts) {
		if(p.IsEmpty()) continue;
		if(tokens.GetCount() >= 16 || !AllChars(p, 256, IsGitTokenChar)) return false;
		for(const char *r : refused)
			if(p == r || p.StartsWith(String(r) + "=")) return false;
		tokens.Add(p);
	}
	if(tokens.IsEmpty()) return false;
	for(const char *s : subcommands)
		if(tokens[0] == s) return true;
	tokens.Clear();
	return false;
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
