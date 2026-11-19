#include <Core/Core.h>
#include <MultiAtlas/MultiAtlasModel.h>
#include <MultiAtlas/MultiAtlasStore.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

static int s_checks, s_failures;
static String s_root, s_cli;
static const char *GATE_VERSION = "1.0.0";
static const char *TRIAL_VERSION = "1.0.999";

struct CliResult {
	int exit_code = -1;
	String out, err;
};

static void Check(bool ok, const String& message)
{
	s_checks++;
	if(!ok) s_failures++;
	Cout() << (ok ? "ok   " : "FAIL ") << message << '\n';
}

static void ShowFailure(const String& label, const CliResult& result)
{
	Cout() << "  " << label << " exit=" << result.exit_code << " stdout=" << TrimBoth(result.out)
	      << " stderr=" << TrimBoth(result.err) << '\n';
}

static CliResult Invoke(const Vector<String>& args)
{
	CliResult result;
	LocalProcess process;
	if(!process.Start2(s_cli, args, NULL, NULL)) {
		result.err = "could not start CLI: " + s_cli;
		return result;
	}
	process.CloseWrite();
	for(;;) {
		String out, err;
		bool more = process.Read2(out, err);
		result.out << out;
		result.err << err;
		if(!more) break;
	}
	result.exit_code = process.GetExitCode();
	return result;
}

static String Repo(const String& id)
{
	return AppendFileName(s_root, id);
}

static String Store(const String& repo, const String& family)
{
	String store = AppendFileName(repo, "multiatlas/" + family + "/store");
	RealizeDirectory(store);
	return AppendFileName(store, "data");
}

static bool Git(const String& repo, const Vector<String>& args, String *captured = NULL)
{
	// Repositories must be direct children of this invocation's private fixture
	// root. Never allow a test helper call to fall back to the process cwd.
	String normalized_root = NormalizePath(s_root);
	String normalized_repo = NormalizePath(repo);
	if(repo.IsEmpty() || normalized_repo == normalized_root ||
	   NormalizePath(GetFileDirectory(normalized_repo)) != normalized_root ||
	   normalized_repo != NormalizePath(AppendFileName(normalized_root, GetFileName(normalized_repo)))) {
		Cout() << "refusing Git command outside test fixture root: " << repo << '\n';
		s_failures++;
		return false;
	}
	Vector<String> argv;
	argv.Add("-C");
	argv.Add(repo);
	for(const String& arg : args)
		argv.Add(arg);
	LocalProcess process;
	if(!process.Start2("git", argv, NULL, NULL)) return false;
	process.CloseWrite();
	String output, errors;
	for(;;) {
		String out, err;
		bool more = process.Read2(out, err);
		output << out;
		errors << err;
		if(!more) break;
	}
	if(captured) *captured = TrimBoth(output);
	int code = process.GetExitCode();
	if(code != 0) Cout() << "fixture git command failed in " << repo << " (" << code << "): " << TrimBoth(errors) << " " << TrimBoth(output) << '\n';
	return code == 0;
}

static bool InitRepo(const String& repo, const String& branch)
{
	return Git(repo, {"init", "-q"}) &&
	       Git(repo, {"config", "user.email", "multiatlas-cli-test@example.invalid"}) &&
	       Git(repo, {"config", "user.name", "MultiAtlas CLI Test"}) &&
	       Git(repo, {"checkout", "-B", branch});
}

static bool CommitRepo(const String& repo, const String& message)
{
	return Git(repo, {"add", "-f", "-A"}) && Git(repo, {"commit", "-q", "-m", message});
}

static MultiAtlasVersion Version(const String& vid, int code)
{
	MultiAtlasVersion v;
	v.vid = vid;
	v.code = code;
	return v;
}

