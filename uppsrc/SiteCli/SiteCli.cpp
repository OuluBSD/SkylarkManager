#include "SiteCli.h"

#include <Core/Ext/TOON.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

NAMESPACE_UPP

static int IndexOfName(const Vector<String>& names, const String& name)
{
	for(int i = 0; i < names.GetCount(); i++)
		if(names[i] == name)
			return i;
	return -1;
}

static bool IsIntText(const String& s)
{
	int i = s.GetCount() && s[0] == '-' ? 1 : 0;
	if(i >= s.GetCount())
		return false;
	for(; i < s.GetCount(); i++)
		if(!IsDigit(s[i]))
			return false;
	return true;
}

static bool IsNumberText(const String& s)
{
	int i = s.GetCount() && s[0] == '-' ? 1 : 0;
	bool dot = false;
	bool digit = false;
	for(; i < s.GetCount(); i++) {
		if(IsDigit(s[i]))
			digit = true;
		else if(s[i] == '.' && !dot)
			dot = true;
		else
			return false;
	}
	return digit;
}

bool SiteCliBuildRequest(const ValueMap& command, const Vector<String>& words,
                         SiteCliRequest& out, String& err)
{
	String method = command["method"];
	String api_path = command["api_path"];
	ValueArray specs = command["args"];
	int n = specs.GetCount();

	Vector<String> names, types;
	Vector<bool> required;
	for(int i = 0; i < n; i++) {
		ValueMap a = specs[i];
		names.Add((String)a["name"]);
		types.Add((String)a["type"]);
		required.Add((bool)a["required"]);
	}

	Vector<String> value;
	Vector<bool> set;
	for(int i = 0; i < n; i++) {
		value.Add();
		set.Add(false);
	}

	for(const String& w : words) {
		int eq = w.Find('=');
		if(eq > 0) {
			int k = IndexOfName(names, w.Left(eq));
			if(k < 0) {
				err = "unknown argument '" + w.Left(eq) + "'";
				return false;
			}
			value[k] = w.Mid(eq + 1);
			set[k] = true;
		}
		else {
			int k = 0;
			while(k < n && set[k])
				k++;
			if(k >= n) {
				err = "too many arguments; unexpected '" + w + "'";
				return false;
			}
			value[k] = w;
			set[k] = true;
		}
	}

	for(int i = 0; i < n; i++) {
		if(required[i] && !set[i]) {
			err = "missing required argument '" + names[i] + "'";
			return false;
		}
		if(set[i] && types[i] == "int" && !IsIntText(value[i])) {
			err = "argument '" + names[i] + "' must be an integer";
			return false;
		}
		if(set[i] && types[i] == "number" && !IsNumberText(value[i])) {
			err = "argument '" + names[i] + "' must be a number";
			return false;
		}
		// A "text" argument is multi-line: text=@path reads the value from a file.
		if(set[i] && types[i] == "text" && value[i].StartsWith("@")) {
			String path = value[i].Mid(1);
			if(!FileExists(path)) {
				err = "argument '" + names[i] + "': cannot read " + path;
				return false;
			}
			value[i] = LoadFile(path);
		}
	}

	// The first N declared arguments fill the N "*" segments of the route, in order.
	Vector<bool> in_path;
	for(int i = 0; i < n; i++)
		in_path.Add(false);
	String path;
	int next = 0;
	Vector<String> segs = Split(api_path, '/');
	for(int s = 0; s < segs.GetCount(); s++) {
		if(s)
			path.Cat('/');
		if(segs[s] != "*") {
			path.Cat(segs[s]);
			continue;
		}
		if(next >= n) {
			err = "route " + api_path + " needs a path argument that the descriptor does not declare";
			return false;
		}
		if(!set[next]) {
			err = "missing path argument '" + names[next] + "'";
			return false;
		}
		path.Cat(UrlEncode(value[next]));
		in_path[next] = true;
		next++;
	}

	out.method = method;
	out.url_path = path;
	out.body.Clear();
	out.content_type.Clear();

	if(method == "GET") {
		for(int i = 0; i < n; i++)
			if(set[i] && !in_path[i]) {
				out.url_path.Cat(out.url_path.Find('?') < 0 ? '?' : '&');
				out.url_path << names[i] << '=' << UrlEncode(value[i]);
			}
		err.Clear();
		return true;
	}

	if(api_path.StartsWith("api/")) {
		ValueMap body;
		for(int i = 0; i < n; i++)
			if(set[i] && !in_path[i]) {
				if(types[i] == "int")
					body.Add(names[i], (int64)StrInt64(value[i]));
				else if(types[i] == "number")
					body.Add(names[i], StrDbl(value[i]));
				else if(value[i].StartsWith("{") || value[i].StartsWith("["))
					body.Add(names[i], ParseJSON(value[i]));  // a nested tree, e.g. a Science article
				else
					body.Add(names[i], value[i]);
			}
		out.body = AsJSON(body);
		out.content_type = "application/json";
	}
	else {
		for(int i = 0; i < n; i++)
			if(set[i] && !in_path[i]) {
				if(!out.body.IsEmpty())
					out.body.Cat('&');
				out.body << names[i] << '=' << UrlEncode(value[i]);
			}
		out.content_type = "application/x-www-form-urlencoded";
	}
	err.Clear();
	return true;
}

