#include "AgentIpcClient.h"

// The child environment is built from the process environment; the password
// is placed under its configured variable name (never in argv).
extern char **environ;

NAMESPACE_UPP

// Read-only bridge to the AgentIPC node. Everything runs through the
// argv-only client (the same shape GitHubForge uses for gh): no shell, no
// password in argv, bounded output, killed on timeout.

Vector<String> AgentIpcDefaultClientArgv()
{
	Vector<String> v;
	v.Add(GetHomeDirFile(".venvs/agentipc/bin/python"));
	v.Add(GetHomeDirFile("OuluLinux/AgentIPC/bin/agentipc"));
	return v;
}

bool AgentIpcValidChannel(const String& chan)
{
	if(chan.IsEmpty() || chan.GetCount() > 64)
		return false;
	for(int i = 0; i < chan.GetCount(); i++) {
		char c = chan[i];
		bool alnum = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
		if(!alnum && c != '-' && c != '_' && c != '.')
			return false;
	}
	return true;
}

// The child environment: the parent's environment with the identity password
// placed under the configured variable name. Built per call, passed as an env
// block, never written to argv and never logged.
static String BuildChildEnv(const AgentIpcConfig& cfg)
{
	StringBuffer b;
	for(char **e = environ; e && *e; e++) {
		String s(*e);
		int eq = s.Find('=');
		if(eq > 0 && s.Mid(0, eq) == cfg.password_env_name)
			continue;
		b << s << '\0';
	}
	b << cfg.password_env_name << '=' << cfg.password << '\0';
	b << '\0';
	return String(b);
}

static AgentIpcReply AgentIpcRun(const AgentIpcConfig& cfg, const Vector<String>& op_args)
{
	AgentIpcReply r;
	if(cfg.client_argv.IsEmpty()) {
		r.error = "agentipc client prefix is not configured";
		return r;
	}
	Vector<String> args;
	for(int i = 1; i < cfg.client_argv.GetCount(); i++)
		args.Add(cfg.client_argv[i]);
	args.Add("vs");
	args.Add("--direct");
	args.Add(cfg.direct);
	args.Add("--server");
	args.Add(cfg.server);
	args.Add("--user");
	args.Add(cfg.user);
	args.Add("--password-env");
	args.Add(cfg.password_env_name);
	for(const String& a : op_args)
		args.Add(a);

	LocalProcess p;
	if(!p.Start2(cfg.client_argv[0], args, ~BuildChildEnv(cfg))) {
		r.error = "cannot start the agentipc client: " + cfg.client_argv[0];
		return r;
	}
	p.CloseWrite();
	String out, errout;
	uint32 start = GetTickCount();
	for(;;) {
		String co, ce;
		bool more = p.Read2(co, ce);
		out << co;
		errout << ce;
		if(!more)
			break;
		if(GetTickCount() - start > (uint32)cfg.timeout_ms) {
			p.Kill();
			r.error = "the agentipc client timed out";
			return r;
		}
		Sleep(5);
	}
	r.exit_code = p.GetExitCode();
	if(out.GetCount() > 4 * 1024 * 1024)
		out = out.Left(4 * 1024 * 1024);
	r.output = out;
	r.error = errout;
	r.ok = r.exit_code == 0;
	if(!r.ok && r.error.IsEmpty())
		r.error = out.Mid(0, 500);
	return r;
}

// One line of the client's read output: "#<chan> [ts] <user>: <body>".
static bool ParseMessageLine(const String& line, AgentIpcMessage& m)
{
	String s = line;
	while(s.GetCount() && (s[s.GetCount() - 1] == '\r' || s[s.GetCount() - 1] == '\n'
	                      || s[s.GetCount() - 1] == ' '))
		s = s.Left(s.GetCount() - 1);
	if(s.GetCount() < 2 || s[0] != '#')
		return false;
	int open = s.Find(" [");
	if(open < 0)
		return false;
	int close = s.Find("]", open + 2);
	if(close < 0)
		return false;
	if(close + 2 >= s.GetCount() || s[close + 1] != ' ')
		return false;
	m.ts = s.Mid(open + 2, close - open - 2);
	String rest = s.Mid(close + 2);
	int colon = rest.Find(": ");
	if(colon < 0)
		return false;
	m.user = rest.Mid(0, colon);
	m.body = rest.Mid(colon + 2);
	return !m.ts.IsEmpty() && !m.user.IsEmpty();
}

AgentIpcReply AgentIpcReadChannel(const AgentIpcConfig& cfg, const String& chan, int limit)
{
	AgentIpcReply r;
	if(!AgentIpcValidChannel(chan)) {
		r.error = "invalid channel name";
		return r;
	}
	Vector<String> op;
	op.Add("read");
	op.Add(chan);
	r = AgentIpcRun(cfg, op);
	if(!r.ok)
		return r;
	Vector<AgentIpcMessage> all;
	Vector<String> lines = Split(r.output, '\n');
	for(const String& line : lines) {
		if(line.IsEmpty())
			continue;
		AgentIpcMessage m;
		if(ParseMessageLine(line, m))
			all.Add(m);
	}
	if(limit > 0 && all.GetCount() > limit)
		all.Remove(0, all.GetCount() - limit);
	r.messages = pick(all);
	return r;
}

AgentIpcReply AgentIpcNames(const AgentIpcConfig& cfg, const String& chan)
{
	AgentIpcReply r;
	if(!AgentIpcValidChannel(chan)) {
		r.error = "invalid channel name";
		return r;
	}
	Vector<String> op;
	op.Add("names");
	op.Add(chan);
	r = AgentIpcRun(cfg, op);
	return r;
}

bool AgentIpcOverview(const AgentIpcConfig& cfg, int tail_bytes,
                      Vector<AgentIpcChannelInfo>& out, String& error)
{
	out.Clear();
	error.Clear();
	if(cfg.records_path.IsEmpty()) {
		error = "the node record path is not configured";
		return false;
	}
	FileIn f(cfg.records_path);
	if(!f.IsOpen()) {
		error = "cannot open the node record file: " + cfg.records_path;
		return false;
	}
	int64 size = f.GetSize();
	int64 skip = size > tail_bytes ? size - tail_bytes : 0;
	f.Seek(skip);
	if(skip > 0) {
		// Skip to the next line start so the first record is whole.
		String dropped = f.GetLine();
		(void)dropped;
	}
	Index<String> seen;
	while(!f.IsEof()) {
		String line = f.GetLine();
		if(line.IsEmpty())
			continue;
		Value v;
		try {
			v = ParseJSON(line);
		}
		catch(...) {
			continue;
		}
		if(!v.Is<ValueMap>())
			continue;
		ValueMap m = v;
		AgentIpcChannelInfo info;
		info.server = (String)m.Get("server", String());
		info.chan = (String)m.Get("chan", String());
		info.last_ts = (String)m.Get("ts", String());
		info.last_user = (String)m.Get("user", String());
		// The body is ciphertext on an encrypted server and is never read here.
		if(info.server.IsEmpty() || info.chan.IsEmpty())
			continue;
		String key = info.server + "/" + info.chan;
		int i = seen.Find(key);
		if(i < 0) {
			seen.Add(key);
			out.Add(info);
		}
		else
			out[i] = info; // later lines carry the later activity
	}
	return true;
}

END_UPP_NAMESPACE
