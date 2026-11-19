#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include <WebEngine/SessionRoutes.h>
#include <MultiAtlas/MultiAtlasSite.h>
#include <MultiAtlas/MultiAtlasStore.h>

NAMESPACE_UPP

static const int API_PORT = 18473;
static String s_root;
static int s_checks, s_failures;

struct ApiTestHost : WebEngineHost {
	ApiTestHost() { port = 18472; threads = 4; prefork = 0; }
};
static ApiTestHost *s_host;
static void HostMain() { s_host->Run(); }

struct HttpReply : Moveable<HttpReply> {
	int status = 0;
	String headers, body;
};

static void Check(bool ok, const String& text)
{
	s_checks++;
	if(!ok) s_failures++;
	Cout() << (ok ? "ok   " : "FAIL ") << text << '\n';
}

static bool Run(const String& program, const Vector<String>& args, const String& cwd = String())
{
	LocalProcess p;
	if(!p.Start2(program, args, NULL, cwd.IsEmpty() ? NULL : ~cwd)) return false;
	p.CloseWrite();
	String stdout_text, stderr_text;
	for(;;) { String out, err; bool more = p.Read2(out, err); stdout_text << out; stderr_text << err; if(!more) break; }
	int exit_code = p.GetExitCode();
	if(exit_code != 0) Cout() << "command failed (" << exit_code << "): " << program << "\n" << stderr_text;
	return exit_code == 0;
}

static String Capture(const String& program, const Vector<String>& args, const String& cwd = String())
{
	LocalProcess p; String output;
	if(!p.Start2(program, args, NULL, cwd.IsEmpty() ? NULL : ~cwd)) return String();
	p.CloseWrite();
	for(;;) { String out, err; bool more = p.Read2(out, err); output << out; if(!more) break; }
	return p.GetExitCode() == 0 ? TrimBoth(output) : String();
}

static bool InitRepo(const String& repo)
{
	if(!RealizeDirectory(AppendFileName(repo, "multiatlas"))) return false;
	SaveFile(AppendFileName(repo, "README.md"), "API fixture\n");
	return Run("git", {"init", "-q"}, repo) &&
	       Run("git", {"config", "user.email", "api-test@example.invalid"}, repo) &&
	       Run("git", {"config", "user.name", "MultiAtlas API Test"}, repo) &&
	       Run("git", {"add", "README.md"}, repo) &&
	       Run("git", {"commit", "-q", "-m", "fixture"}, repo) &&
	       Run("git", {"branch", "-M", "main"}, repo);
}

static String MakeStore(const String& repo, const String& family)
{
	String path = AppendFileName(repo, "multiatlas/" + family + "/store");
	RealizeDirectory(path);
	return path;
}

static MultiAtlasVersion Version(const String& vid, int code = 1000000)
{
	MultiAtlasVersion v; v.vid = vid; v.code = code; return v;
}

static bool SeedTrack(const String& repo, const String& track_id, const String& family_id,
                      const String& requirement = String(), const String& warning_action = String())
{
	String store = MakeStore(repo, family_id), root = AppendFileName(store, "data"), error;
	MultiAtlasTrack family; family.track_id = track_id; family.family = family_id;
	family.branch = "track/" + track_id; family.versions.Add(Version("v1"));
	if(!requirement.IsEmpty()) family.versions[0].requires.Add(requirement);
	if(!MultiAtlasSaveTrackFamily(root, family_id, family, RecordAbsent(), error)) return false;
	MultiAtlasMap map; map.map = track_id; map.track_id = track_id; map.version = "v1";
	MultiAtlasElement e; e.id = "start"; e.name = "Start"; e.kind = "task"; e.status = "open";
	e.w = 100; e.h = 60; map.elements.Add(e);
	if(!MultiAtlasSaveTrackMap(root, family_id, map, RecordAbsent(), error)) return false;
	MultiAtlasRules rules; MultiAtlasAction action;
	if(!warning_action.IsEmpty()) { action.name = "known"; rules.actions.Add(pick(action)); }
	if(!MultiAtlasSaveTrackRules(root, family_id, rules, RecordAbsent(), error)) return false;
	return true;
}

