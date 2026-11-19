#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include <WebEngine/SessionRoutes.h>
#include <MultiAtlas/MultiAtlasSite.h>
#include <MultiAtlas/MultiAtlasModel.h>
#include <MultiAtlas/MultiAtlasReplay.h>
#include <MultiAtlas/MultiAtlasReplayLoader.h>
#include <MultiAtlas/MultiAtlasStore.h>
#include <MultiAtlas/MultiAtlasWarningLedger.h>

NAMESPACE_UPP

static int s_checks, s_failures;
static String s_root;
static const int API_PORT = 18483;

struct E2EHost : WebEngineHost {
	E2EHost() { port = 18482; threads = 4; prefork = 0; }
};
static E2EHost *s_host;
static void HostMain() { s_host->Run(); }

static void Check(bool ok, const String& text)
{
	s_checks++;
	Cout() << (ok ? "ok   " : "FAIL ") << text << '\n';
	if(!ok) s_failures++;
}

static bool HasAtom(const Vector<String>& values, const String& atom)
{
	for(const String& value : values) if(value == atom) return true;
	return false;
}

static String Repo(const String& id) { return AppendFileName(s_root, id); }

static String Store(const String& repo, const String& family)
{
	String root = AppendFileName(repo, "multiatlas/" + family + "/store");
	RealizeDirectory(root);
	return AppendFileName(root, "data");
}

static bool Git(const String& repo, const Vector<String>& args, String *captured = NULL)
{
	// Fixture repositories are direct children of this unique private /tmp root.
	String nr = NormalizePath(repo), nt = NormalizePath(s_root);
	if(!IsFullPath(nt) || nr == nt || NormalizePath(GetFileDirectory(nr)) != nt ||
	   nr != NormalizePath(AppendFileName(nt, GetFileName(nr)))) {
		Cout() << "refusing fixture git outside private root: " << repo << '\n';
		return false;
	}
	Vector<String> argv;
	argv.Add("-C"); argv.Add(nr);
	for(const String& arg : args) argv.Add(arg);
	LocalProcess p;
	if(!p.Start2("git", argv, NULL, NULL)) return false;
	p.CloseWrite();
	String output, errors;
	for(;;) { String out, err; bool more = p.Read2(out, err); output << out; errors << err; if(!more) break; }
	if(captured) *captured = TrimBoth(output);
	int code = p.GetExitCode();
	if(code) Cout() << "git fixture command failed (" << code << "): " << TrimBoth(errors) << '\n';
	return code == 0;
}

static bool InitRepo(const String& repo, const String& branch)
{
	return Git(repo, {"init", "-q"}) &&
	       Git(repo, {"config", "user.email", "multiatlas-e2e@example.invalid"}) &&
	       Git(repo, {"config", "user.name", "MultiAtlas E2E"}) &&
	       Git(repo, {"checkout", "-B", branch});
}

static bool Commit(const String& repo, const String& message)
{
	return Git(repo, {"add", "-A"}) && Git(repo, {"commit", "-q", "-m", message});
}

static MultiAtlasVersion Version(const String& id, int code, const String& requirement = String())
{
	MultiAtlasVersion v; v.vid = id; v.code = code;
	if(!requirement.IsEmpty()) v.requires.Add(requirement);
	return v;
}

static Value TrackConfig(const String& repo, const String& id, const String& family)
{
	ValueMap track; track.Add("track_id", id); track.Add("family", family);
	track.Add("repo", repo); track.Add("branch", "track/" + id);
	track.Add("store", "multiatlas/" + family + "/store");
	ValueArray editors; track.Add("editors", editors); return track;
}