static bool SeedTrack(const String& repo, const String& track_id, const String& family,
                      const String& required = String())
{
	String data = Store(repo, family), error;
	MultiAtlasTrack track;
	track.track_id = track_id;
	track.family = family;
	track.branch = "track/" + track_id;
	track.versions.Add(Version("v1", 1000000));
	track.versions.Add(Version("trial-v1", 1000999));
	if(!required.IsEmpty()) track.versions[0].requires.Add(required);
	if(!MultiAtlasSaveTrackFamily(data, family, track, RecordAbsent(), error)) { Cout() << "seed track family: " << error << '\n'; return false; }

	MultiAtlasMap map;
	map.map = track_id;
	map.track_id = track_id;
	map.version = "v1";
	MultiAtlasElement item;
	item.id = "item-1";
	item.name = "Fixture item";
	item.kind = "task";
	item.status = "open";
	item.w = 100;
	item.h = 60;
	map.elements.Add(item);
	if(!MultiAtlasSaveTrackMap(data, family, map, RecordAbsent(), error)) { Cout() << "seed track map: " << error << '\n'; return false; }
	MultiAtlasRules rules;
	bool ok = MultiAtlasSaveTrackRules(data, family, rules, RecordAbsent(), error);
	if(!ok) Cout() << "seed track rules: " << error << '\n';
	return ok;
}

static bool SeedMeta(const String& repo, const String& family, const String& required)
{
	String data = Store(repo, family), error;
	MultiAtlasMetaFamily meta;
	meta.ensemble_name = "cli-fixture";
	meta.title = "CLI fixture";
	meta.meta_family = family;
	meta.versions.Add(Version("m1", 1000000));
	if(!required.IsEmpty()) meta.versions[0].requires.Add(required);
	auto& trial = meta.versions.Add(Version("m-trial", 1000999));
	trial.requires.Add("alpha:tested:1.0.999");
	meta.versions.Add(Version("m-next", 1001000)).requires.Add("tested:1.0.999");
	if(!MultiAtlasSaveMetaFamily(data, family, meta, RecordAbsent(), error)) { Cout() << "seed meta family: " << error << '\n'; return false; }
	MultiAtlasMap map;
	map.map = "meta";
	map.track_id = "meta";
	map.version = "m1";
	if(!MultiAtlasSaveTrackMap(data, family, map, RecordAbsent(), error)) { Cout() << "seed meta map: " << error << '\n'; return false; }
	MultiAtlasRules rules;
	bool ok = MultiAtlasSaveTrackRules(data, family, rules, RecordAbsent(), error);
	if(!ok) Cout() << "seed meta rules: " << error << '\n';
	return ok;
}

static bool AddEvent(const String& repo, const String& family, const String& track_id,
                    const String& id, const String& vid, const String& kind,
                    const String& subject, const String& type, const String& text,
                    const Vector<String>& effects = Vector<String>(), const String& action = String())
{
	MultiAtlasEvent event;
	event.id = id;
	event.at = id;
	event.track_id = track_id;
	event.vid = vid;
	event.subject_kind = kind;
	event.subject_id = subject;
	event.type = type;
	event.action = action;
	event.text = text;
	event.effects <<= effects;
	String error;
	return MultiAtlasPutTrackEvent(Store(repo, family), family, track_id, event, error);
}

static bool HasEvent(const String& repo, const String& family, const String& track_id,
                     const String& id, const String& type)
{
	Vector<MultiAtlasEvent> events;
	String error;
	if(!MultiAtlasLoadTrackEvents(Store(repo, family), family, track_id, events, error)) return false;
	for(const MultiAtlasEvent& event : events)
		if(event.id.StartsWith(id) && event.type == type) return true;
	return false;
}

static Value TrackConfig(const String& repo, const String& id, const String& family)
{
	ValueMap t;
	t.Add("track_id", id);
	t.Add("family", family);
	t.Add("repo", repo);
	t.Add("branch", "track/" + id);
	t.Add("store", "multiatlas/" + family + "/store");
	ValueArray editors;
	t.Add("editors", editors);
	return t;
}