static bool SeedMeta(const String& repo, const String& family_id)
{
	String store = MakeStore(repo, family_id), root = AppendFileName(store, "data"), error;
	MultiAtlasMetaFamily family; family.ensemble_name = "fixture"; family.title = "Fixture";
	family.meta_family = family_id; family.versions.Add(Version("m1"));
	if(!MultiAtlasSaveMetaFamily(root, family_id, family, RecordAbsent(), error)) return false;
	MultiAtlasMap map; map.map = "meta"; map.track_id = "meta"; map.version = "m1";
	MultiAtlasElement e; e.id = "milestone"; e.name = "Milestone"; e.kind = "task"; e.status = "open";
	e.w = 100; e.h = 60; map.elements.Add(e);
	if(!MultiAtlasSaveTrackMap(root, family_id, map, RecordAbsent(), error)) return false;
	MultiAtlasRules rules;
	return MultiAtlasSaveTrackRules(root, family_id, rules, RecordAbsent(), error);
}

static Value TrackConfig(const String& repo, const String& id, const String& family,
                         const String& editor)
{
	ValueMap t; t.Add("track_id", id); t.Add("family", family); t.Add("repo", repo);
	t.Add("branch", "track/" + id); t.Add("store", "multiatlas/" + family + "/store");
	ValueArray editors; if(!editor.IsEmpty()) editors.Add(editor); t.Add("editors", editors);
	return t;
}

static Value EnsembleConfig(const String& name, bool public_read,
                            const String& meta_repo, const String& meta_family,
                            const String& first_repo, const String& second_repo,
                            const String& first_editor, const String& second_editor)
{
	ValueMap meta; meta.Add("family", meta_family); meta.Add("repo", meta_repo);
	meta.Add("store", "multiatlas/" + meta_family + "/store");
	ValueArray tracks;
	tracks.Add(TrackConfig(first_repo, "vision", "family-vision-" + name, first_editor));
	tracks.Add(TrackConfig(second_repo, "agent", "family-agent-" + name, second_editor));
	ValueMap ensemble; ensemble.Add("name", name); ensemble.Add("title", "Fixture " + name);
	if(!public_read) ensemble.Add("public_read", false);
	ensemble.Add("meta", meta); ensemble.Add("tracks", tracks);
	return ensemble;
}

static HttpReply Request(const String& method, const String& path, const String& body = String(),
                         const String& cookie = String(), const String& csrf = String(),
                         const String& origin = String("http://127.0.0.1:18473"),
                         bool send_csrf = true)
{
	HttpReply reply;
	TcpSocket socket; socket.Timeout(5000);
	if(!socket.Connect("127.0.0.1", API_PORT)) return reply;
	String request = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n";
	if(!origin.IsEmpty()) request << "Origin: " << origin << "\r\n";
	if(!body.IsEmpty()) request << "Content-Type: application/json\r\nContent-Length: " << body.GetCount() << "\r\n";
	if(!cookie.IsEmpty()) request << "Cookie: " << cookie << "\r\n";
	if(send_csrf && !csrf.IsEmpty()) request << "X-WF-CSRF: " << csrf << "\r\n";
	request << "\r\n" << body;
	socket.Put(~request, request.GetCount());
	String response;
	for(;;) {
		String part = socket.Get(4096);
		if(part.IsEmpty()) break;
		response.Cat(part);
	}
	int end = response.Find("\r\n\r\n");
	if(end < 0) return reply;
	reply.headers = response.Left(end);
	reply.body = response.Mid(end + 4);
	int space = reply.headers.Find(' ');
	if(space >= 0) reply.status = atoi(~reply.headers.Mid(space + 1, 3));
	return reply;
}

static String JsonCookie(HttpReply& login, String& csrf)
{
	int split = login.body.Find("\r\n\r\n");
	Value parsed;
	try { parsed = ParseJSON(login.body); } catch(...) { return String(); }
	if(!parsed.Is<ValueMap>()) return String();
	ValueMap data = parsed;
	csrf = AsString(data.Get("csrf"));
	int p = login.headers.Find("Set-Cookie: ");
	if(p < 0) p = login.headers.Find("set-cookie: ");
	if(p < 0) return String();
	p += 12;
	int e = login.headers.Find(';', p);
	return e < 0 ? login.headers.Mid(p) : login.headers.Mid(p, e - p);
}

static ValueMap Json(HttpReply reply)
{
	try {
		Value v = ParseJSON(reply.body);
		if(v.Is<ValueMap>()) return ValueMap(v);
	}
	catch(...) {}
	return ValueMap();
}