Vector<String> SiteCliSuggest(const String& typed, const Vector<String>& names)
{
	Vector<String> out;
	if(typed.IsEmpty())
		return out;
	for(const String& n : names) {
		int common = 0;
		while(common < typed.GetCount() && common < n.GetCount() && typed[common] == n[common])
			common++;
		if(n.Find(typed) >= 0 || typed.Find(n) >= 0 || common >= (typed.GetCount() < 3 ? typed.GetCount() : 3))
			out.Add(n);
	}
	return out;
}

// ---------------------------------------------------------------------------
// Cookie jar: "site<TAB>field<TAB>value" lines. The file holds a session
// credential, so it is written 0600.

static void JarLoad(const String& file, VectorMap<String, String>& jar)
{
	if(file.IsEmpty() || !FileExists(file))
		return;
	for(const String& line : Split(LoadFile(file), '\n')) {
		Vector<String> p = Split(line, '\t');
		if(p.GetCount() == 3)
			jar.GetAdd(p[0] + "\t" + p[1]) = p[2];
	}
}

static bool JarSave(const String& file, const VectorMap<String, String>& jar)
{
	if(file.IsEmpty())
		return true;
	String data;
	for(int i = 0; i < jar.GetCount(); i++) {
		if(jar[i].IsEmpty())
			continue;
		data << jar.GetKey(i) << '\t' << jar[i] << '\n';
	}
	if(!SaveFile(file, data))
		return false;
	chmod(file, 0600);
	return true;
}

static String JarGet(const VectorMap<String, String>& jar, const String& site, const String& field)
{
	int i = jar.Find(site + "\t" + field);
	return i >= 0 ? jar[i] : String();
}

static void JarSet(VectorMap<String, String>& jar, const String& site, const String& field, const String& value)
{
	jar.GetAdd(site + "\t" + field) = value;
}

// The bearer token for a request: --bearer <file> wins, else the token the jar kept at login.
static String BearerFor(const SiteCliOptions& opts, const VectorMap<String, String>& jar, const String& site)
{
	if(opts.bearer_file.GetCount())
		return TrimBoth(LoadFile(opts.bearer_file));
	return JarGet(jar, site, "bearer");
}

// ---------------------------------------------------------------------------
// HTTP: one request, no redirects, the site's own cookie and CSRF token.

struct SiteCliReply {
	int    status = 0;
	String body;
	String location;    // redirect target of a 3xx reply, empty otherwise
	String set_cookie;
};