static bool WriteRegistry(const String& invalid_contract)
{
	String suffix = invalid_contract.IsEmpty() ? "clean" : "invalid";
	String meta_family = "cli-meta-" + suffix;
	String alpha_family = "cli-alpha-" + suffix;
	String beta_family = "cli-beta-" + suffix;
	String meta_repo = Repo("meta-repo-" + suffix), alpha_repo = Repo("alpha-repo-" + suffix), beta_repo = Repo("beta-repo-" + suffix);
	for(const String& repo : {meta_repo, alpha_repo, beta_repo})
		if(!RealizeDirectory(repo)) return false;
	if(!SeedMeta(meta_repo, meta_family, String("alpha:tested:") + GATE_VERSION)) { Cout() << "meta fixture setup failed\n"; return false; }
	if(!SeedTrack(alpha_repo, "alpha", alpha_family)) { Cout() << "alpha fixture setup failed\n"; return false; }
	String beta_contract = invalid_contract.IsEmpty() ? String("alpha:tested:") + GATE_VERSION : invalid_contract;
	if(!SeedTrack(beta_repo, "beta", beta_family, beta_contract)) { Cout() << "beta fixture setup failed\n"; return false; }
	if(!AddEvent(alpha_repo, alpha_family, "alpha", "task-proposed-1", "v1", "task", "task-1", "proposed", "Implement fixture")) { Cout() << "proposal event setup failed\n"; return false; }
	for(const String& repo : {meta_repo, alpha_repo, beta_repo})
		if(!SaveFile(AppendFileName(repo, "README.md"), "MultiAtlas CLI fixture\n")) return false;
	if(!InitRepo(meta_repo, "main") || !InitRepo(alpha_repo, "track/alpha") || !InitRepo(beta_repo, "track/beta")) { Cout() << "git initialization failed\n"; return false; }
	if(!CommitRepo(meta_repo, "seed meta fixture") || !CommitRepo(alpha_repo, "seed alpha fixture") || !CommitRepo(beta_repo, "seed beta fixture")) { Cout() << "git seed commit failed\n"; return false; }

	ValueMap meta;
	meta.Add("family", meta_family);
	meta.Add("repo", meta_repo);
	meta.Add("store", "multiatlas/" + meta_family + "/store");
	ValueArray tracks;
	tracks.Add(TrackConfig(alpha_repo, "alpha", alpha_family));
	tracks.Add(TrackConfig(beta_repo, "beta", beta_family));
	ValueMap ensemble;
	ensemble.Add("name", "cli-fixture");
	ensemble.Add("title", "CLI fixture");
	ensemble.Add("meta", meta);
	ensemble.Add("tracks", tracks);
	ValueArray ensembles;
	ensembles.Add(ensemble);
	ValueMap root;
	root.Add("ensembles", ensembles);
	String registry = AppendFileName(AppendFileName(AppendFileName(s_root, "site/engine"), "multiatlas"), "projects.json");
	return RealizeDirectory(GetFileDirectory(registry)) && SaveFile(registry, AsJSON(root));
}

static bool AddCommittedGateProof(const String& repo, const String& family)
{
	String source_commit;
	if(!Git(repo, {"rev-parse", "HEAD"}, &source_commit) || source_commit.GetCount() != 40) return false;
	MultiAtlasEvent event;
	event.id = "verified-gate-v1";
	event.at = "2026-01-01T00:00:00Z";
	event.track_id = "alpha";
	event.vid = "v1";
	event.subject_kind = "element";
	event.subject_id = "gate-v1";
	event.type = "gate-pass";
	event.action = "GatePass";
	event.text = "Local gate verified at source commit " + ToLower(source_commit);
	event.effects.Add(String("tested:") + GATE_VERSION);
	String error;
	if(!MultiAtlasPutTrackEvent(Store(repo, family), family, "alpha", event, error)) return false;
	ValueMap proof;
	proof.Add("track_id", "alpha");
	proof.Add("gate_version", GATE_VERSION);
	proof.Add("result", "passed");
	proof.Add("event_id", event.id);
	proof.Add("source_commit", ToLower(source_commit));
	proof.Add("child_vid", "v1");
	String path = AppendFileName(repo, String(".multiatlas/gates/") + GATE_VERSION + ".json");
	return RealizeDirectory(GetFileDirectory(path)) && SaveFile(path, AsJSON(proof)) && CommitRepo(repo, "record verified gate fixture");
}

static void ResolveCli()
{
	const Vector<String>& args = CommandLine();
	if(!args.IsEmpty()) s_cli = args[0];
	if(s_cli.IsEmpty()) s_cli = GetEnv("MULTIATLAS_VERSION_BIN");
	if(s_cli.IsEmpty()) s_cli = AppendFileName(GetCurrentDirectory(), "bin/MultiAtlasVersion");
	if(!FileExists(s_cli)) {
		String message = "CLI executable not found: " + s_cli + "; pass its path as the first argument or set MULTIATLAS_VERSION_BIN";
		Cout() << "FAIL " << message << '\n';
		s_failures++;
	}
}

static const char *TRIAL_DIR = ".multiatlas/meta-trials/1.0.999";

