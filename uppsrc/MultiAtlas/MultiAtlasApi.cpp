#include "MultiAtlasApi.h"
#include "MultiAtlasRegistry.h"
#include "MultiAtlasStore.h"
#include "MultiAtlasReplay.h"
#include "MultiAtlasScheduler.h"
#include "MultiAtlasWarningLedger.h"
#include "MultiAtlasReplayLoader.h"
#include <WebStorage/WebStorage.h>
#include <WebEngine/Request.h>

NAMESPACE_UPP

namespace {

String SiteDataRoot(const EngineSite& site)
{
	// EngineSite::data_root is <site-data>/engine/multiatlas.
	return GetFileFolder(GetFileFolder(site.data_root));
}

ValueMap BodyMap(WebRequest& r, bool& valid)
{
	valid = true;
	String body = r.Body();
	if(body.IsEmpty()) return ValueMap();
	try {
		Value v = ParseJSON(body);
		if(v.Is<ValueMap>()) return ValueMap(v);
	}
	catch(...) {}
	valid = false;
	return ValueMap();
}

String BodyString(const ValueMap& body, const char *key)
{
	Value v = body.Get(key);
	return v.Is<String>() ? (String)v : String();
}

String Timestamp()
{
	Time now = GetSysTime();
	return Format("%04d-%02d-%02dT%02d:%02d:%02d",
	              now.year, now.month, now.day, now.hour, now.minute, now.second);
}

ValueArray StringArray(const Vector<String>& items)
{
	ValueArray out;
	for(const String& item : items) out.Add(item);
	return out;
}

void Fail(WebRequest& r, int status, const String& error)
{
	ValueMap out;
	out.Add("ok", false);
	out.Add("error", error);
	r.Json(out, status);
}

void Reply(WebRequest& r, const Value& data, int status = 200)
{
	ValueMap out;
	out.Add("ok", status >= 200 && status < 300);
	out.Add("data", data);
	r.Json(out, status);
}

bool IsAdmin(const WebRequest& r)
{
	return r.HasUser() && FindIndex(r.User().roles, String("admin")) >= 0;
}

bool IsEditor(const WebRequest& r, const MultiAtlasTrackDef& track)
{
	return IsAdmin(r) || (r.HasUser() && FindIndex(track.editors, r.User().username) >= 0);
}

bool ReadAllowed(WebRequest& r, const MultiAtlasEnsembleDef& ensemble)
{
	if(ensemble.public_read || r.HasUser()) return true;
	Fail(r, 404, "ensemble not found");
	return false;
}

bool LoadEnsemble(WebRequest& r, const String& name, MultiAtlasEnsembleDef& ensemble)
{
	String error;
	if(!MultiAtlasFindEnsemble(SiteDataRoot(r.Site()), name, ensemble, error)) {
		Fail(r, error.Find("not found") >= 0 ? 404 : 500,
		     error.Find("not found") >= 0 ? error : "MultiAtlas registry is unavailable");
		return false;
	}
	return ReadAllowed(r, ensemble);
}

const MultiAtlasTrackDef *FindTrack(const MultiAtlasEnsembleDef& ensemble, const String& id)
{
	for(const MultiAtlasTrackDef& track : ensemble.tracks)
		if(track.track_id == id) return &track;
	return NULL;
}

String StoreRoot(const MultiAtlasEnsembleDef& ensemble, const String& track)
{
	return MultiAtlasResolveTrackStore(ensemble, track);
}

void FailStore(WebRequest& r, const String& error)
{
	if(error.Find("not found") >= 0)
		Fail(r, 404, "MultiAtlas record not found");
	else
		Fail(r, 500, "MultiAtlas storage operation failed");
}

bool GitOutput(const String& repo, const Vector<String>& args, String& output)
{
	Vector<String> argv;
	argv.Add("-C"); argv.Add(repo);
	for(const String& arg : args) argv.Add(arg);
	LocalProcess process;
	if(!process.Start2("git", argv)) return false;
	process.CloseWrite();
	output.Clear();
	uint32 started = GetTickCount();
	for(;;) {
		String stdout_text, stderr_text;
		bool more = process.Read2(stdout_text, stderr_text);
		output << stdout_text;
		if(!more) break;
		if(GetTickCount() - started > 10000) {
			process.Kill();
			return false;
		}
		Sleep(2);
	}
	return process.GetExitCode() == 0;
}

Value GitFacts(const String& repo)
{
	ValueMap out;
	String head, branch, sync;
	Vector<String> args;
	args.Add("rev-parse"); args.Add("--verify"); args.Add("HEAD");
	bool available = GitOutput(repo, args, head);
	args.Clear(); args.Add("symbolic-ref"); args.Add("--short"); args.Add("-q"); args.Add("HEAD");
	GitOutput(repo, args, branch);
	args.Clear(); args.Add("log"); args.Add("-1"); args.Add("--format=%cI");
	GitOutput(repo, args, sync);
	out.Add("available", available);
	out.Add("head", available ? TrimBoth(head) : String());
	out.Add("branch", TrimBoth(branch));
	out.Add("sync_at", TrimBoth(sync));
	return out;
}

bool Replay(WebRequest& r, const MultiAtlasEnsembleDef& ensemble,
            MultiAtlasReplayInput& input, MultiAtlasReplayContext& context,
            ValueArray& acks)
{
	String error; Vector<MultiAtlasWarningAck> loaded_acks;
	if(!MultiAtlasLoadReplayInput(ensemble, input, loaded_acks, error)) {
		FailStore(r, error);
		return false;
	}
	for(const MultiAtlasWarningAck& ack : loaded_acks) {
		ValueMap row = StoreAsJsonValue(ack);
		if(!r.HasUser()) {
			int actor = row.Find("actor"), comment = row.Find("comment");
			if(comment >= 0) row.Remove(comment);
			if(actor >= 0) row.Remove(actor);
		}
		acks.Add(row);
	}
	MultiAtlasReplayEnsemble(input, context);
	return true;
}

Value MessageValue(const MultiAtlasRoadmapMessage& m)
{
	ValueMap row;
	row.Add("severity", m.severity); row.Add("track_id", m.track_id);
	row.Add("vid", m.vid); row.Add("subject_kind", m.subject_kind);
	row.Add("subject_id", m.subject_id); row.Add("rule", m.rule);
	row.Add("literal", m.literal); row.Add("text", m.text);
	row.Add("warning_id", MultiAtlasWarningId(m.track_id, m));
	return row;
}

Value ReplayReport(const MultiAtlasReplayInput& input, const MultiAtlasReplayContext& context,
                   const ValueArray& acks)
{
	ValueArray tracks, unfulfilled;
	for(const MultiAtlasTrackReplayInput& track : input.tracks) {
		ValueMap row; ValueArray errors, warnings, versions;
		row.Add("track_id", track.track_id);
		for(const MultiAtlasVersion& version : track.versions) {
			ValueMap v; v.Add("vid", version.vid); v.Add("code", version.code);
			v.Add("requires", StringArray(version.requires)); versions.Add(v);
		}
		row.Add("versions", versions);
		for(const MultiAtlasRoadmapMessage& m : context.messages) {
			if(m.track_id != track.track_id) continue;
			Value v = MessageValue(m);
			if(m.severity == "error") {
				errors.Add(v);
				MultiAtlasContractRef ref; String parse_error;
				if(MultiAtlasParseContract(m.literal, ref, parse_error) && !ref.track_id.IsEmpty())
					unfulfilled.Add(v);
			}
			else warnings.Add(v);
		}
		row.Add("errors", errors); row.Add("warnings", warnings);
		tracks.Add(row);
	}
	ValueMap meta; ValueArray meta_errors, meta_warnings;
	meta.Add("track_id", "meta");
	for(const MultiAtlasRoadmapMessage& m : context.messages) if(m.track_id == "meta") {
		if(m.severity == "error") {
			meta_errors.Add(MessageValue(m));
			MultiAtlasContractRef ref; String parse_error;
			if(MultiAtlasParseContract(m.literal, ref, parse_error) && !ref.track_id.IsEmpty())
				unfulfilled.Add(MessageValue(m));
		}
		else meta_warnings.Add(MessageValue(m));
	}
	meta.Add("errors", meta_errors); meta.Add("warnings", meta_warnings); tracks.Add(meta);
	ValueMap out;
	out.Add("ok", context.fatal_error.IsEmpty() && context.total_errors == 0);
	out.Add("fatal_error", context.fatal_error);
	out.Add("total_errors", context.total_errors + (context.fatal_error.IsEmpty() ? 0 : 1));
	out.Add("total_warnings", context.total_warnings);
	out.Add("tracks", tracks);
	out.Add("unfulfilled_cross_track_dependencies", unfulfilled);
	out.Add("acknowledgments", acks);
	return out;
}

Value VersionTree(const MultiAtlasTrackReplayInput& track)
{
	ValueMap row; ValueArray versions, actions;
	row.Add("track_id", track.track_id);
	for(const MultiAtlasVersion& version : track.versions) {
		ValueMap v; v.Add("vid", version.vid); v.Add("code", version.code);
		v.Add("requires", StringArray(version.requires)); versions.Add(v);
	}
	row.Add("versions", versions);
	for(const MultiAtlasAction& action : track.rules.actions) {
		ValueMap a; a.Add("name", action.name); a.Add("requires", StringArray(action.requires));
		actions.Add(a);
	}
	row.Add("actions", actions);
	return row;
}

void ListEnsembles(WebRequest& r)
{
	Vector<MultiAtlasEnsembleDef> ensembles; String error;
	if(!MultiAtlasLoadRegistry(SiteDataRoot(r.Site()), ensembles, error)) {
		Fail(r, 500, "MultiAtlas registry is unavailable"); return;
	}
	ValueArray rows;
	for(const MultiAtlasEnsembleDef& ensemble : ensembles) {
		if(!ensemble.public_read && !r.HasUser()) continue;
		ValueMap row; ValueArray tracks;
		row.Add("name", ensemble.name); row.Add("title", ensemble.title);
		row.Add("public_read", ensemble.public_read);
		for(const MultiAtlasTrackDef& track : ensemble.tracks) {
			ValueMap t; t.Add("track_id", track.track_id); t.Add("family", track.family);
			t.Add("branch", track.branch); tracks.Add(t);
		}
		row.Add("tracks", tracks); rows.Add(row);
	}
	ValueMap data; data.Add("ensembles", rows); Reply(r, data);
}

void Status(WebRequest& r)
{
	MultiAtlasEnsembleDef ensemble;
	if(!LoadEnsemble(r, r.PathArg(0), ensemble)) return;
	MultiAtlasReplayInput replay_input; MultiAtlasReplayContext replay_context; ValueArray acks;
	if(!Replay(r, ensemble, replay_input, replay_context, acks)) return;
	ValueMap data; ValueArray tracks;
	for(const MultiAtlasTrackDef& track : ensemble.tracks) {
		ValueMap row; row.Add("track_id", track.track_id); row.Add("family", track.family);
		row.Add("git", GitFacts(track.repo));
		ValueArray versions, gates;
		for(const MultiAtlasTrackReplayInput& replay_track : replay_input.tracks) {
			if(replay_track.track_id != track.track_id) continue;
			for(const MultiAtlasVersion& version : replay_track.versions) {
				ValueMap v; v.Add("vid", version.vid); v.Add("code", version.code);
				v.Add("requires", StringArray(version.requires)); versions.Add(v);
			}
		}
		int exported = replay_context.exported_atoms_by_track.Find(track.track_id);
		if(exported >= 0)
			for(const String& atom : replay_context.exported_atoms_by_track[exported])
				if(atom.StartsWith("tested:")) gates.Add(atom.Mid(7));
		row.Add("versions", versions); row.Add("passed_gates", gates);
		tracks.Add(row);
	}
	ValueMap meta; meta.Add("family", ensemble.meta_family); meta.Add("git", GitFacts(ensemble.meta_repo));
	data.Add("ensemble", ensemble.name); data.Add("tracks", tracks); data.Add("meta", meta);
	data.Add("replay_error", replay_context.fatal_error);
	Reply(r, data);
}

void Check(WebRequest& r)
{
	MultiAtlasEnsembleDef ensemble;
	if(!LoadEnsemble(r, r.PathArg(0), ensemble)) return;
	MultiAtlasReplayInput input; MultiAtlasReplayContext context; ValueArray acks;
	if(!Replay(r, ensemble, input, context, acks)) return;
	Reply(r, ReplayReport(input, context, acks));
}

void Tree(WebRequest& r)
{
	MultiAtlasEnsembleDef ensemble;
	if(!LoadEnsemble(r, r.PathArg(0), ensemble)) return;
	MultiAtlasReplayInput input; MultiAtlasReplayContext context; ValueArray acks;
	if(!Replay(r, ensemble, input, context, acks)) return;
	ValueArray tracks;
	for(const MultiAtlasTrackReplayInput& track : input.tracks) {
		tracks.Add(VersionTree(track));
	}
	tracks.Add(VersionTree(input.meta));
	MultiAtlasTrackGraph graph; String graph_error; Vector<String> order; String cycle;
	bool graph_ok = MultiAtlasBuildDependencyGraph(input, graph, graph_error) &&
	                MultiAtlasTopologicalSort(graph, order, cycle);
	ValueArray edges, order_values;
	for(const MultiAtlasTrackDependency& edge : graph.edges) {
		ValueMap e; e.Add("from", edge.from_track); e.Add("to", edge.to_track);
		e.Add("contract", edge.contract); edges.Add(e);
	}
	for(const String& id : order) order_values.Add(id);
	ValueMap data; data.Add("tracks", tracks); data.Add("edges", edges);
	data.Add("topological_order", order_values);
	if(!graph_ok) data.Add("error", graph_error.IsEmpty() ? cycle : graph_error);
	Reply(r, data, graph_ok ? 200 : 409);
}

void Map(WebRequest& r)
{
	MultiAtlasEnsembleDef ensemble;
	if(!LoadEnsemble(r, r.PathArg(0), ensemble)) return;
	const MultiAtlasTrackDef *track = FindTrack(ensemble, r.PathArg(1));
	if(!track) { Fail(r, 404, "track not found"); return; }
	String root = StoreRoot(ensemble, track->track_id), revision, error;
	MultiAtlasMap map;
	if(!MultiAtlasLoadTrackMap(root, track->family, map, revision, error)) {
		FailStore(r, error); return;
	}
	ValueMap data = StoreAsJsonValue(map); data.Add("revision", revision);
	Reply(r, data);
}

bool EditorWrite(WebRequest& r, const MultiAtlasTrackDef& track)
{
	if(!r.HasUser()) { Fail(r, 401, "authentication required"); return false; }
	if(!IsEditor(r, track)) { Fail(r, 403, "track editor or admin role required"); return false; }
	return true;
}

bool ExpectedRevision(WebRequest& r, const String& current)
{
	bool valid; ValueMap body = BodyMap(r, valid);
	if(!valid) { Fail(r, 400, "request body must be a JSON object"); return false; }
	String expected = BodyString(body, "expected_revision");
	if(expected.IsEmpty()) expected = r.Param("expected_revision");
	if(expected.IsEmpty()) { Fail(r, 400, "expected_revision is required"); return false; }
	if(expected != current) { Fail(r, 409, "record revision conflict; reload and retry"); return false; }
	return true;
}

void AppendEvent(WebRequest& r, bool meta)
{
	MultiAtlasEnsembleDef ensemble;
	if(!LoadEnsemble(r, r.PathArg(0), ensemble)) return;
	String track_id = meta ? String("meta") : r.PathArg(1);
	const MultiAtlasTrackDef *track = meta ? NULL : FindTrack(ensemble, track_id);
	if(!meta && !track) { Fail(r, 404, "track not found"); return; }
	if(meta) {
		if(!r.HasUser()) { Fail(r, 401, "authentication required"); return; }
		if(!IsAdmin(r)) { Fail(r, 403, "meta-map writes require admin role"); return; }
	}
	else if(!EditorWrite(r, *track)) return;
	bool valid; ValueMap body = BodyMap(r, valid);
	if(!valid) { Fail(r, 400, "request body must be a JSON object"); return; }
	String key = meta ? ensemble.meta_family : track->family;
	String root = StoreRoot(ensemble, track_id), revision, error;
	if(root.IsEmpty()) { Fail(r, 404, "store unavailable"); return; }
	if(meta) {
		MultiAtlasMetaFamily family;
		if(!MultiAtlasLoadMetaFamily(root, key, family, revision, error)) {
			FailStore(r, error); return;
		}
		bool known_vid = false;
		for(const MultiAtlasVersion& version : family.versions) known_vid |= version.vid == BodyString(body, "vid");
		if(!known_vid) { Fail(r, 400, "vid is not present in the meta family"); return; }
	}
	else {
		MultiAtlasTrack family;
		if(!MultiAtlasLoadTrackFamily(root, key, family, revision, error)) {
			FailStore(r, error); return;
		}
		bool known_vid = false;
		for(const MultiAtlasVersion& version : family.versions) known_vid |= version.vid == BodyString(body, "vid");
		if(!known_vid) { Fail(r, 400, "vid is not present in the track family"); return; }
	}
	if(!ExpectedRevision(r, revision)) return;
	MultiAtlasEvent event;
	event.id = BodyString(body, "id");
	if(event.id.IsEmpty()) event.id = "ev-" + Format64Hex((int64)(Random64() & 0x7fffffffffffffffULL));
	event.at = Timestamp(); event.actor = r.User().username; event.track_id = track_id;
	event.vid = BodyString(body, "vid"); event.subject_kind = BodyString(body, "subject_kind");
	event.subject_id = BodyString(body, "subject_id"); event.type = BodyString(body, "type");
	event.action = BodyString(body, "action"); event.text = BodyString(body, "text");
	if(event.vid.IsEmpty() || event.type.IsEmpty()) { Fail(r, 400, "vid and type are required"); return; }
	Value req_value = body.Get("requires"), effects_value = body.Get("effects");
	if(!req_value.IsNull()) {
		if(!req_value.Is<ValueArray>()) { Fail(r, 400, "requires must be an array of strings"); return; }
		for(const Value& item : ValueArray(req_value)) {
			if(!item.Is<String>()) { Fail(r, 400, "requires must be an array of strings"); return; }
			event.requires.Add((String)item);
		}
	}
	if(!effects_value.IsNull()) {
		if(!effects_value.Is<ValueArray>()) { Fail(r, 400, "effects must be an array of strings"); return; }
		for(const Value& item : ValueArray(effects_value)) {
			if(!item.Is<String>()) { Fail(r, 400, "effects must be an array of strings"); return; }
			MultiAtlasContractRef effect; String parse_error;
			if(!MultiAtlasParseContract((String)item, effect, parse_error) || !effect.track_id.IsEmpty() || effect.negated) {
				Fail(r, 400, "effects must be valid local positive atoms"); return;
			}
			event.effects.Add((String)item);
		}
	}
	for(const String& requirement : event.requires) {
		MultiAtlasContractRef ref; String parse_error;
		if(!MultiAtlasParseContract(requirement, ref, parse_error)) {
			Fail(r, 400, "requires contains an invalid contract: " + parse_error); return;
		}
		if(!ref.track_id.IsEmpty() && ref.track_id != "meta" && !FindTrack(ensemble, ref.track_id)) {
			Fail(r, 400, "requires references an unregistered track: " + ref.track_id); return;
		}
	}
	if(!MultiAtlasPutTrackEvent(root, key, track_id, event, error)) {
		if(error.Find("revision conflict") >= 0 || error.Find("already exists") >= 0)
			Fail(r, 409, "event ID already exists or changed concurrently");
		else if(error.Find("invalid") >= 0 || error.Find("scope") >= 0)
			Fail(r, 400, error);
		else
			Fail(r, 500, "MultiAtlas storage operation failed");
		return;
	}
	ValueMap data; data.Add("event", StoreAsJsonValue(event)); data.Add("revision", revision);
	data.Add("revision_guard", "family revision is an advisory stale check; append uses atomic create-only event ID");
	Reply(r, data, 201);
}

void Ack(WebRequest& r)
{
	MultiAtlasEnsembleDef ensemble;
	if(!LoadEnsemble(r, r.PathArg(0), ensemble)) return;
	if(!r.HasUser()) { Fail(r, 401, "authentication required"); return; }
	bool valid; ValueMap body = BodyMap(r, valid);
	if(!valid) { Fail(r, 400, "request body must be a JSON object"); return; }
	String track_id = BodyString(body, "track_id");
	const MultiAtlasTrackDef *track = FindTrack(ensemble, track_id);
	if(track_id != "meta" && !track) { Fail(r, 404, "track not found"); return; }
	if(track_id == "meta") {
		if(!IsAdmin(r)) { Fail(r, 403, "meta-map acknowledgments require admin role"); return; }
	}
	else if(!EditorWrite(r, *track)) return;
	String warning_id = BodyString(body, "warning_id"), rule = BodyString(body, "rule");
	String vid = BodyString(body, "vid"), comment = BodyString(body, "comment");
	if(!MultiAtlasValidWarningId(warning_id) || rule.IsEmpty() || vid.IsEmpty() || comment.IsEmpty() || comment.GetCount() > 2000) {
		Fail(r, 400, "warning_id, rule, vid and a comment of at most 2000 characters are required"); return;
	}
	MultiAtlasReplayInput input; MultiAtlasReplayContext context; ValueArray acks;
	if(!Replay(r, ensemble, input, context, acks)) return;
	bool active = false;
	for(const MultiAtlasRoadmapMessage& message : context.messages)
		if(message.severity == "warning" && message.track_id == track_id && message.rule == rule && message.vid == vid &&
		   MultiAtlasWarningId(track_id, message) == warning_id) active = true;
	if(!active) { Fail(r, 404, "warning is no longer active in this scope"); return; }
	String root = StoreRoot(ensemble, track_id), new_rev, error;
	MultiAtlasWarningAck ack;
	ack.track_id = track_id; ack.warning_id = warning_id; ack.rule = rule;
	ack.vid = vid; ack.actor = r.User().username; ack.comment = comment; ack.at = Timestamp();
	String expected = BodyString(body, "expected_revision");
	if(expected.IsEmpty()) { Fail(r, 400, "expected_revision is required (use 'absent' for a new acknowledgment)"); return; }
	if(!MultiAtlasSaveTrackAck(root, track_id, ack, expected, new_rev, error)) {
		if(error.Find("conflict") >= 0 || error.Find("already exists") >= 0)
			Fail(r, 409, "warning acknowledgment already exists or changed concurrently");
		else if(error.Find("invalid") >= 0 || error.Find("missing") >= 0)
			Fail(r, 400, error);
		else
			Fail(r, 500, "MultiAtlas storage operation failed");
		return;
	}
	ValueMap data; data.Add("acknowledgment", StoreAsJsonValue(ack)); data.Add("revision", new_rev);
	Reply(r, data, 201);
}

} // namespace

void InstallMultiAtlasApi(EngineSite& site)
{
	site.Router().Add("GET", "api/multiatlas/ensembles", ListEnsembles);
	site.Router().Add("GET", "api/multiatlas/*/status", Status);
	site.Router().Add("GET", "api/multiatlas/*/check", Check);
	site.Router().Add("GET", "api/multiatlas/*/tree", Tree);
	site.Router().Add("GET", "api/multiatlas/*/track/*/map", Map);
	site.Router().Add("POST", "api/multiatlas/*/track/*/event", [](WebRequest& r) { AppendEvent(r, false); });
	site.Router().Add("POST", "api/multiatlas/*/meta/event", [](WebRequest& r) { AppendEvent(r, true); });
	site.Router().Add("POST", "api/multiatlas/*/warning/ack", Ack);
}

END_UPP_NAMESPACE