static String Base(const String& url)
{
	String s = url;
	while(s.GetCount() && s[s.GetCount() - 1] == '/')
		s.Trim(s.GetCount() - 1);
	return s;
}

static bool Send(const String& method, const String& url, const String& body, const String& content_type,
                 const String& cookie_name, const String& cookie_value, const String& csrf, const String& bearer,
                 SiteCliReply& reply, String& err)
{
	HttpRequest http(url);
	http.MaxRedirect(0);
	http.RequestTimeout(30000);
	http.Accept("application/json");
	if(csrf.GetCount())
		http.AddHeaders("x-wf-csrf: " + csrf + "\r\n");
	if(bearer.GetCount())
		http.AddHeaders("Authorization: Bearer " + bearer + "\r\n");
	if(cookie_name.GetCount())
		http.Cookie(cookie_name, cookie_value);
	if(method == "POST") {
		http.ContentType(content_type);
		http.Post(body);
	}
	http.Execute();
	if(http.IsSocketError()) {
		err = http.GetErrorDesc();
		return false;
	}
	reply.status = http.GetStatusCode();
	reply.body = http.GetContent();
	reply.location = http.GetHeader("location");
	reply.set_cookie = http.GetHeader("set-cookie");
	return true;
}

// "name=value; Path=/; ..." -> name and value.
static bool ParseSetCookie(const String& sc, String& name, String& value)
{
	int eq = sc.Find('=');
	if(eq <= 0)
		return false;
	name = TrimBoth(sc.Left(eq));
	String rest = sc.Mid(eq + 1);
	int semi = rest.Find(';');
	value = semi >= 0 ? rest.Left(semi) : rest;
	return true;
}

static void PrintReply(const SiteCliOptions& opts, const String& body)
{
	Value v = ParseJSON(body);
	if(opts.toon && !IsNull(v))
		Cout() << AsTOON(v) << "\n";
	else if(!IsNull(v))
		Cout() << AsJSON(v, true) << "\n";
	else {
		if(opts.toon)
			Cerr() << "note: reply is not JSON, printed as text\n";
		Cout() << body << "\n";
	}
}

static void Trace(const SiteCliOptions& opts, const String& method, const String& url,
                  const String& body, const SiteCliReply& reply)
{
	if(!opts.trace)
		return;
	Cerr() << "trace: " << method << " " << url << "\n";
	if(body.GetCount())
		Cerr() << "trace: request " << body << "\n";
	Cerr() << "trace: status " << reply.status << "\n";
	Cerr() << "trace: reply " << reply.body << "\n";
}

static String SiteUrl(const SiteCliOptions& opts, const String& site)
{
	int i = opts.site_urls.Find(site);
	return Base(i >= 0 ? opts.site_urls[i] : opts.server);
}

static bool FetchManifest(const SiteCliOptions& opts, ValueMap& out, String& err)
{
	SiteCliReply reply;
	if(!Send("GET", Base(opts.server) + "/api/cli/manifest", String(), String(), String(), String(), String(),
	         String(), reply, err))
		return false;
	Value v = ParseJSON(reply.body);
	if(reply.status != 200 || !v.Is<ValueMap>()) {
		err = Format("manifest: HTTP %d %s", reply.status, reply.body);
		return false;
	}
	out = v;
	return true;
}