static bool WriteTrialEvidence(const String& repo, const String& source,
                              const String& case_name)
{
	ValueMap check;
	check.Add("name", "integration");
	check.Add("result", case_name == "failed-check" ? "failed" : "passed");
	ValueArray checks;
	checks.Add(check);
	ValueMap report;
	report.Add("source_commit", source);
	report.Add("version", TRIAL_VERSION);
	report.Add("result", "passed");
	report.Add("checks", checks);
	String report_json = AsJSON(report);
	ValueMap marker;
	marker.Add("schema", "multiatlas-meta-trial/v1");
	marker.Add("ensemble", case_name == "identity" ? "another-ensemble" : "cli-fixture");
	marker.Add("track_id", "meta");
	marker.Add("version", case_name == "wrong-version" ? "1.1.999" : TRIAL_VERSION);
	marker.Add("meta_vid", "m-trial");
	marker.Add("source_commit", source);
	marker.Add("result", case_name == "failed" ? "failed" : "passed");
	marker.Add("report", case_name == "traversal" ? "../report.json" : "report.json");
	marker.Add("report_sha256", SHA256String(case_name == "hash" ? "different report" : report_json));
	String dir = AppendFileName(repo, TRIAL_DIR);
	return RealizeDirectory(dir) && SaveFile(AppendFileName(dir, "report.json"), report_json) &&
	       SaveFile(AppendFileName(dir, "evidence.json"), AsJSON(marker));
}

