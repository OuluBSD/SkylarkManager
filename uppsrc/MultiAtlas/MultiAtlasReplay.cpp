#include "MultiAtlasReplay.h"

NAMESPACE_UPP

static bool Has(const Vector<String>& values, const String& value)
{
	for(const String& item : values)
		if(item == value)
			return true;
	return false;
}

static void AddReplayUnique(Vector<String>& values, const String& value)
{
	if(!Has(values, value))
		values.Add(value);
}

static void Remove(Vector<String>& values, const String& value)
{
	for(int i = values.GetCount() - 1; i >= 0; i--)
		if(values[i] == value)
			values.Remove(i);
}

static bool Holds(const Vector<String>& state, const String& literal)
{
	bool negated = literal.StartsWith("!");
	String atom = negated ? literal.Mid(1) : literal;
	bool value = Has(state, atom);
	return negated ? !value : value;
}

static void Apply(Vector<String>& state, const String& literal)
{
	bool negated = literal.StartsWith("!");
	String atom = negated ? literal.Mid(1) : literal;
	if(negated)
		Remove(state, atom);
	else
		AddReplayUnique(state, atom);
}

static bool PublicAtom(const String& atom, bool explicitly_exported,
	                   const MultiAtlasRules& rules)
{
	MultiAtlasContractRef ref;
	String error;
	if(!MultiAtlasParseContract(atom, ref, error) || !ref.track_id.IsEmpty())
		return false;
	if(explicitly_exported)
		return true;
	if(ref.family_kind == "tested" || ref.family_kind == "artifact" ||
	   ref.family_kind == "done" || ref.family_kind == "realized")
		return true;
	for(const MultiAtlasAtomDef& def : rules.atoms)
		if(def.name == ref.atom_name && def.exported)
			return true;
	return false;
}

static bool ValidLocalAtom(const String& literal)
{
	MultiAtlasContractRef ref;
	String error;
	return MultiAtlasParseContract(literal, ref, error) && ref.track_id.IsEmpty();
}

static void Report(Vector<MultiAtlasRoadmapMessage>& messages, const char *severity,
	               const String& track, const String& vid, const String& kind,
	               const String& subject, const char *rule, const String& literal,
	               const String& text)
{
	MultiAtlasRoadmapMessage message;
	message.severity = severity;
	message.track_id = track;
	message.vid = vid;
	message.subject_kind = kind;
	message.subject_id = subject;
	message.rule = rule;
	message.literal = literal;
	message.text = text;
	messages.Add(pick(message));
}

static bool ResolveExternal(const MultiAtlasContractRef& ref,
	                        const MultiAtlasReplaySources& sources,
	                        bool& value)
{
	if(ref.vid_anchor.IsEmpty()) {
	int track = sources.final_exports.Find(ref.track_id);
		if(track < 0)
			return false;
		value = Has(sources.final_exports[track], ref.atom_name);
	}
	else {
	int track = sources.version_exports.Find(ref.track_id);
		if(track < 0)
			return false;
		const VectorMap<String, Vector<String>>& track_worlds = sources.version_exports[track];
		int version = track_worlds.Find(ref.vid_anchor);
		if(version < 0)
			return false;
		value = Has(track_worlds[version], ref.atom_name);
	}
	return true;
}

static bool HoldsContract(const String& literal, const Vector<String>& state,
	                      const MultiAtlasReplaySources& sources,
	                      bool& known)
{
	MultiAtlasContractRef ref;
	String error;
	if(!MultiAtlasParseContract(literal, ref, error)) {
		known = true;
		return false;
	}
	bool value;
	if(ref.track_id.IsEmpty()) {
		known = true;
		return Holds(state, literal);
	}
	known = ResolveExternal(ref, sources, value);
	if(!known)
		return false;
	return ref.negated ? !value : value;
}

static void CountMessage(MultiAtlasReplayContext& context,
	                     const MultiAtlasRoadmapMessage& message)
{
	if(message.severity == "error") context.total_errors++;
	else if(message.severity == "warning") context.total_warnings++;
}