static void PrintUsage()
{
	Cout() <<
	"skylarkctl: agent-first client for a Skylark server's command manifest\n"
	"\n"
	"usage:\n"
	"  skylarkctl [global options] help\n"
	"  skylarkctl [global options] manifest [section]\n"
	"  skylarkctl [global options] login <section> <user>\n"
	"  skylarkctl [global options] logout <section>\n"
	"  skylarkctl [global options] whoami <section>\n"
	"  skylarkctl [global options] repl\n"
	"  skylarkctl [global options] <section> <command> [name=value | value ...]\n"
	"\n"
	"global options:\n"
	"  --server <url>         manifest host, e.g. http://127.0.0.1:8080 (required)\n"
	"  --site <section=url>   base URL of one section's own port (repeatable)\n"
	"  --session <file>       cookie jar kept between runs (needed for login)\n"
	"  --password-env <name>  read the login password from this variable, else prompt\n"
	"  --bearer <file>        send this token as Authorization: Bearer (host api/ routes)\n"
	"  --bearer-login         login through api/auth/login and keep the bearer token in the jar\n"
	"  --toon                 print replies as TOON; preferred for AI agents (compact)\n"
	"  --json                 print replies as JSON (default)\n"
	"  --trace                print the last request and reply on stderr\n"
	"\n"
	"exit codes: 0 ok, 1 server or transport error, 2 usage error\n";
}

static int CommandFailed(const String& message)
{
	Cerr() << "error: " << message << "\n";
	return 2;
}

// Lists the sections and commands the connected server offers.
static int Help(const SiteCliOptions& opts)
{
	PrintUsage();
	if(opts.server.IsEmpty())
		return 0;
	ValueMap m;
	String err;
	if(!FetchManifest(opts, m, err))
		return CommandFailed(err);
	ValueArray sites = m["sites"];
	Cout() << "\nsections on " << opts.server << ":\n";
	for(int s = 0; s < sites.GetCount(); s++) {
		ValueMap site = sites[s];
		Cout() << "\n" << (String)site["id"] << "  (" << (String)site["title"] << ")\n";
		ValueArray cmds = site["commands"];
		for(int c = 0; c < cmds.GetCount(); c++) {
			ValueMap cmd = cmds[c];
			String route = (String)cmd["transport"] == "broker"
				? "broker " + (String)cmd["broker_op"]
				: (String)cmd["method"] + " " + (String)cmd["api_path"];
			Cout() << "  " << (String)cmd["name"] << "  " << route << "\n      " << (String)cmd["description"] << "\n";
			ValueArray args = cmd["args"];
			if(args.GetCount()) {
				Cout() << "      args:";
				for(int a = 0; a < args.GetCount(); a++) {
					ValueMap arg = args[a];
					Cout() << " " << (String)arg["name"] << "=" << (String)arg["type"]
					       << ((bool)arg["required"] ? "" : "?");
				}
				Cout() << "\n";
			}
		}
	}
	return 0;
}

static int ManifestCommand(const SiteCliOptions& opts, const Vector<String>& words)
{
	String err;
	if(opts.server.IsEmpty())
		return CommandFailed("no --server given");
	String site = words.GetCount() > 1 ? words[1] : String();
	SiteCliReply reply;
	String url = Base(opts.server) + "/api/cli/manifest" + (site.IsEmpty() ? String() : "/" + UrlEncodePath(site));
	if(!Send("GET", url, String(), String(), String(), String(), String(), String(), reply, err))
		return CommandFailed(err);
	Trace(opts, "GET", url, String(), reply);
	PrintReply(opts, reply.body);
	return reply.status == 200 ? 0 : 1;
}