static void MetaTrialTests(const String& site, const String& meta_repo, const String& meta_family,
                           const String& alpha_repo, const String& alpha_family)
{
	auto trial = [&](const String& version = TRIAL_VERSION) {
		return Invoke({site, "--ensemble", "cli-fixture", "--meta-trial-pass", version});
	};
	auto advance = [&](const String& milestone) {
		return Invoke({site, "--ensemble", "cli-fixture", "--meta-advance", milestone});
	};
	auto rejected = [&](const CliResult& result, const String& message, const String& label) {
		bool pass = result.exit_code == 1 && result.out.Find(message) >= 0;
		if(!pass) ShowFailure(label, result);
		Check(pass, label);
	};

	rejected(trial("m1"), "configured .999", "trial rejects non-gate versions");
	rejected(trial("../../1.0.999"), "configured .999", "trial rejects malformed version paths");
	rejected(trial(), "committed meta trial evidence is missing", "trial rejects missing evidence");
	String source;
	if(!Git(meta_repo, {"rev-parse", "HEAD"}, &source)) {
		Check(false, "retrieve tested source commit");
		return;
	}
	if(!WriteTrialEvidence(meta_repo, source, "valid")) {
		Check(false, "write local evidence");
		return;
	}
	rejected(trial(), "committed meta trial evidence is missing", "trial rejects uncommitted evidence");
	rejected(advance("m-next"), "not the next", "advancement cannot skip a pending trial milestone");

	auto evidence_case = [&](const String& case_name, const String& message, const String& label) {
		bool prepared = Git(meta_repo, {"reset", "--mixed", source}) &&
		                WriteTrialEvidence(meta_repo, source, case_name) &&
		                Git(meta_repo, {"add", "-f", "--", TRIAL_DIR});
		if(prepared && case_name == "missing-report")
			prepared = Git(meta_repo, {"rm", "-f", "--cached", "--", String(TRIAL_DIR) + "/report.json"});
		if(prepared && case_name == "source-change")
			prepared = SaveFile(AppendFileName(meta_repo, "README.md"), "Untested code change\n") &&
			           Git(meta_repo, {"add", "--", "README.md"});
		prepared = prepared && Git(meta_repo, {"commit", "-q", "-m", "record " + case_name + " trial evidence"});
		if(prepared) rejected(trial(), message, label);
		else Check(false, label);
	};
	evidence_case("failed", "passing result", "trial rejects failed evidence");
	evidence_case("identity", "identity", "trial rejects evidence belonging to another ensemble");
	evidence_case("wrong-version", "version", "trial rejects evidence for another version");
	evidence_case("traversal", "report name", "trial rejects report path traversal");
	evidence_case("hash", "SHA-256 does not match", "trial rejects report content hash mismatch");
	evidence_case("failed-check", "unique name and a passing result", "trial rejects a failed report check");
	evidence_case("missing-report", "regular report must both be committed", "trial rejects a report absent from HEAD");
	evidence_case("source-change", "evidence commit also changes", "trial rejects unrelated changes in the evidence commit");
	Check(Git(meta_repo, {"checkout", source, "--", "README.md"}), "restore tested source tree");
	evidence_case("valid", "missing alpha:tested:1.0.999", "trial requires a replayed child gate");
	rejected(advance("m-trial"), "missing alpha:tested:1.0.999", "trial milestone requires its child gate");

	String evidence_head;
	bool stale = Git(meta_repo, {"rev-parse", "HEAD"}, &evidence_head) &&
	             Git(meta_repo, {"commit", "--allow-empty", "-q", "-m", "move main beyond evidence"});
	if(stale) rejected(trial(), "sole parent of current main HEAD", "trial rejects a stale source commit");
	else Check(false, "advance main for stale evidence fixture");
	bool wrong_branch = Git(meta_repo, {"reset", "--mixed", evidence_head}) &&
	                    Git(meta_repo, {"checkout", "-b", "not-main"});
	if(wrong_branch) rejected(trial(), "worktree root on main", "trial rejects other worktree branches");
	else Check(false, "switch branch for trial fixture");
	if(!Git(meta_repo, {"checkout", "main"})) {
		Check(false, "return to meta main");
		return;
	}
	String report_path = AppendFileName(AppendFileName(meta_repo, TRIAL_DIR), "report.json");
	Check(SaveFile(report_path, "uncommitted replacement"), "modify committed trial report locally");
	rejected(trial(), "uncommitted changes", "trial rejects local changes to its committed report");
	Check(Git(meta_repo, {"checkout", "--", String(TRIAL_DIR) + "/report.json"}), "restore committed report");

	Check(!HasEvent(meta_repo, meta_family, "meta", "meta-trial-", "gate-pass"),
	      "rejected trials append no gate events");
	Check(AddEvent(alpha_repo, alpha_family, "alpha", "trial-gate", "trial-v1", "element",
	               "trial", "gate-pass", "child trial passed", {"tested:1.0.999"}, "GatePass") &&
	      CommitRepo(alpha_repo, "commit child trial gate"), "child trial gate now replays");
	rejected(advance("m-trial"), "missing tested:1.0.999", "trial milestone requires a meta-owned test pass");
	Check(AddEvent(meta_repo, meta_family, "meta", "legacy-trial-milestone", "m-trial", "milestone",
	               "m-trial", "milestone", "Legacy milestone without a trial event"),
	      "seed a legacy milestone that advanced without its trial gate");
	rejected(advance("m-next"), "missing tested:1.0.999", "later milestone requires the prior meta trial gate");
	String legacy_path = AppendFileName(Store(meta_repo, meta_family), "events/" + meta_family + ".ev-legacy-trial-milestone.json");
	Check(DeleteFile(legacy_path), "remove legacy milestone fixture before recording trial");
	CliResult recorded = trial("m-trial");
	if(recorded.exit_code != 0) ShowFailure("meta-trial-pass", recorded);
	Check(recorded.exit_code == 0 &&
	      HasEvent(meta_repo, meta_family, "meta", "meta-trial-1.0.999", "gate-pass"),
	      "committed passing evidence records the meta-owned trial event");
	CliResult next = Invoke({site, "--ensemble", "cli-fixture", "--meta-check"});
	Check(next.exit_code == 0 && next.out.Find("next=m-trial") >= 0,
	      "recording evidence leaves the trial milestone unadvanced");
	Check(trial().exit_code == 1, "repeating the same trial cannot append a second gate event");
	rejected(advance("m-next"), "not the next", "recorded trial still requires sequential advancement");
	CliResult gate = advance("m-trial");
	if(gate.exit_code != 0) ShowFailure("advance trial", gate);
	Check(gate.exit_code == 0, "trial milestone advances after committed evidence and child gates");
	CliResult following = advance("m-next");
	if(following.exit_code != 0) ShowFailure("advance following milestone", following);
	Check(following.exit_code == 0, "next version advances with the prior meta trial gate");
}