static bool SeedTrack(const String& repo, const String& id, const String& family,
                      const String& branch, const String& gate_requirement = String())
{
	String data = Store(repo, family), err;
	MultiAtlasTrack track; track.track_id = id; track.family = family; track.branch = branch;
	track.versions.Add(Version("v1", 100));
	if(!gate_requirement.IsEmpty()) track.versions.Add(Version("v2", 200, gate_requirement));
	if(!MultiAtlasSaveTrackFamily(data, family, track, RecordAbsent(), err)) { Cout() << err << '\n'; return false; }
	MultiAtlasMap map; map.map = id; map.track_id = id; map.version = "v1";
	if(!MultiAtlasSaveTrackMap(data, family, map, RecordAbsent(), err)) { Cout() << err << '\n'; return false; }
	MultiAtlasRules rules;
	if(!MultiAtlasSaveTrackRules(data, family, rules, RecordAbsent(), err)) { Cout() << err << '\n'; return false; }
	return true;
}

static bool AddEvent(const String& repo, const String& family, const String& track,
                     const String& id, const String& version, const String& type,
                     const Vector<String>& effects = Vector<String>(),
                     const Vector<String>& requires = Vector<String>())
{
	MultiAtlasEvent e; e.id = id; e.at = id; e.track_id = track; e.vid = version;
	e.subject_kind = "task"; e.subject_id = id; e.type = type;
	e.effects <<= effects; e.requires <<= requires;
	String err;
	return MultiAtlasPutTrackEvent(Store(repo, family), family, track, e, err);
}

static bool WorkerEventCommit(const String& repo, const String& family,
                              const String& track, const String& id)
{
	return AddEvent(repo, family, track, id, "v1", "worker-update") &&
	       SaveFile(AppendFileName(repo, id + ".txt"), "concurrent worker event\n") &&
	       Commit(repo, "worker event " + track);
}

struct HttpReply : Moveable<HttpReply> { int status = 0; String headers, body; };

static HttpReply Request(const String& method, const String& path, const String& body = String(),
                         const String& cookie = String(), const String& csrf = String())
{
	HttpReply reply; TcpSocket socket; socket.Timeout(5000);
	if(!socket.Connect("127.0.0.1", API_PORT)) return reply;
	String request = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\nOrigin: http://127.0.0.1:18483\r\n";
	if(!body.IsEmpty()) request << "Content-Type: application/json\r\nContent-Length: " << body.GetCount() << "\r\n";
	if(!cookie.IsEmpty()) request << "Cookie: " << cookie << "\r\n";
	if(!csrf.IsEmpty()) request << "X-WF-CSRF: " << csrf << "\r\n";
	request << "\r\n" << body; socket.Put(~request, request.GetCount());
	String response;
	for(;;) { String part = socket.Get(4096); if(part.IsEmpty()) break; response.Cat(part); }
	int end = response.Find("\r\n\r\n"); if(end < 0) return reply;
	reply.headers = response.Left(end); reply.body = response.Mid(end + 4);
	int space = reply.headers.Find(' '); if(space >= 0) reply.status = atoi(~reply.headers.Mid(space + 1, 3));
	return reply;
}

static String LoginAdmin(String& csrf)
{
	String body = "username=e2e-admin&password=e2e-password";
	TcpSocket socket; socket.Timeout(5000);
	if(!socket.Connect("127.0.0.1", API_PORT)) return String();
	String request = "POST /session/login HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: http://127.0.0.1:18483\r\n"
	                 "Accept: application/json\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
	                 AsString(body.GetCount()) + "\r\nConnection: close\r\n\r\n" + body;
	socket.Put(~request, request.GetCount()); String raw;
	for(;;) { String part = socket.Get(4096); if(part.IsEmpty()) break; raw.Cat(part); }
	int split = raw.Find("\r\n\r\n"); if(split < 0) return String();
	Value parsed; try { parsed = ParseJSON(raw.Mid(split + 4)); } catch(...) { return String(); }
	if(!parsed.Is<ValueMap>()) return String();
	csrf = AsString(ValueMap(parsed).Get("csrf"));
	int p = raw.Find("Set-Cookie: "); if(p < 0) p = raw.Find("set-cookie: ");
	if(p < 0) return String(); p += 12; int end = raw.Find(';', p);
	return end < 0 ? raw.Mid(p) : raw.Mid(p, end - p);
}