static Vector<String> ReplayTrack(const MultiAtlasTrackReplayInput& track,
	                              const Vector<String>& imported_atoms,
	                              const MultiAtlasReplaySources& sources,
	                              VectorMap<String, Vector<String>>& version_atoms,
	                              Vector<MultiAtlasRoadmapMessage>& messages,
	                              const String& track_id,
	                              Vector<bool>& version_passed)
{
	version_atoms.Clear();
	version_passed.Clear();
	Vector<String> state;
	bool invalid_initial = false;
	for(const String& literal : track.rules.initial) {
		if(!ValidLocalAtom(literal)) {
			invalid_initial = true;
			Report(messages, "error", track_id, String(), "rules", "initial", "invalid-effect", literal,
			       "track " + track_id + " subject initial has invalid or cross-track atom " + literal);
			continue;
		}
		Apply(state, literal);
	}
	for(const String& literal : imported_atoms)
		Apply(state, literal);

	Vector<Vector<int>> events_by_version;
	events_by_version.SetCount(track.versions.GetCount());
	Index<String> version_index;
	for(int i = 0; i < track.versions.GetCount(); i++) version_index.Add(track.versions[i].vid);
	for(int i = 0; i < track.events.GetCount(); i++) {
		int vi = version_index.Find(track.events[i].vid);
		if(vi >= 0) events_by_version[vi].Add(i);
	}
	for(Vector<int>& bucket : events_by_version)
		CoStableSort(bucket, [&](int a, int b) {
			const MultiAtlasEvent& ea = track.events[a];
			const MultiAtlasEvent& eb = track.events[b];
			return ea.at == eb.at ? ea.id < eb.id : ea.at < eb.at;
		});

	bool timeline_blocked = invalid_initial;
	for(int version_index = 0; version_index < track.versions.GetCount(); version_index++) {
		const MultiAtlasVersion& version = track.versions[version_index];
		bool entry_ok = true;
		if(timeline_blocked) entry_ok = false;
		for(const String& literal : version.requires) {
			bool known = false;
			bool holds = HoldsContract(literal, state, sources, known);
			if(!holds) {
				entry_ok = false;
				MultiAtlasContractRef ref;
				String parse_error;
				bool pending_gate = known && MultiAtlasParseContract(literal, ref, parse_error) &&
				                    !ref.negated && ref.family_kind == "tested";
				String reason = known ? "not true" : "unknown upstream track or anchor";
				Report(messages, pending_gate ? "warning" : "error", track_id, version.vid, "version", version.vid,
				       "contract-gate", literal, track_id + " version " + version.vid +
				       " subject " + version.vid + " requirement " + literal + " " +
				       (pending_gate ? "pending gate" : reason));
			}
		}
		if(entry_ok) {
			for(int event_index : events_by_version[version_index]) {
				const MultiAtlasEvent& event = track.events[event_index];
				if(event.vid != version.vid) continue;
				Vector<String> needs;
				for(const String& req : event.requires) needs.Add(req);
				const MultiAtlasAction *action = NULL;
				for(const MultiAtlasAction& candidate : track.rules.actions)
					if(candidate.name == event.action) { action = &candidate; break; }
				if(!event.action.IsEmpty() && !action)
					Report(messages, "warning", track_id, version.vid, event.subject_kind,
					       event.subject_id, "unknown-action", event.action,
					       "track " + track_id + " version " + version.vid + " subject " +
					       event.subject_id + " names unknown action " + event.action);
				if(action)
					for(const String& req : action->requires) needs.Add(req);
				bool event_ok = true;
				for(const String& req : needs) {
					bool known = false;
					if(!HoldsContract(req, state, sources, known)) {
						event_ok = false;
						String reason = known ? "not true" : "unknown upstream track or anchor";
						Report(messages, "error", track_id, version.vid, event.subject_kind,
						       event.subject_id, "requires", req,
						       "track " + track_id + " version " + version.vid + " subject " +
						       event.subject_id + " requirement " + req + " " + reason);
					}
				}
				if(!event_ok) { timeline_blocked = true; continue; }
				Vector<String> effects;
				for(const String& effect : event.effects) effects.Add(effect);
				if(action) for(const String& effect : action->effects) effects.Add(effect);
				bool valid_effects = true;
				for(const String& effect : effects)
					if(!ValidLocalAtom(effect)) {
						valid_effects = false;
						Report(messages, "error", track_id, version.vid, event.subject_kind,
						       event.subject_id, "invalid-effect", effect,
						       "track " + track_id + " version " + version.vid + " subject " +
						       event.subject_id + " has invalid or cross-track effect " + effect);
					}
				if(!valid_effects) { timeline_blocked = true; continue; }
				for(const String& effect : effects) Apply(state, effect);
				}
		}
		if(!entry_ok) timeline_blocked = true;
		version_passed.Add(!timeline_blocked);
		version_atoms.GetAdd(version.vid) = clone(state);
	}

	Vector<String> exported;
	int last_passed = -1;
	for(int i = 0; i < version_passed.GetCount(); i++)
		if(version_passed[i]) last_passed = i;
	if(last_passed >= 0) {
		int last_snapshot = version_atoms.Find(track.versions[last_passed].vid);
		const Vector<String>& final_state = version_atoms[last_snapshot];
		for(const String& atom : final_state)
			if(PublicAtom(atom, false, track.rules)) exported.Add(atom);
	}
	return exported;
}