static int LoginCommand(const SiteCliOptions& opts, const Vector<String>& words)
{
	if(words.GetCount() < 3)
		return CommandFailed("usage: login <section> <user>");
	if(opts.session_file.IsEmpty())
		return CommandFailed("login keeps the session in a cookie jar: pass --session <file>");
	String site = words[1];
	String password;
	if(opts.password_env.GetCount()) {
		const char *p = getenv(opts.password_env);
		password = p ? p : "";
	}
	else {
		Cerr() << "password: ";
		fflush(stderr);
		char buf[4096];
		if(fgets(buf, sizeof buf, stdin)) {
			password = buf;
			while(password.GetCount() && (password[password.GetCount() - 1] == '\n' || password[password.GetCount() - 1] == '\r'))
				password.Trim(password.GetCount() - 1);
		}
	}
	VectorMap<String, String> jar;
	JarLoad(opts.session_file, jar);
	if(opts.bearer_login) {
		// Host sites without a composed session route: api/auth/login returns a bearer token.
		ValueMap creds;
		creds.Add("username", words[2]);
		creds.Add("password", password);
		String api = SiteUrl(opts, site) + "/api/auth/login";
		SiteCliReply api_reply;
		String api_err;
		if(!Send("POST", api, AsJSON(creds), "application/json", String(), String(), String(), String(),
		         api_reply, api_err))
			return CommandFailed(api_err);
		Trace(opts, "POST", api, "username=" + words[2] + "&password=<hidden>", api_reply);
		ValueMap res = ParseJSON(api_reply.body);
		if(api_reply.status != 200 || res["token"].IsNull())
			return CommandFailed("sign-in refused (status " + IntStr(api_reply.status) + ")");
		JarSet(jar, site, "bearer", (String)res["token"]);
		JarSet(jar, site, "user", (String)res["username"]);
		if(!JarSave(opts.session_file, jar))
			return CommandFailed("cannot write " + opts.session_file);
		ValueMap out;
		out.Add("ok", true);
		out.Add("user", (String)res["username"]);
		PrintReply(opts, AsJSON(out));
		return 0;
	}
	String body = "username=" + UrlEncode(words[2]) + "&password=" + UrlEncode(password);
	String url = SiteUrl(opts, site) + "/session/login";
	SiteCliReply reply;
	String err;
	if(!Send("POST", url, body, "application/x-www-form-urlencoded", JarGet(jar, site, "cookie_name"),
	         JarGet(jar, site, "cookie_value"), JarGet(jar, site, "csrf"), String(), reply, err))
		return CommandFailed(err);
	Trace(opts, "POST", url, "username=" + words[2] + "&password=<hidden>", reply);
	PrintReply(opts, reply.body);
	if(reply.status != 200)
		return 1;
	String name, value;
	if(!ParseSetCookie(reply.set_cookie, name, value))
		return CommandFailed("login succeeded but the site set no session cookie");
	ValueMap res = ParseJSON(reply.body);
	JarSet(jar, site, "cookie_name", name);
	JarSet(jar, site, "cookie_value", value);
	JarSet(jar, site, "csrf", (String)res["csrf"]);
	JarSet(jar, site, "user", (String)res["user"]);
	if(!JarSave(opts.session_file, jar))
		return CommandFailed("cannot write " + opts.session_file);
	return 0;
}

static int LogoutCommand(const SiteCliOptions& opts, const Vector<String>& words)
{
	if(words.GetCount() < 2)
		return CommandFailed("usage: logout <section>");
	String site = words[1];
	VectorMap<String, String> jar;
	JarLoad(opts.session_file, jar);
	SiteCliReply reply;
	String err;
	String url = SiteUrl(opts, site) + "/session/logout";
	if(!Send("POST", url, String(), "application/x-www-form-urlencoded", JarGet(jar, site, "cookie_name"),
	         JarGet(jar, site, "cookie_value"), JarGet(jar, site, "csrf"), String(), reply, err))
		return CommandFailed(err);
	Trace(opts, "POST", url, String(), reply);
	PrintReply(opts, reply.body);
	JarSet(jar, site, "cookie_name", "");
	JarSet(jar, site, "cookie_value", "");
	JarSet(jar, site, "csrf", "");
	JarSet(jar, site, "user", "");
	JarSave(opts.session_file, jar);
	return reply.status == 200 ? 0 : 1;
}

static int WhoamiCommand(const SiteCliOptions& opts, const Vector<String>& words)
{
	if(words.GetCount() < 2)
		return CommandFailed("usage: whoami <section>");
	String site = words[1];
	VectorMap<String, String> jar;
	JarLoad(opts.session_file, jar);
	SiteCliReply reply;
	String err;
	String url = SiteUrl(opts, site) + "/session/whoami";
	if(!Send("GET", url, String(), String(), JarGet(jar, site, "cookie_name"),
	         JarGet(jar, site, "cookie_value"), JarGet(jar, site, "csrf"), BearerFor(opts, jar, site), reply, err))
		return CommandFailed(err);
	Trace(opts, "GET", url, String(), reply);
	PrintReply(opts, reply.body);
	return reply.status == 200 ? 0 : 1;
}