static void Run()
{
	byte nonce[16];
	for(int i = 0; i < 16; i++) nonce[i] = (byte)Random(256);
	s_root = AppendFileName("/tmp", "multiatlas-e2e-" + ToLower(HexString(nonce, 16)));
	const String vision = Repo("vision"), ai = Repo("ai"), meta = Repo("meta");
	const String vf = "e2e-vision", af = "e2e-ai", mf = "e2e-meta";
	bool dirs = RealizeDirectory(vision) && RealizeDirectory(ai) && RealizeDirectory(meta);
	Check(dirs, "three isolated temporary repository fixtures created");
	if(!dirs) return;
	bool seeded = SeedTrack(vision, "vision", vf, "track/vision") &&
	              SeedTrack(ai, "ai", af, "track/ai", "vision:tested:v1");
	String md = Store(meta, mf), err;
	MultiAtlasMetaFamily mfam; mfam.ensemble_name = "synthetic"; mfam.title = "Synthetic"; mfam.meta_family = mf;
	mfam.versions.Add(Version("m1", 100, "vision:tested:v1"));
	seeded = seeded && MultiAtlasSaveMetaFamily(md, mf, mfam, RecordAbsent(), err);
	MultiAtlasMap mmap; mmap.map = "meta"; mmap.track_id = "meta"; mmap.version = "m1";
	MultiAtlasRules rules;
	seeded = seeded && MultiAtlasSaveTrackMap(md, mf, mmap, RecordAbsent(), err) &&
	         MultiAtlasSaveTrackRules(md, mf, rules, RecordAbsent(), err);
	Check(seeded, "track stores and meta-map seeded with a proof-gated meta version");
	bool git_ready = InitRepo(vision, "track/vision") && InitRepo(ai, "track/ai") && InitRepo(meta, "meta/main");
	git_ready = git_ready && Commit(vision, "seed vision") && Commit(ai, "seed ai") && Commit(meta, "seed meta");
	Check(git_ready, "three fixture branches initialized and seeded through explicit git -C calls");
	if(!git_ready) return;
	bool w1 = false, w2 = false;
	Thread worker1, worker2;
	bool started1 = worker1.Run([&] { w1 = WorkerEventCommit(vision, vf, "vision", "vision-worker-event"); });
	bool started2 = worker2.Run([&] { w2 = WorkerEventCommit(ai, af, "ai", "ai-worker-event"); });
	worker1.Wait(); worker2.Wait();
	Check(started1 && started2 && w1 && w2,
	      "Track 1 and Track 2 workers concurrently append events and commit independently");

	MultiAtlasRoadmapMessage warning; warning.severity = "warning"; warning.track_id = "vision";
	warning.vid = "v1"; warning.subject_kind = "task"; warning.subject_id = "review-note";
	warning.rule = "review-note"; warning.text = "operator reviewed synthetic finding";
	String warning_id = MultiAtlasWarningId("vision", warning), ack_rev;
	MultiAtlasWarningAck ack; ack.track_id = "vision"; ack.warning_id = warning_id;
	ack.rule = warning.rule; ack.vid = "v1"; ack.actor = "e2e"; ack.comment = "acknowledged"; ack.at = "fixture";
	bool ack_saved = MultiAtlasSaveTrackAck(Store(vision, vf), "vision", ack, RecordAbsent(), ack_rev, err);
	Check(ack_saved && ack_rev == "1", "Track 1 warning acknowledgment persisted in Track 1 store");
	bool ai_update = AddEvent(ai, af, "ai", "ai-second-update", "v1", "worker-update") &&
	                 SaveFile(AppendFileName(ai, "second-update.txt"), "later AI work\n") && Commit(ai, "second AI update");
	Vector<MultiAtlasWarningAck> vision_acks, ai_acks;
	bool ack_isolated = MultiAtlasLoadTrackAcks(Store(vision, vf), "vision", vision_acks, err) &&
	                    MultiAtlasLoadTrackAcks(Store(ai, af), "ai", ai_acks, err);
	Check(ai_update && ack_isolated && vision_acks.GetCount() == 1 &&
	      vision_acks[0].warning_id == warning_id && ai_acks.IsEmpty(),
	      "Track 1 acknowledgment remains intact after a separate Track 2 update");

	String source_commit;
	bool gate = Git(vision, {"rev-parse", "HEAD"}, &source_commit);
	String gate_event = "vision-gate-v1";
	if(gate) {
		MultiAtlasEvent proof_event; proof_event.id = gate_event; proof_event.at = "fixture";
		proof_event.track_id = "vision"; proof_event.vid = "v1"; proof_event.subject_kind = "element";
		proof_event.subject_id = "gate-v1"; proof_event.type = "gate-pass"; proof_event.action = "GatePass";
		proof_event.text = "Local gate verified at source commit " + ToLower(source_commit);
		proof_event.effects.Add("tested:v1");
		gate = MultiAtlasPutTrackEvent(Store(vision, vf), vf, "vision", proof_event, err);
	}
	ValueMap proof; proof.Add("track_id", "vision"); proof.Add("gate_version", "v1"); proof.Add("result", "passed");
	proof.Add("event_id", gate_event); proof.Add("source_commit", ToLower(source_commit)); proof.Add("child_vid", "v1");
	String proof_path = AppendFileName(vision, ".multiatlas/gates/v1.json");
	gate = gate && RealizeDirectory(GetFileDirectory(proof_path)) && SaveFile(proof_path, AsJSON(proof)) &&
	       Commit(vision, "pass vision gate v1");
	Check(gate, "Track 1 gate proof is committed on its branch");
	Check(gate && Git(ai, {"checkout", "-B", "track/ai"}) && Git(meta, {"checkout", "-B", "meta/main"}),
	      "worker branches are at their intended names before local bridge API starts");

	MultiAtlasEnsembleDef ensemble; ensemble.name = "synthetic"; ensemble.title = "Synthetic";
	ensemble.meta_family = mf; ensemble.meta_repo = meta; ensemble.meta_store = "multiatlas/" + mf + "/store";
	MultiAtlasTrackDef vd; vd.track_id = "vision"; vd.family = vf; vd.repo = vision; vd.branch = "track/vision"; vd.store = "multiatlas/" + vf + "/store";
	MultiAtlasTrackDef ad; ad.track_id = "ai"; ad.family = af; ad.repo = ai; ad.branch = "track/ai"; ad.store = "multiatlas/" + af + "/store";
	ensemble.tracks.Add(pick(vd)); ensemble.tracks.Add(pick(ad));
	String site_data = AppendFileName(s_root, "site-data"), engine_root = AppendFileName(site_data, "engine");
	String registry_dir = AppendFileName(engine_root, "multiatlas"); RealizeDirectory(registry_dir);
	ValueMap mcfg; mcfg.Add("family", mf); mcfg.Add("repo", meta); mcfg.Add("store", ensemble.meta_store);
	ValueArray tcfg; tcfg.Add(TrackConfig(vision, "vision", vf)); tcfg.Add(TrackConfig(ai, "ai", af));
	ValueMap ecfg; ecfg.Add("name", "synthetic"); ecfg.Add("title", "Synthetic"); ecfg.Add("meta", mcfg); ecfg.Add("tracks", tcfg);
	ValueArray earr; earr.Add(ecfg); ValueMap rconfig; rconfig.Add("ensembles", earr);
	bool registry = SaveFile(AppendFileName(registry_dir, "projects.json"), AsJSON(rconfig));
	EngineSite& site = TheWebEngine().AddSite("multiatlas", "MultiAtlas", "http://127.0.0.1:18483", AppendFileName(engine_root, "multiatlas"));
	InstallMultiAtlasSite(site); InstallSessionRoutes(site);
	Vector<String> admin_roles; admin_roles.Add("admin"); String user_id; bool existed;
	site.Accounts().CreateUser("e2e-admin", "e2e-password", "E2E Admin", admin_roles, user_id, existed);
	bool user = true;
	E2EHost host; host.BindSite(API_PORT, "multiatlas"); s_host = &host; Thread host_thread; host_thread.Run(HostMain);
	for(int i = 0; i < 100 && Request("GET", "/api/multiatlas/ensembles").status == 0; i++) Sleep(20);
	String csrf, cookie = LoginAdmin(csrf);
	Check(registry && user && !cookie.IsEmpty() && !csrf.IsEmpty(), "isolated MultiAtlas API host and admin session are ready");
	String gate_commit; Git(vision, {"rev-parse", "HEAD"}, &gate_commit);
	String ai_commit, meta_seed_commit;
	Git(ai, {"rev-parse", "HEAD"}, &ai_commit);
	Git(meta, {"rev-parse", "HEAD"}, &meta_seed_commit);
	Cout() << "fixture commits before bridge: vision=" << gate_commit << " ai=" << ai_commit
	       << " meta=" << meta_seed_commit << '\n';
	String proof_body = "{\"vid\":\"m1\",\"subject_kind\":\"element\",\"subject_id\":\"e_track_vision\","
	                    "\"type\":\"stage\",\"action\":\"RecordGatePass\","
	                    "\"effects\":[\"track:vision:tested:v1\"],\"text\":\"Track vision passed gate v1 at commit " + gate_commit + "\","
	                    "\"gate_track_id\":\"vision\",\"gate_version\":\"v1\",\"gate_event_id\":\"" + gate_event + "\","
	                    "\"gate_child_vid\":\"v1\",\"git_commit_hash\":\"" + gate_commit + "\"}";
	HttpReply bridge_reply = Request("POST", "/api/multiatlas/synthetic/meta/event", proof_body, cookie, csrf);
	ValueMap bridge_json; try { bridge_json = ValueMap(ParseJSON(bridge_reply.body)); } catch(...) {}
	bool bridged = bridge_reply.status == 201 && (bool)bridge_json.Get("ok");
	Check(bridged, "local RecordGatePass API hook validates committed proof and writes the meta bridge event");
	bool bridge_committed = bridged && Commit(meta, "record verified vision bridge");
	Check(bridge_committed, "meta repository commits the accepted bridge event");
	String meta_bridge_commit; Git(meta, {"rev-parse", "HEAD"}, &meta_bridge_commit);
	Cout() << "fixture commit after bridge: meta=" << meta_bridge_commit << '\n';
	MultiAtlasReplayInput input; Vector<MultiAtlasWarningAck> acknowledgments;
	bool loaded = MultiAtlasLoadReplayInput(ensemble, input, acknowledgments, err);
	MultiAtlasReplayContext context;
	bool replayed = loaded && MultiAtlasReplayEnsemble(input, context);
	bool bridge_in_replay = false;
	for(const MultiAtlasEvent& event : input.meta.events)
		if(event.action == "RecordGatePass" && HasAtom(event.requires, "vision:tested:v1"))
			bridge_in_replay = true;
	bool meta_gate_ready = replayed && HasAtom(context.exported_atoms_by_track.Get("vision"), "tested:v1");
	for(const MultiAtlasRoadmapMessage& message : context.messages)
		if(message.track_id == "meta" && message.literal == "vision:tested:v1" && message.severity == "warning")
			meta_gate_ready = false;
	Check(meta_gate_ready && bridge_in_replay && context.total_errors == 0,
	      "proof-backed API bridge is in meta replay and unlocks its dependent map version");
	host.StopEngines(); host.Quit(); host_thread.Wait();
	// Thread::GetCount counts active U++-started threads, not the main thread.
	for(int i = 0; i < 250 && Thread::GetCount() != 0; i++) Sleep(10);
	Check(Thread::GetCount() == 0, "local API listener and request threads have fully exited");

	DeleteFolderDeep(s_root);
}

END_UPP_NAMESPACE

CONSOLE_APP_MAIN
{
	using namespace Upp;
	Run();
	Cout() << "MultiAtlasE2ETest checks: " << (s_checks - s_failures) << "/" << s_checks << " passed\n";
	if(s_failures) SetExitCode(1);
}