static void CliTests()
{
	ResolveCli();
	if(s_failures) return;
	if(!WriteRegistry(String())) {
		Check(false, "clean mock ensemble and repository stores are seeded");
		return;
	}
	Check(true, "clean mock ensemble and repository stores are seeded");
	String site = AppendFileName(s_root, "site");
	String alpha_repo = Repo("alpha-repo-clean"), alpha_family = "cli-alpha-clean";
	String beta_repo = Repo("beta-repo-clean"), beta_family = "cli-beta-clean";
	String meta_repo = Repo("meta-repo-clean"), meta_family = "cli-meta-clean";
	CliResult blocked_meta = Invoke({site, "--ensemble", "cli-fixture", "--meta-check"});
	Check(blocked_meta.exit_code == 1, "meta check is blocked before required child gate proof");
	CliResult deps = Invoke({site, "--ensemble", "cli-fixture", "--deps"});
	Check(deps.exit_code == 0 && deps.out.Find("alpha -> beta") >= 0 && deps.out.Find("alpha -> meta") >= 0,
	      "dependency command prints the child DAG and topological order");
	CliResult status = Invoke({site, "--ensemble", "cli-fixture", "--status"});
	if(status.out.Find("track/alpha") < 0) ShowFailure("status branch capture", status);
	Check(status.exit_code == 0 && status.out.Find("alpha") >= 0 && status.out.Find("beta") >= 0,
	      "status command lists each configured track");
	CliResult status_json = Invoke({site, "--ensemble", "cli-fixture", "--status", "--json"});
	bool status_json_ok = false;
	try { ValueMap parsed = ParseJSON(status_json.out); status_json_ok = status_json.exit_code == 0 && ValueArray(parsed.Get("tracks")).GetCount() == 2; }
	catch(...) {}
	Check(status_json_ok, "status --json returns both configured tracks in a JSON object");
	CliResult next = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--next"});
	Check(next.exit_code == 0 && next.out.Find("task-1") >= 0, "next returns the proposed open task");
	CliResult done = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--task-done", "task-1", "--version", "v1", "--note", "implemented in fixture"});
	if(done.exit_code != 0 || !HasEvent(alpha_repo, alpha_family, "alpha", "taskdone-", "done")) ShowFailure("task-done", done);
	Check(done.exit_code == 0 && HasEvent(alpha_repo, alpha_family, "alpha", "taskdone-", "done"),
	      "task-done appends a completion event");
	CliResult blocked_gate = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--gate-check", GATE_VERSION});
	Check(blocked_gate.exit_code == 1, "gate check fails before tested gate proof exists");
	CliResult refused_gate = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--gate-pass", GATE_VERSION, "--commit", "unverified"});
	Check(refused_gate.exit_code == 1, "gate pass rejects an unverified commit proof");
	Check(AddCommittedGateProof(alpha_repo, alpha_family), "matching gate event and evidence marker are committed at HEAD");
	CliResult passed_gate = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--gate-check", GATE_VERSION});
	Check(passed_gate.exit_code == 0, "gate check passes once the tested fact is replayed");
	CliResult verified_gate = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--gate-pass", GATE_VERSION});
	String gate_head;
	Git(alpha_repo, {"rev-parse", "HEAD"}, &gate_head);
	CliResult recorded_gate = Invoke({site, "--ensemble", "cli-fixture", "--meta-record-gate", "alpha", GATE_VERSION, "--commit", gate_head});
	if(verified_gate.exit_code != 0 || recorded_gate.exit_code != 0) { ShowFailure("gate-pass", verified_gate); ShowFailure("meta-record-gate", recorded_gate); }
	Check(verified_gate.exit_code == 0 && recorded_gate.exit_code == 0 &&
	      HasEvent(meta_repo, meta_family, "meta", "gate-alpha-" + String(GATE_VERSION) + "-", "stage"),
	      "verified gate proof passes and can be recorded in the meta event log");
	CliResult clear_meta = Invoke({site, "--ensemble", "cli-fixture", "--meta-check"});
	if(clear_meta.exit_code != 0) ShowFailure("meta-check", clear_meta);
	Check(clear_meta.exit_code == 0, "meta check passes after the required child gate proof");
	CliResult clean = Invoke({site, "--ensemble", "cli-fixture", "--check"});
	Check(clean.exit_code == 0, "clean ensemble --check exits 0 (" + TrimBoth(clean.err) + ")");
	CliResult json = Invoke({site, "--ensemble", "cli-fixture", "--check", "--json"});
	bool json_ok = false;
	try {
		Value parsed = ParseJSON(json.out);
		json_ok = parsed.Is<ValueMap>() && (bool)ValueMap(parsed).Get("ok") && json.exit_code == 0;
	}
	catch(...) {}
	Check(json_ok, "clean --check --json emits parseable JSON and exits 0");
	CliResult advance = Invoke({site, "--ensemble", "cli-fixture", "--meta-advance", "m1"});
	if(advance.exit_code != 0) ShowFailure("meta-advance", advance);
	Vector<MultiAtlasEvent> meta_events;
	String meta_error;
	bool advanced = MultiAtlasLoadTrackEvents(Store(meta_repo, meta_family), meta_family, "meta", meta_events, meta_error);
	bool found_advance = false;
	for(const MultiAtlasEvent& event : meta_events)
		if(event.type == "milestone" && event.text.Find("Integrated milestone m1") >= 0) found_advance = true;
	Check(advance.exit_code == 0 && advanced && found_advance, "meta advance appends a milestone event");
	MetaTrialTests(site, meta_repo, meta_family, alpha_repo, alpha_family);

	CliResult feedback_alpha = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--feedback", "--category", "docs", "--severity", "friction", "--note", "alpha note"});
	CliResult feedback_beta = Invoke({site, "--ensemble", "cli-fixture", "--track", "beta", "--feedback", "--category", "tooling", "--severity", "suggestion", "--note", "beta note"});
	CliResult feedback = Invoke({site, "--ensemble", "cli-fixture", "--feedback-list", "--track", "alpha", "--unresolved"});
	if(feedback_alpha.exit_code != 0 || feedback_beta.exit_code != 0 || feedback.exit_code != 0) { ShowFailure("feedback alpha", feedback_alpha); ShowFailure("feedback beta", feedback_beta); ShowFailure("feedback-list", feedback); }
	Check(feedback_alpha.exit_code == 0 && feedback_beta.exit_code == 0 && feedback.exit_code == 0 &&
	      feedback.out.Find("alpha note") >= 0 && feedback.out.Find("beta note") < 0,
	      "feedback list filters to the selected track");
	CliResult feedback_json = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--feedback-list", "--json"});
	String feedback_id;
	try {
		ValueArray listed = ParseJSON(feedback_json.out);
		if(!listed.IsEmpty()) feedback_id = AsString(ValueMap(listed[0]).Get("id"));
	}
	catch(...) {}
	Vector<String> no_effects;
	bool resolved_seeded = !feedback_id.IsEmpty() && AddEvent(alpha_repo, alpha_family, "alpha", "resolve-feedback", "v1", "feedback", feedback_id, "resolution", "resolved", no_effects, "ResolveFeedback");
	CliResult unresolved = Invoke({site, "--ensemble", "cli-fixture", "--track", "alpha", "--feedback-list", "--unresolved"});
	if(!resolved_seeded || unresolved.exit_code != 0) ShowFailure("unresolved feedback-list", unresolved);
	Check(resolved_seeded && unresolved.exit_code == 0 && unresolved.out.Find("alpha note") < 0,
	      "unresolved filter omits feedback with a matching resolution event");

	if(!WriteRegistry("missing:tested:v1")) {
		Check(false, "invalid contract fixture is seeded");
		return;
	}
	Check(true, "invalid contract fixture is seeded");
	CliResult invalid = Invoke({site, "--ensemble", "cli-fixture", "--check"});
	Check(invalid.exit_code == 1, "unsatisfied cross-track contract --check exits 1");
	CliResult invalid_json = Invoke({site, "--ensemble", "cli-fixture", "--check", "--json"});
	bool invalid_json_ok = false;
	try {
		Value parsed = ParseJSON(invalid_json.out);
	invalid_json_ok = parsed.Is<ValueMap>() && !(bool)ValueMap(parsed).Get("ok") && invalid_json.exit_code == 1;
	}
	catch(...) {}
	Check(invalid_json_ok, "failing --check --json remains parseable and exits 1");
}

END_UPP_NAMESPACE

CONSOLE_APP_MAIN
{
	using namespace Upp;
	s_root = AppendFileName(GetTempPath(), "multiatlas-cli-test-" + AsString((int)msecs()) + "-" + AsString((int)Random(100000)));
	RealizeDirectory(s_root);
	CliTests();
	Cout() << "MultiAtlasVersionTest checks: " << (s_checks - s_failures) << "/" << s_checks << " passed\n";
	if(s_failures) Cout() << "fixture retained for inspection: " << s_root << '\n';
	else DeleteFolderDeep(s_root);
	SetExitCode(s_failures ? 1 : 0);
}
