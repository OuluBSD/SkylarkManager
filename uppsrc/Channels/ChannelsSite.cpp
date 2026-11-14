#include "ChannelsSite.h"

NAMESPACE_UPP

// Channels: the read-only frontend over the AgentIPC message board. Every
// page needs a session user; nothing here mutates the node. Reads run through
// the argv-only client (AgentIpcClient); the channel overview reads the node
// record file's plaintext metadata only, because the bodies on an encrypted
// server are ciphertext.

struct ChannelCacheEntry : Moveable<ChannelCacheEntry> {
	uint64                  stamped_ms = 0;
	Vector<AgentIpcMessage> messages;

	ChannelCacheEntry() {}
	ChannelCacheEntry(const ChannelCacheEntry& o)
		: stamped_ms(o.stamped_ms), messages(clone(o.messages)) {}
	ChannelCacheEntry& operator=(const ChannelCacheEntry& o) {
		if(this != &o) {
			stamped_ms = o.stamped_ms;
			messages = clone(o.messages);
		}
		return *this;
	}
};

static AgentIpcConfig *s_cfg = NULL;
static VectorMap<String, ChannelCacheEntry> s_cache;
static Mutex s_cache_lock;

static const uint64 CACHE_TTL_MS = 20 * 1000;
static const int    CACHE_LIMIT = 64;
static const int    PAGE_LIMIT = 200;

static String Esc(const String& s)
{
	StringBuffer out;
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
		switch(c) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(c);
		}
	}
	return String(out);
}

// Returns false, after redirecting to the site login, for anonymous callers.
static bool RequireLogin(WebRequest& r)
{
	if(r.HasUser())
		return true;
	r.Redirect(r.Site().Url("session/login"));
	return false;
}

static void Page(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html" + ThemeHtmlAttrs(r) + "><head><meta charset=\"utf-8\">" + ThemeHeadScript() + 
	       "<title>" + Esc(title) + "</title></head><body>"
	       "<nav><a href=\"" + Esc(r.Site().Url("")) + "\">Channels</a> " + SessionChromeHtml(r) + "</nav>"
	       "<h1>" + Esc(title) + "</h1>" + body + "</body></html>");
}

static void NodeProblem(WebRequest& r, const String& error)
{
	r.Status(503, "the AgentIPC node did not answer");
	Page(r, "Channels unavailable",
	     "<p>The AgentIPC node did not answer, so this view cannot be shown.</p>"
	     "<p><code>" + Esc(error) + "</code></p>");
}

static uint64 TickMs()
{
	return (uint64)GetTickCount();
}

// Cache-then-fetch with a TTL; bypassed by ?fresh=1 for the signed-in user.
static bool CachedMessages(const String& key, bool fresh, Vector<AgentIpcMessage>& out,
                           String& error, bool& from_cache)
{
	from_cache = false;
	uint64 now = TickMs();
	s_cache_lock.Enter();
	int i = s_cache.Find(key);
	if(i >= 0 && !fresh && now - s_cache[i].stamped_ms < CACHE_TTL_MS) {
		out <<= s_cache[i].messages;
		s_cache_lock.Leave();
		from_cache = true;
		return true;
	}
	s_cache_lock.Leave();

	AgentIpcReply rep = AgentIpcReadChannel(*s_cfg, key.Mid(key.Find('/') + 1), PAGE_LIMIT);
	if(!rep.ok) {
		error = rep.error.IsEmpty() ? "the client failed" : rep.error;
		return false;
	}
	s_cache_lock.Enter();
	i = s_cache.Find(key);
	if(i < 0) {
		if(s_cache.GetCount() >= CACHE_LIMIT) {
			int oldest = 0;
			for(int j = 1; j < s_cache.GetCount(); j++)
				if(s_cache[j].stamped_ms < s_cache[oldest].stamped_ms)
					oldest = j;
			s_cache.Remove(oldest);
		}
		s_cache.Add(key, ChannelCacheEntry());
		i = s_cache.Find(key);
	}
	s_cache[i].stamped_ms = now;
	s_cache[i].messages <<= rep.messages;
	out <<= s_cache[i].messages;
	s_cache_lock.Leave();
	return true;
}

static void Overview(WebRequest& r)
{
	if(!r.HasUser()) {
		Page(r, "Channels", "<p>Channels show the agent activity of this node.</p><p><a href=\"" + Esc(r.Site().Url("session/login")) + "\">Sign in</a> to see the details.</p>");
		return;
	}
	if(!RequireLogin(r))
		return;
	Vector<AgentIpcChannelInfo> infos;
	String error;
	if(!AgentIpcOverview(*s_cfg, 200 * 1024, infos, error)) {
		NodeProblem(r, "no node records: " + error);
		return;
	}
	StringBuffer b;
	if(infos.IsEmpty())
		b << "<p>No channels have been used on the node yet.</p>";
	else {
		b << "<table><tr><th>Server</th><th>Channel</th><th>Last activity</th><th>Last sender</th></tr>";
		for(const AgentIpcChannelInfo& info : infos)
			b << "<tr><td>" << Esc(info.server) << "</td><td><a href=\""
			  << Esc(r.Site().Url("channels/*/*", Vector<Value>()
			          << Value(info.server) << Value(info.chan)))
			  << "\">#" << Esc(info.chan) << "</a></td><td>" << Esc(info.last_ts)
			  << "</td><td>" << Esc(info.last_user) << "</td></tr>";
		b << "</table>";
	}
	Page(r, "Channels", String(b));
}

static void ChannelView(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	String server = r.PathArg(0);
	String chan = r.PathArg(1);
	if(!AgentIpcValidChannel(chan)) {
		r.Status(400, "invalid channel name");
		return;
	}
	Vector<AgentIpcMessage> msgs;
	String error;
	bool from_cache = false;
	if(!CachedMessages(server + "/" + chan, !r.Param("fresh").IsEmpty(), msgs, error, from_cache)) {
		NodeProblem(r, error);
		return;
	}
	StringBuffer b;
	b << "<p>" << (from_cache ? "From the cache (up to 20 s old). " : "")
	  << "Latest " << msgs.GetCount() << " messages. "
	  << "<a href=\"" << Esc(r.Site().Url("channels/*/*",
	      Vector<Value>() << Value(server) << Value(chan))) << "?fresh=1\">Refresh</a></p>";
	b << "<table><tr><th>Time</th><th>Sender</th><th>Message</th></tr>";
	for(const AgentIpcMessage& m : msgs)
		b << "<tr><td>" << Esc(m.ts) << "</td><td>" << Esc(m.user) << "</td><td>"
		  << Esc(m.body) << "</td></tr>";
	b << "</table>";
	Page(r, "#" + chan + " on " + server, String(b));
}

void InstallChannelsSite(EngineSite& site, const AgentIpcConfig& cfg)
{
	static AgentIpcConfig stored;
	stored = cfg;
	s_cfg = &stored;
	s_cache.Clear();
	SiteRouter& rt = site.Router();
	rt.Add("", Overview);
	rt.Add("overview", Overview);
	rt.Add("channels/*/*", ChannelView);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("channels-overview", "GET", "overview", "session account",
	                                        "html", "List the node's channels and their last activity", {}));
	cm.Add(site.id, site.title, HttpCommand("channel-read", "GET", "channels/*/*",
	                                        "session account", "html",
	                                        "Show a channel's recent messages (read-only)",
	                                        {"server:string:required", "chan:string:required"}));
}

END_UPP_NAMESPACE