Vector<String> MultiAtlasSingleTrackReplay(
	const MultiAtlasTrackReplayInput& track, const Vector<String>& imported_atoms,
	const MultiAtlasReplaySources& sources,
	VectorMap<String, Vector<String>>& version_atoms,
	Vector<MultiAtlasRoadmapMessage>& messages)
{
	Vector<bool> version_passed;
	return ReplayTrack(track, imported_atoms, sources, version_atoms, messages, track.track_id, version_passed);
}

bool MultiAtlasReplayEnsemble(const MultiAtlasReplayInput& input, MultiAtlasReplayContext& context)
{
	context.exported_atoms_by_track.Clear();
	context.version_worlds_by_track.Clear();
	context.messages.Clear();
	context.total_errors = context.total_warnings = 0;
	context.fatal_error.Clear();
	MultiAtlasReplaySources sources;
	MultiAtlasTrackGraph graph;
	if(!MultiAtlasBuildDependencyGraph(input, graph, context.fatal_error)) return false;
	Vector<String> order;
	if(!MultiAtlasTopologicalSort(graph, order, context.fatal_error)) return false;
	for(const String& track_id : order) {
		const MultiAtlasTrackReplayInput *track = track_id == "meta" ? &input.meta : NULL;
		if(!track)
			for(const MultiAtlasTrackReplayInput& candidate : input.tracks)
				if(candidate.track_id == track_id) { track = &candidate; break; }
		if(!track) { context.fatal_error = "topological order references unknown track " + track_id; return false; }
		String effective_id = track_id;
		Vector<String> imported;
		for(const MultiAtlasTrackDependency& edge : graph.edges) {
			if(edge.to_track != track_id) continue;
			int source_index = sources.final_exports.Find(edge.from_track);
			if(source_index < 0) continue;
			for(const String& atom : sources.final_exports[source_index])
				AddReplayUnique(imported, edge.from_track + ":" + atom);
		}
		VectorMap<String, Vector<String>> track_worlds;
		Vector<bool> version_passed;
		Vector<String> exported = ReplayTrack(*track, imported, sources,
	                                     track_worlds, context.messages, effective_id, version_passed);
		context.exported_atoms_by_track.GetAdd(effective_id) = clone(exported);
		VectorMap<String, Vector<String>> exported_versions;
		for(int vi = 0; vi < track->versions.GetCount(); vi++) {
			const MultiAtlasVersion& version = track->versions[vi];
			Vector<String> filtered;
			int snapshot = track_worlds.Find(version.vid);
			if(snapshot >= 0 && version_passed[vi])
				for(const String& atom : track_worlds[snapshot])
					if(PublicAtom(atom, false, track->rules)) filtered.Add(atom);
			if(version_passed[vi]) {
				exported_versions.GetAdd(version.vid) = clone(filtered);
				if(version.code >= 0)
					exported_versions.GetAdd(Format("%d.%d.%d", version.code / 1000000,
					                                   (version.code / 1000) % 1000, version.code % 1000)) = pick(filtered);
			}
		}
		context.version_worlds_by_track.GetAdd(track_id) = pick(track_worlds);
		bool has_passed_version = false;
		for(bool passed : version_passed) has_passed_version |= passed;
		if(has_passed_version)
			sources.final_exports.GetAdd(track_id) = clone(exported);
		sources.version_exports.GetAdd(track_id) = pick(exported_versions);
	}
	for(const MultiAtlasRoadmapMessage& message : context.messages) CountMessage(context, message);
	return context.total_errors == 0;
}

END_UPP_NAMESPACE