// "<section> <command> args..." runs one manifest command over HTTP.
static int RunManifestCommand(const SiteCliOptions& opts, const Vector<String>& words)
{
	if(words.GetCount() < 2)
		return CommandFailed("usage: <section> <command> [args]; try 'skylarkctl help'");
	if(opts.server.IsEmpty())
		return CommandFailed("no --server given");
	ValueMap m;
	String err;
	if(!FetchManifest(opts, m, err))
		return CommandFailed(err);
	ValueArray sites = m["sites"];
	Vector<String> site_ids;
	ValueMap site;
	bool found = false;
	for(int s = 0; s < sites.GetCount(); s++) {
		site_ids.Add((String)ValueMap(sites[s])["id"]);
		if(site_ids.Top() == words[0]) {
			site = sites[s];
			found = true;
		}
	}
	if(!found) {
		String hint = "unknown section '" + words[0] + "'";
		Vector<String> near = SiteCliSuggest(words[0], site_ids);
		if(near.GetCount())
			hint << "; did you mean: " << Join(near, ", ");
		else
			hint << "; sections on this server: " << Join(site_ids, ", ");
		return CommandFailed(hint);
	}

	ValueArray cmds = site["commands"];
	Vector<String> cmd_names;
	ValueMap command;
	bool have = false;
	for(int c = 0; c < cmds.GetCount(); c++) {
		cmd_names.Add((String)ValueMap(cmds[c])["name"]);
		if(cmd_names.Top() == words[1]) {
			command = cmds[c];
			have = true;
		}
	}
	if(!have) {
		String hint = "unknown command '" + words[1] + "' in section " + words[0];
		Vector<String> near = SiteCliSuggest(words[1], cmd_names);
		if(near.GetCount())
			hint << "; did you mean: " << Join(near, ", ");
		else
			hint << "; commands: " << Join(cmd_names, ", ");
		return CommandFailed(hint);
	}
	if((String)command["transport"] != "http")
		return CommandFailed("'" + words[1] + "' is the broker operation " + (String)command["broker_op"] +
		                     "; it is not reachable over HTTP");

	Vector<String> rest;
	for(int i = 2; i < words.GetCount(); i++)
		rest.Add(words[i]);
	SiteCliRequest req;
	if(!SiteCliBuildRequest(command, rest, req, err))
		return CommandFailed(err);

	VectorMap<String, String> jar;
	JarLoad(opts.session_file, jar);
	String url = SiteUrl(opts, words[0]) + "/" + req.url_path;
	SiteCliReply reply;
	if(!Send(req.method, url, req.body, req.content_type, JarGet(jar, words[0], "cookie_name"),
	         JarGet(jar, words[0], "cookie_value"), JarGet(jar, words[0], "csrf"), BearerFor(opts, jar, words[0]), reply, err))
		return CommandFailed(err);
	Trace(opts, req.method, url, req.body, reply);
	// Browser-style routes answer a successful write with a redirect; report its target.
	if(reply.status >= 300 && reply.status < 400 && reply.location.GetCount()) {
		// A redirect to the login page is a refusal, not a success.
		if(reply.location.Find("/session/login") >= 0) {
			Cerr() << "error: not signed in (the site redirected to its login page)\n";
			return 1;
		}
		Cout() << "redirect: " << reply.location << "\n";
		return 0;
	}
	PrintReply(opts, reply.body);
	// Some host routes answer 200 with {"status": "error"}; that is a refusal too.
	Value v = ParseJSON(reply.body);
	bool refused = v.Is<ValueMap>() && (String)ValueMap(v)["status"] == "error";
	return reply.status >= 200 && reply.status < 300 && !refused ? 0 : 1;
}