static void HttpTest()
{
	String base = AppendFileName(s_root, "fixture");
	String public_meta = AppendFileName(base, "public-meta");
	String public_vision = AppendFileName(base, "public-vision");
	String public_agent = AppendFileName(base, "public-agent");
	String private_meta = AppendFileName(base, "private-meta");
	String private_vision = AppendFileName(base, "private-vision");
	String private_agent = AppendFileName(base, "private-agent");
	for(const String& repo : {public_meta, public_vision, public_agent, private_meta, private_vision, private_agent})
		Check(InitRepo(repo), "fixture git repository initialized");
	Check(Run("git", {"checkout", "-qb", "track/vision"}, public_vision) &&
	      Run("git", {"checkout", "-qb", "track/vision"}, private_vision),
	      "child fixtures use their configured worker branches");
	Check(SaveFile(AppendFileName(public_vision, "track-only.txt"), "ahead of main\n") &&
	      Run("git", {"add", "track-only.txt"}, public_vision) &&
	      Run("git", {"commit", "-q", "-m", "track-only commit"}, public_vision),
	      "public track fixture has a commit ahead of main");
	Check(SeedTrack(public_vision, "vision", "family-vision-public"), "public vision store seeded");
	Check(SeedTrack(public_agent, "agent", "family-agent-public", "vision:tested:v1"), "public dependent agent store seeded");
	Check(SeedMeta(public_meta, "family-meta-public"), "public meta store seeded");
	Check(SeedTrack(private_vision, "vision", "family-vision-private"), "private vision store seeded");
	Check(SeedTrack(private_agent, "agent", "family-agent-private"), "private agent store seeded");
	Check(SeedMeta(private_meta, "family-meta-private"), "private meta store seeded");

	String site_data = AppendFileName(base, "site-data"), engine_root = AppendFileName(site_data, "engine");
	String registry_dir = AppendFileName(engine_root, "multiatlas");
	RealizeDirectory(registry_dir);
	ValueArray ensembles;
	Value public_ensemble = EnsembleConfig("public", true, public_meta, "family-meta-public",
	                                      public_vision, public_agent, "alice", "bob");
	Value private_ensemble = EnsembleConfig("private", false, private_meta, "family-meta-private",
	                                       private_vision, private_agent, "alice", "bob");
	// Family keys and store directories are namespaced per ensemble. Replace the
	// public/private names in the fixture records above with config-selected ids.
	ValueMap root; ensembles.Add(public_ensemble); ensembles.Add(private_ensemble);
	root.Add("ensembles", ensembles);
	Check(SaveFile(AppendFileName(registry_dir, "projects.json"), AsJSON(root)), "registry fixture written");

	EngineSite& site = TheWebEngine().AddSite("multiatlas", "MultiAtlas",
		"http://127.0.0.1:18473", AppendFileName(engine_root, "multiatlas"));
	InstallMultiAtlasSite(site); InstallSessionRoutes(site);
	String user_id; bool existed; Vector<String> vision_role, agent_role, admin_role;
	site.Accounts().CreateUser("alice", "pw-alice", "Alice", vision_role, user_id, existed);
	site.Accounts().CreateUser("bob", "pw-bob", "Bob", agent_role, user_id, existed);
	admin_role.Add("admin"); site.Accounts().CreateUser("rootadmin", "pw-admin", "Admin", admin_role, user_id, existed);
	ApiTestHost host; host.BindSite(API_PORT, "multiatlas"); s_host = &host;
	Thread host_thread; host_thread.Run(HostMain);
	for(int i = 0; i < 100 && Request("GET", "/api/multiatlas/ensembles").status == 0; i++) Sleep(20);

	HttpReply anon_list = Request("GET", "/api/multiatlas/ensembles");
	ValueMap anon_json = Json(anon_list), anon_data = anon_json.Get("data");
	ValueArray anon_ensembles = anon_data.Get("ensembles");
	Check(anon_list.status == 200 && anon_ensembles.GetCount() == 1 && AsString(ValueMap(anon_ensembles[0]).Get("name")) == "public",
	      "anonymous ensemble listing omits private ensemble");
	HttpReply private_anon = Request("GET", "/api/multiatlas/private/check");
	Check(private_anon.status == 404, "anonymous direct request hides private ensemble");
	HttpReply private_timeline = Request("GET", "/multiatlas/private/timeline");
	Check(private_timeline.status == 404, "anonymous timeline hides private ensemble");
	HttpReply timeline = Request("GET", "/multiatlas/public/timeline");
	int slider_handler = timeline.body.Find("slider.addEventListener('input',update)");
	String dashboard_script = slider_handler >= 0 ? timeline.body.Mid(slider_handler) : String();
	Check(timeline.status == 200 && timeline.body.Find("0.1.0") >= 0 && timeline.body.Find("0.7.0") >= 0 &&
	      timeline.body.Find("1.0.0") >= 0 && timeline.body.Find("mat-scrub") >= 0 &&
	      slider_handler >= 0 && dashboard_script.Find("fetch(") < 0,
	      "timeline renders seven milestones and uses local-only slider updates");
	Check(timeline.body.Find("/track/'+encodeURIComponent(t.id)") >= 0 &&
	      timeline.body.Find("ahead_main") >= 0 && timeline.body.Find("behind_main") >= 0 &&
	      timeline.body.Find("\"ahead_main\":\"1\"") >= 0 && timeline.body.Find("\"behind_main\":\"0\"") >= 0 &&
	      timeline.body.Find("git.setAttribute('aria-label'") >= 0,
	      "timeline links track detail and reports whitespace-parsed divergence plus full accessible SHA");
	HttpReply public_check = Request("GET", "/api/multiatlas/public/check");
	ValueMap public_json = Json(public_check), public_report = public_json.Get("data");
	Check(public_check.status == 200 && public_report.Find("ok") >= 0 && public_report.Find("total_errors") >= 0 &&
	      public_report.Find("tracks") >= 0 && public_report.Find("unfulfilled_cross_track_dependencies") >= 0,
	      "check endpoint returns structured replay report");
	ValueArray unfulfilled = public_report.Get("unfulfilled_cross_track_dependencies");
	ValueMap pending_gate;
	if(unfulfilled.GetCount() == 1) pending_gate = unfulfilled[0];
	Check((bool)public_report.Get("ok") && (int)public_report.Get("total_errors") == 0 &&
	      (int)public_report.Get("total_warnings") > 0 && unfulfilled.GetCount() == 1 &&
	      pending_gate.Get("severity") == "warning" && pending_gate.Get("rule") == "contract-gate" &&
	      String(pending_gate.Get("text")).Find("pending gate") >= 0,
	      "pending tested gate is an unresolved dependency warning without failing replay");
	Check(public_check.body.Find(~public_meta) < 0 && public_check.body.Find("site-data") < 0,
	      "public JSON omits repository paths and site-account storage paths");

	String cookie, csrf;
	TcpSocket auth; auth.Timeout(5000);
	String login_body = "username=alice&password=pw-alice";
	String auth_req = "POST /session/login HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://127.0.0.1:18473\r\n"
	                  "Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
	                  AsString(login_body.GetCount()) + "\r\nConnection: close\r\n\r\n" + login_body;
	if(auth.Connect("127.0.0.1", API_PORT)) {
		auth.Put(~auth_req, auth_req.GetCount()); String raw;
		for(;;) { String part = auth.Get(4096); if(part.IsEmpty()) break; raw.Cat(part); }
		int split = raw.Find("\r\n\r\n");
		HttpReply good_login;
		if(split >= 0) { good_login.headers = raw.Left(split); good_login.body = raw.Mid(split + 4); }
		good_login.status = raw.Find(" 200 ") >= 0 ? 200 : 401;
		cookie = JsonCookie(good_login, csrf);
	}
	Check(!cookie.IsEmpty() && !csrf.IsEmpty(), "site-local account login provisions an engine session and CSRF token");

	HttpReply private_list = Request("GET", "/api/multiatlas/ensembles", String(), cookie, csrf);
	ValueMap private_list_json = Json(private_list), private_list_data = private_list_json.Get("data");
	Check(private_list.status == 200 && ValueArray(private_list_data.Get("ensembles")).GetCount() == 2,
	      "authenticated account sees private ensemble listing");
	HttpReply private_check = Request("GET", "/api/multiatlas/private/check", String(), cookie, csrf);
	Check(private_check.status == 200, "authenticated direct read opens private ensemble");
	HttpReply no_csrf = Request("POST", "/api/multiatlas/public/track/vision/event",
	                           "{\"expected_revision\":\"1\",\"vid\":\"v1\",\"type\":\"gate\"}",
	                           cookie, csrf, "http://127.0.0.1:18473", false);
	Check(no_csrf.status == 403, "WebEngine rejects cookie-authenticated write without CSRF token");
	HttpReply bad_origin = Request("POST", "/api/multiatlas/public/track/vision/event",	                               "{\"expected_revision\":\"1\",\"vid\":\"v1\",\"type\":\"gate\"}",
	                               cookie, csrf, "http://evil.example");
	Check(bad_origin.status == 403, "WebEngine rejects cross-origin write");
	HttpReply wrong_track = Request("POST", "/api/multiatlas/public/track/agent/event",	                                "{\"expected_revision\":\"1\",\"vid\":\"v1\",\"type\":\"gate\"}", cookie, csrf);
	Check(wrong_track.status == 403, "track editor cannot write another track");
	HttpReply invalid_vid = Request("POST", "/api/multiatlas/public/track/vision/event",	                                "{\"expected_revision\":\"1\",\"vid\":\"missing\",\"type\":\"gate\"}", cookie, csrf);
	Check(invalid_vid.status == 400, "event rejects an unknown family version");
	HttpReply bad_effect = Request("POST", "/api/multiatlas/public/track/vision/event",	                               "{\"expected_revision\":\"1\",\"vid\":\"v1\",\"type\":\"gate\",\"effects\":[\"agent:artifact:forged\"]}", cookie, csrf);
	Check(bad_effect.status == 400, "event rejects a cross-track effect literal");
	HttpReply stale = Request("POST", "/api/multiatlas/public/track/vision/event",	                         "{\"id\":\"gate-1\",\"expected_revision\":\"old\",\"vid\":\"v1\",\"type\":\"gate\"}", cookie, csrf);
	Check(stale.status == 409, "stale family revision returns conflict");
	HttpReply added = Request("POST", "/api/multiatlas/public/track/vision/event",	                          "{\"id\":\"gate-1\",\"expected_revision\":\"1\",\"vid\":\"v1\",\"type\":\"gate\",\"effects\":[\"tested:v1\"]}", cookie, csrf);
	Check(added.status == 201, "track editor appends event with expected family revision");
	HttpReply duplicate = Request("POST", "/api/multiatlas/public/track/vision/event",	                             "{\"id\":\"gate-1\",\"expected_revision\":\"1\",\"vid\":\"v1\",\"type\":\"gate\"}", cookie, csrf);
	Check(duplicate.status == 409, "duplicate append-only event id returns conflict");
	HttpReply unblocked = Request("GET", "/api/multiatlas/public/check", String(), cookie, csrf);
	ValueMap unblocked_json = Json(unblocked), unblocked_data = unblocked_json.Get("data");
	Check(unblocked.status == 200 && (bool)unblocked_data.Get("ok") &&
	      ValueArray(unblocked_data.Get("unfulfilled_cross_track_dependencies")).IsEmpty(),
	      "successful gate event unblocks the dependent track through ensemble replay");
	HttpReply warning = Request("POST", "/api/multiatlas/public/track/vision/event",
	                           "{\"id\":\"warning-1\",\"expected_revision\":\"1\",\"vid\":\"v1\","
	                           "\"type\":\"note\",\"action\":\"ghost\",\"subject_kind\":\"task\",\"subject_id\":\"warn-me\"}",
	                           cookie, csrf);
	Check(warning.status == 201, "event can append an action warning fixture");
	MultiAtlasRoadmapMessage warning_message; warning_message.track_id = "vision";
	warning_message.vid = "v1"; warning_message.subject_kind = "task";
	warning_message.subject_id = "warn-me"; warning_message.rule = "unknown-action";
	warning_message.literal = "ghost";
	warning_message.text = "track vision version v1 subject warn-me names unknown action ghost";
	String warning_id = MultiAtlasWarningId("vision", warning_message);
	HttpReply ack = Request("POST", "/api/multiatlas/public/warning/ack",
	                       "{\"track_id\":\"vision\",\"warning_id\":\"" + warning_id +
	                       "\",\"rule\":\"unknown-action\",\"vid\":\"v1\",\"comment\":\"reviewed\","
	                       "\"expected_revision\":\"absent\"}", cookie, csrf);
	Check(ack.status == 201, "scoped warning acknowledgment is recorded");
	HttpReply duplicate_ack = Request("POST", "/api/multiatlas/public/warning/ack",
	                                 "{\"track_id\":\"vision\",\"warning_id\":\"" + warning_id +
	                                 "\",\"rule\":\"unknown-action\",\"vid\":\"v1\",\"comment\":\"again\","
	                                 "\"expected_revision\":\"absent\"}", cookie, csrf);
	Check(duplicate_ack.status == 409, "duplicate scoped acknowledgment returns conflict");
	HttpReply cross_scope_ack = Request("POST", "/api/multiatlas/public/warning/ack",
	                                   "{\"track_id\":\"agent\",\"warning_id\":\"" + warning_id +
	                                   "\",\"rule\":\"unknown-action\",\"vid\":\"v1\",\"comment\":\"cross-scope\","
	                                   "\"expected_revision\":\"absent\"}", cookie, csrf);
	Check(cross_scope_ack.status == 403, "track-scoped editor cannot acknowledge another track's warning");
	HttpReply anon_after_ack = Request("GET", "/api/multiatlas/public/check");
	ValueArray public_acks = ValueMap(Json(anon_after_ack).Get("data")).Get("acknowledgments");
	ValueMap public_ack = public_acks.IsEmpty() ? ValueMap() : ValueMap(public_acks[0]);
	Check(anon_after_ack.status == 200 && public_ack.Find("actor") < 0 && public_ack.Find("comment") < 0,
	      "anonymous replay omits private acknowledgment actor and comment");

	HttpReply map = Request("GET", "/api/multiatlas/public/track/vision/map", String(), cookie, csrf);
	HttpReply tree = Request("GET", "/api/multiatlas/public/tree", String(), cookie, csrf);
	HttpReply status = Request("GET", "/api/multiatlas/public/status", String(), cookie, csrf);
	Check(map.status == 200 && Json(map).Find("data") >= 0, "track map endpoint returns map JSON");
	Check(tree.status == 200 && ValueArray(ValueMap(Json(tree).Get("data")).Get("edges")).GetCount() == 1,
	      "tree endpoint returns a dependency edge and versions");
	ValueArray status_tracks = ValueMap(Json(status).Get("data")).Get("tracks");
	ValueMap git = ValueMap(ValueMap(status_tracks[0]).Get("git"));
	Check(status.status == 200 && status_tracks.GetCount() == 2 && (bool)git.Get("available") &&
	      AsString(git.Get("head")).GetCount() == 40 && !AsString(git.Get("sync_at")).IsEmpty(),
	      "status endpoint returns argv-only git head, branch, timestamp and track versions");
	String vision_root = AppendFileName(public_vision, "multiatlas/family-vision-public/store/data");
	MultiAtlasTrack cyclic_family; String cyclic_revision, cyclic_error;
	bool cycle_seeded = MultiAtlasLoadTrackFamily(vision_root, "family-vision-public", cyclic_family,
	                                             cyclic_revision, cyclic_error);
	if(cycle_seeded) {
		cyclic_family.versions[0].requires.Add("agent:done:cycle");
		cycle_seeded = MultiAtlasSaveTrackFamily(vision_root, "family-vision-public", cyclic_family,
		                                         cyclic_revision, cyclic_error);
	}
	Check(cycle_seeded, "fatal replay cycle fixture is persisted");
	HttpReply cyclic_status = Request("GET", "/api/multiatlas/public/status", String(), cookie, csrf);
	ValueMap cyclic_data = ValueMap(Json(cyclic_status).Get("data"));
	Check(cyclic_status.status == 200 && !AsString(cyclic_data.Get("replay_error")).IsEmpty(),
	      "status handles a fatal cyclic DAG without missing-export access");
	if(cycle_seeded) {
		cycle_seeded = MultiAtlasLoadTrackFamily(vision_root, "family-vision-public", cyclic_family,
		                                        cyclic_revision, cyclic_error);
		if(cycle_seeded) cyclic_family.versions[0].requires.Clear();
		cycle_seeded = cycle_seeded && MultiAtlasSaveTrackFamily(vision_root, "family-vision-public", cyclic_family,
		                                        cyclic_revision, cyclic_error);
	}
	Check(cycle_seeded, "temporary replay cycle is removed for gate proof checks");
	HttpReply missing = Request("GET", "/api/multiatlas/public/track/nope/map", String(), cookie, csrf);
	Check(missing.status == 404, "unknown track map returns not found");

	HttpReply admin_login;
	String admin_cookie, admin_csrf;
	TcpSocket admin; admin.Timeout(5000);
	String admin_body = "username=rootadmin&password=pw-admin";
	String admin_req = "POST /session/login HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://127.0.0.1:18473\r\n"
	                   "Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
	                   AsString(admin_body.GetCount()) + "\r\nConnection: close\r\n\r\n" + admin_body;
	if(admin.Connect("127.0.0.1", API_PORT)) {
		admin.Put(~admin_req, admin_req.GetCount()); String raw;
		for(;;) { String part = admin.Get(4096); if(part.IsEmpty()) break; raw.Cat(part); }
		int split = raw.Find("\r\n\r\n");
		if(split >= 0) { admin_login.headers = raw.Left(split); admin_login.body = raw.Mid(split + 4); }
		admin_cookie = JsonCookie(admin_login, admin_csrf);
	}
	HttpReply denied_meta = Request("POST", "/api/multiatlas/public/meta/event",	                               "{\"expected_revision\":\"1\",\"vid\":\"m1\",\"type\":\"milestone\"}", cookie, csrf);
	Check(denied_meta.status == 403, "non-admin track editor cannot write meta-map");
	HttpReply meta_write = Request("POST", "/api/multiatlas/public/meta/event",	                               "{\"expected_revision\":\"1\",\"vid\":\"m1\",\"type\":\"milestone\"}", admin_cookie, admin_csrf);
	Check(meta_write.status == 201, "admin can append a meta-map milestone");
	Check(Run("git", {"add", "-f", "multiatlas/family-vision-public/store/data/events/family-vision-public.ev-gate-1.json", "multiatlas/family-vision-public/store/data/events/family-vision-public.ev-warning-1.json"}, public_vision) &&
	      Run("git", {"commit", "--only", "-q", "-m", "fixture-gate-state", "--", "multiatlas/family-vision-public/store/data/events/family-vision-public.ev-gate-1.json", "multiatlas/family-vision-public/store/data/events/family-vision-public.ev-warning-1.json"}, public_vision),
	      "fixture child events committed before bridge dispatch");
	String source_head = Capture("git", {"rev-parse", "--verify", "HEAD"}, public_vision);
	Check(source_head.GetCount() == 40, "fixture child branch commit resolves");
	MultiAtlasMetaFamily gate_meta; String gate_meta_revision, gate_meta_error;
	bool configured = MultiAtlasLoadMetaFamily(
		AppendFileName(public_meta, "multiatlas/family-meta-public/store/data"),
		"family-meta-public", gate_meta, gate_meta_revision, gate_meta_error);
	if(configured) {
		gate_meta.versions[0].requires.Add("vision:tested:v1");
		configured = MultiAtlasSaveMetaFamily(
			AppendFileName(public_meta, "multiatlas/family-meta-public/store/data"),
			"family-meta-public", gate_meta, gate_meta_revision, gate_meta_error);
	}
	Check(configured, "meta release requirement uses the normalized child gate contract");
	String forged_body = "{\"vid\":\"m1\",\"subject_kind\":\"element\",\"subject_id\":\"e_track_vision\","
	                    "\"type\":\"stage\",\"action\":\"RecordGatePass\","
	                    "\"effects\":[\"track:vision:tested:v1\"],\"text\":\"Track vision passed gate v1 at commit " + source_head + "\","
	                    "\"gate_track_id\":\"vision\",\"gate_version\":\"v1\",\"gate_event_id\":\"uncommitted\","
	                    "\"gate_child_vid\":\"v1\",\"git_commit_hash\":\"" + source_head + "\"}";
	HttpReply forged_gate = Request("POST", "/api/multiatlas/public/meta/event", forged_body,
	                                admin_cookie, admin_csrf);
	Check(forged_gate.status == 409, "authorized request without committed gate proof is rejected");
	String bridge_root = AppendFileName(s_root, "bridge-queue"); RealizeDirectory(bridge_root);
	String tester = AppendFileName(s_root, "gate-verifier.sh");
	Check(SaveFile(tester, "#!/bin/sh\n[ -d \"$1\" ] && [ \"$2\" = v1 ]\n") &&
	      Run("chmod", {"+x", tester}), "isolated gate tester fixture installed");
	String task_path = AppendFileName(bridge_root, "verified-gate.task.txt");
	Check(SaveFile(task_path, "ensemble=public\ntrack_id=vision\ngate_version=v1\ngit_commit_hash=" + source_head + "\n"),
	      "isolated bridge task written");
	String api_base = "http://127.0.0.1:18473";
	String manager_root = GetCurrentDirectory();
	String gate_script = AppendFileName(manager_root, "uppsrc/MultiAtlas/tools/record-gate-pass.sh");
	String adapter_script = AppendFileName(manager_root, "uppsrc/MultiAtlas/tools/worker-bridge/multiatlas-gate-adapter.sh");
	SetEnv("MA_API_BASE", api_base); SetEnv("MA_API_ORIGIN", api_base); SetEnv("MA_META_VID", "m1");
	SetEnv("MA_COOKIE", admin_cookie); SetEnv("MA_CSRF", admin_csrf); SetEnv("MA_GATE_QUEUE_DIR", bridge_root);
	SetEnv("MA_ENSEMBLE", "public"); SetEnv("MA_TRACK_ID", "vision"); SetEnv("MA_TRACK_REPO", public_vision);
	SetEnv("MA_TRACK_BRANCH", "track/vision"); SetEnv("MA_TRACK_FAMILY", "family-vision-public");
	SetEnv("MA_TRACK_STORE", "multiatlas/family-vision-public/store"); SetEnv("MA_GATE_TESTER", tester);
	SetEnv("MA_RECORD_GATE_SCRIPT", gate_script);
	bool bridge_pipeline = Run("sh", {adapter_script, "--once"}, manager_root);
	Check(bridge_pipeline && FileExists(task_path + ".done"),
	      "restricted worker bridge verifies, commits scoped evidence and synchronizes over HTTP");
	String gate_commit = Capture("git", {"rev-parse", "--verify", "HEAD"}, public_vision);
	String evidence_path = AppendFileName(public_vision, ".multiatlas/gates/v1.json");
	ValueMap evidence;
	try { evidence = ValueMap(ParseJSON(LoadFile(evidence_path))); } catch(...) {}
	String gate_event_id = AsString(evidence.Get("event_id"));
	String child_vid = AsString(evidence.Get("child_vid"));
	String changed = Capture("git", {"diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD"}, public_vision);
	Check(gate_commit.GetCount() == 40 && gate_event_id.GetCount() > 0 && child_vid == "v1" &&
	      changed.Find(".multiatlas/gates/v1.json") >= 0 && changed.Find("family-vision-public.ev-gate-v1-" + source_head + ".json") >= 0 &&
	      changed.Find('\n') >= 0,
	      "adapter commit contains only its event and gate evidence marker");
	String proof_body = "{\"vid\":\"m1\",\"subject_kind\":\"element\",\"subject_id\":\"e_track_vision\","
	                    "\"type\":\"stage\",\"action\":\"RecordGatePass\","
	                    "\"effects\":[\"track:vision:tested:v1\"],\"text\":\"Track vision passed gate v1 at commit " + gate_commit + "\","
	                    "\"gate_track_id\":\"vision\",\"gate_version\":\"v1\",\"gate_event_id\":\"" + gate_event_id + "\","
	                    "\"gate_child_vid\":\"" + child_vid + "\",\"git_commit_hash\":\"" + gate_commit + "\"}";
	HttpReply recorded_gate = Request("POST", "/api/multiatlas/public/meta/event", proof_body,
	                                  admin_cookie, admin_csrf);
	ValueMap recorded_data = ValueMap(Json(recorded_gate).Get("data"));
	ValueMap recorded_event = recorded_data.Get("event");
	ValueArray normalized_requires = recorded_event.Get("requires");
	Check(recorded_gate.status == 200 && (bool)recorded_data.Get("idempotent") &&
	      ValueArray(recorded_event.Get("effects")).IsEmpty() && normalized_requires.GetCount() == 1 &&
	      AsString(normalized_requires[0]) == "vision:tested:v1",
	      "repeat proof is an idempotent requirement and cannot mint a child fact");
	String first_evidence_head = gate_commit;
	Check(Run("mv", {task_path + ".done", task_path}) &&
	      Run("sh", {adapter_script, "--once"}, manager_root) && FileExists(task_path + ".done") &&
	      Capture("git", {"rev-parse", "--verify", "HEAD"}, public_vision) == first_evidence_head,
	      "retry after commit resends the matching proof without another child commit");
	HttpReply gate_check = Request("GET", "/api/multiatlas/public/check", String(), admin_cookie, admin_csrf);
	ValueMap gate_check_data = ValueMap(Json(gate_check).Get("data"));
	Check(gate_check.status == 200 && (bool)gate_check_data.Get("ok"),
	      "verified gate satisfies meta replay immediately");
	SetEnv("MA_API_BASE", ""); SetEnv("MA_API_ORIGIN", ""); SetEnv("MA_META_VID", "");
	SetEnv("MA_COOKIE", ""); SetEnv("MA_CSRF", ""); SetEnv("MA_GATE_QUEUE_DIR", "");
	SetEnv("MA_ENSEMBLE", ""); SetEnv("MA_TRACK_ID", ""); SetEnv("MA_TRACK_REPO", "");
	SetEnv("MA_TRACK_BRANCH", ""); SetEnv("MA_TRACK_FAMILY", ""); SetEnv("MA_TRACK_STORE", "");
	SetEnv("MA_GATE_TESTER", ""); SetEnv("MA_RECORD_GATE_SCRIPT", "");
	host.StopEngines(); host.Quit(); host_thread.Wait();
}

END_UPP_NAMESPACE

CONSOLE_APP_MAIN
{
	using namespace Upp;
	s_root = AppendFileName(GetTempPath(), "multiatlas-api-test-" + AsString((int)msecs()) + "-" + AsString((int)Random(100000)));
	RealizeDirectory(s_root);
	HttpTest();
	Cout() << "MultiAtlasApiTest checks: " << (s_checks - s_failures) << "/" << s_checks << " passed\n";
	DeleteFolderDeep(s_root);
	SetExitCode(s_failures ? 1 : 0);
}