static int Dispatch(const SiteCliOptions& opts, const Vector<String>& words)
{
	if(words.IsEmpty() || words[0] == "help")
		return Help(opts);
	if(words[0] == "manifest")
		return ManifestCommand(opts, words);
	if(words[0] == "login")
		return LoginCommand(opts, words);
	if(words[0] == "logout")
		return LogoutCommand(opts, words);
	if(words[0] == "whoami")
		return WhoamiCommand(opts, words);
	return RunManifestCommand(opts, words);
}

static Vector<String> SplitWords(const String& line)
{
	Vector<String> out;
	for(const String& w : Split(line, ' '))
		if(!TrimBoth(w).IsEmpty())
			out.Add(TrimBoth(w));
	return out;
}

// Reads one line from stdin. False at end of input.
static bool ReadLine(String& line)
{
	char buf[8192];
	if(!fgets(buf, sizeof buf, stdin))
		return false;
	line = buf;
	while(line.GetCount() && (line[line.GetCount() - 1] == '\n' || line[line.GetCount() - 1] == '\r'))
		line.Trim(line.GetCount() - 1);
	return true;
}

static void Repl(const SiteCliOptions& opts)
{
	Vector<String> history;
	Cout() << "skylarkctl repl: 'help', 'history', 'exit'\n";
	for(;;) {
		Cout() << "skylarkctl> "; fflush(stdout);
		String line;
		if(!ReadLine(line))
			break;
		Vector<String> words = SplitWords(line);
		if(words.IsEmpty())
			continue;
		if(words[0] == "exit" || words[0] == "quit")
			break;
		if(words[0] == "history") {
			for(int i = 0; i < history.GetCount(); i++)
				Cout() << (i + 1) << "  " << history[i] << "\n";
			continue;
		}
		history.Add(TrimBoth(line));
		Dispatch(opts, words);
	}
}

END_UPP_NAMESPACE

using namespace Upp;

CONSOLE_APP_MAIN
{
	Vector<String> args;
	for(int i = 0; i < CommandLine().GetCount(); i++)
		args.Add(CommandLine()[i]);

	SiteCliOptions opts;
	Vector<String> words;
	bool self_test = false;
	for(int i = 0; i < args.GetCount(); i++) {
		const String& a = args[i];
		bool has_value = i + 1 < args.GetCount();
		if(a == "--server" && has_value)
			opts.server = args[++i];
		else if(a == "--site" && has_value) {
			String v = args[++i];
			int eq = v.Find('=');
			if(eq <= 0) {
				Exit(CommandFailed("--site takes section=url"));
			}
			opts.site_urls.GetAdd(v.Left(eq)) = v.Mid(eq + 1);
		}
		else if(a == "--session" && has_value)
			opts.session_file = args[++i];
		else if(a == "--password-env" && has_value)
			opts.password_env = args[++i];
		else if(a == "--bearer" && has_value)
			opts.bearer_file = args[++i];
		else if(a == "--bearer-login")
			opts.bearer_login = true;
		else if(a == "--toon")
			opts.toon = true;
		else if(a == "--json")
			opts.toon = false;
		else if(a == "--trace")
			opts.trace = true;
		else if(a == "--self-test")
			self_test = true;
		else if(a == "--help" || a == "-h") {
			PrintUsage();
			Exit(0);
		}
		else
			words.Add(a);
	}

	if(self_test) {
		int failures = SiteCliSelfTests();
		Cout() << "\nSiteCli: " << (failures ? "FAILED" : "all checks passed") << "\n";
		Exit(failures ? 1 : 0);
	}
	if(words.GetCount() == 1 && words[0] == "repl") {
		Repl(opts);
		Exit(0);
	}
	int rc = Dispatch(opts, words);
	fflush(stdout);
	Exit(rc);
}
