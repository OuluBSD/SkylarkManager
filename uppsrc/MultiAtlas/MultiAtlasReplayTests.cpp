#include "MultiAtlasReplay.h"

NAMESPACE_UPP

static int s_replay_checks, s_replay_failures;

static bool ContainsReplayAtom(const Vector<String>& atoms, const String& atom)
{
	for(const String& value : atoms) if(value == atom) return true;
	return false;
}

static void ReplayCheck(bool ok, const String& message)
{
	s_replay_checks++;
	if(!ok) { s_replay_failures++; Cout() << "FAIL: " << message << '\n'; }
}

static void AddReplayVersion(MultiAtlasTrackReplayInput& track, const String& vid,
	                         const Vector<String>& requires)
{
	MultiAtlasVersion version;
	version.vid = vid;
	for(const String& literal : requires) version.requires.Add(literal);
	track.versions.Add(pick(version));
}

static MultiAtlasEvent ReplayEvent(const String& track, const String& vid, const String& id,
	                             const String& action, const Vector<String>& effects)
{
	MultiAtlasEvent event;
	event.track_id = track; event.vid = vid; event.id = id; event.action = action;
	event.subject_kind = "task"; event.subject_id = id;
	for(const String& literal : effects) event.effects.Add(literal);
	return event;
}

void RunMultiAtlasReplayEngineTests()
{
	s_replay_checks = s_replay_failures = 0;
	MultiAtlasReplayInput input;
	input.tracks.SetCount(3);
	input.tracks[0].track_id = "track1";
	input.tracks[1].track_id = "track2";
	input.tracks[2].track_id = "independent";
	Vector<String> none;
	AddReplayVersion(input.tracks[0], "0.1.999", none);
	input.tracks[0].events.Add(ReplayEvent("track1", "0.1.999", "gate", "", Vector<String>()));
	input.tracks[0].events.Top().effects.Add("tested:0.1.999");
	input.tracks[0].events.Top().effects.Add("realized:CorePackage");

	Vector<String> track2_requires; track2_requires.Add("track1:tested:0.1.999");
	AddReplayVersion(input.tracks[1], "v2", track2_requires);
	MultiAtlasAction consume; consume.name = "consume";
	consume.requires.Add("track1:tested:0.1.999"); consume.effects.Add("artifact:consumed");
	input.tracks[1].rules.actions.Add(pick(consume));
	Vector<String> no_requirements;
	AddReplayVersion(input.tracks[2], "v1", no_requirements);
	input.tracks[2].events.Add(ReplayEvent("independent", "v1", "local", "", no_requirements));

	Vector<String> meta_requires;
	meta_requires.Add("track1:tested:0.1.999"); meta_requires.Add("track2:tested:v2");
	AddReplayVersion(input.meta, "1.0.0", meta_requires);
	MultiAtlasEvent meta_gate = ReplayEvent("meta", "1.0.0", "release", "", no_requirements);
	meta_gate.effects.Add("tested:1.0.0"); input.meta.events.Add(pick(meta_gate));
	MultiAtlasEvent track2_gate = ReplayEvent("track2", "v2", "gate", "", no_requirements);
	track2_gate.effects.Add("tested:v2"); input.tracks[1].events.Add(pick(track2_gate));

	MultiAtlasReplayContext context;
	bool passed = MultiAtlasReplayEnsemble(input, context);
	ReplayCheck(passed && context.total_errors == 0 &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("track2"), "tested:v2") &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("meta"), "tested:1.0.0") &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("track1"), "realized:CorePackage"),
	           "three child tracks replay topologically and meta release sees both exported gates");
	ReplayCheck(ContainsReplayAtom(context.version_worlds_by_track.Get("track1").Get("0.1.999"), "tested:0.1.999") &&
	           !ContainsReplayAtom(context.version_worlds_by_track.Get("track1").Get("0.1.999"), "gate"),
	           "version snapshots contain effects and do not invent event subjects as atoms");

	// Deleting the producer gate event blocks Track 2 at entry and the meta release.
	input.tracks[0].events.Clear();
	input.tracks[1].versions[0].code = 2000;
	input.meta.versions[0].requires.Add("track2@v2:tested:v2");
	input.meta.versions[0].requires.Add("track2@0.2.0:tested:v2");
	passed = MultiAtlasReplayEnsemble(input, context);
	bool exact_missing = false;
	Vector<String> meta_pending;
	for(const MultiAtlasRoadmapMessage& message : context.messages)
		if(message.track_id == "track2" && message.vid == "v2" && message.subject_id == "v2" &&
		   message.literal == "track1:tested:0.1.999" &&
		   message.text.Find("track1:tested:0.1.999 pending gate") >= 0 && message.severity == "warning")
			exact_missing = true;
		else if(message.track_id == "meta" &&
		        message.text.Find("pending gate") >= 0 && message.severity == "warning")
			meta_pending.Add(message.literal);
	ReplayCheck(passed && context.total_errors == 0 && exact_missing &&
	           ContainsReplayAtom(meta_pending, "track2:tested:v2") &&
	           context.exported_atoms_by_track.Get("track2").IsEmpty() &&
	           context.exported_atoms_by_track.Get("meta").IsEmpty(),
	           "registered track blocked at its first version exports no atoms and its gate is pending");
	ReplayCheck(passed && context.total_warnings == 5 &&
	           ContainsReplayAtom(meta_pending, "track2@v2:tested:v2") &&
	           ContainsReplayAtom(meta_pending, "track2@0.2.0:tested:v2"),
	           "registered blocked version resolves by both declared ID and canonical label");

	// A known, registered gate that has not yet been proven blocks the timeline
	// without making an otherwise honest draft plan fail --check.
	MultiAtlasReplayInput pending_gate;
	pending_gate.tracks.SetCount(2);
	pending_gate.tracks[0].track_id = "vision";
	pending_gate.tracks[1].track_id = "agent";
	AddReplayVersion(pending_gate.tracks[0], "0.4.999", no_requirements);
	AddReplayVersion(pending_gate.tracks[1], "0.5.0", Vector<String>());
	pending_gate.tracks[1].versions.Top().requires.Add("vision:tested:0.4.999");
	AddReplayVersion(pending_gate.meta, "1.0.0", no_requirements);
	pending_gate.meta.versions.Top().requires.Add("vision:tested:0.4.999");
	passed = MultiAtlasReplayEnsemble(pending_gate, context);
	ReplayCheck(passed && context.total_errors == 0 && context.total_warnings == 2 &&
	           !ContainsReplayAtom(context.exported_atoms_by_track.Get("agent"), "tested:0.5.0") &&
	           !ContainsReplayAtom(context.exported_atoms_by_track.Get("meta"), "tested:1.0.0"),
	           "pending registered tested gates warn and block dependent track and meta timelines without check errors");

	MultiAtlasReplayInput prior_exports;
	prior_exports.tracks.SetCount(3);
	prior_exports.tracks[0].track_id = "vision";
	prior_exports.tracks[1].track_id = "ai";
	prior_exports.tracks[2].track_id = "agent";
	AddReplayVersion(prior_exports.tracks[0], "0.4.999", no_requirements);
	AddReplayVersion(prior_exports.tracks[1], "0.1.0", no_requirements);
	Vector<String> ai_gate_req; ai_gate_req.Add("vision:tested:0.4.999");
	AddReplayVersion(prior_exports.tracks[1], "0.2.0", ai_gate_req);
	MultiAtlasEvent ai_realize = ReplayEvent("ai", "0.1.0", "core", "", no_requirements);
	ai_realize.effects.Add("artifact:checkpoint"); ai_realize.effects.Add("realized:CardEvaluator");
	prior_exports.tracks[1].events.Add(pick(ai_realize));
	Vector<String> agent_req; agent_req.Add("ai:realized:CardEvaluator");
	AddReplayVersion(prior_exports.tracks[2], "0.1.0", agent_req);
	MultiAtlasEvent agent_gate = ReplayEvent("agent", "0.1.0", "gate", "", no_requirements);
	agent_gate.effects.Add("tested:0.1.0"); prior_exports.tracks[2].events.Add(pick(agent_gate));
	AddReplayVersion(prior_exports.meta, "1.0.0", no_requirements);
	prior_exports.meta.versions.Top().requires.Add("ai:tested:0.2.0");
	passed = MultiAtlasReplayEnsemble(prior_exports, context);
	bool meta_pending_gate = false;
	for(const MultiAtlasRoadmapMessage& message : context.messages)
		if(message.track_id == "meta" && message.literal == "ai:tested:0.2.0" &&
		   message.severity == "warning" && message.text.Find("pending gate") >= 0)
			meta_pending_gate = true;
	ReplayCheck(passed && context.total_errors == 0 && context.total_warnings == 2 &&
	           meta_pending_gate &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("ai"), "artifact:checkpoint") &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("ai"), "realized:CardEvaluator") &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("agent"), "tested:0.1.0") &&
	           !ContainsReplayAtom(context.exported_atoms_by_track.Get("ai"), "tested:0.2.0") &&
	           !ContainsReplayAtom(context.exported_atoms_by_track.Get("meta"), "tested:1.0.0"),
	           "latest passed snapshot retains AI artifact/realization imports while pending later gates block exports");
	pending_gate.tracks[0].events.Add(ReplayEvent("vision", "0.4.999", "gate", "", no_requirements));
	pending_gate.tracks[0].events.Top().effects.Add("tested:0.4.999");
	pending_gate.tracks[1].events.Add(ReplayEvent("agent", "0.5.0", "gate", "", no_requirements));
	pending_gate.tracks[1].events.Top().effects.Add("tested:0.5.0");
	pending_gate.meta.events.Add(ReplayEvent("meta", "1.0.0", "release", "", no_requirements));
	pending_gate.meta.events.Top().effects.Add("tested:1.0.0");
	passed = MultiAtlasReplayEnsemble(pending_gate, context);
	ReplayCheck(passed && context.total_errors == 0 && context.total_warnings == 0 &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("agent"), "tested:0.5.0") &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("meta"), "tested:1.0.0"),
	           "actual gate fact unblocks downstream track and meta milestones");

	MultiAtlasReplayInput bad_prerequisite;
	bad_prerequisite.tracks.SetCount(2);
	bad_prerequisite.tracks[0].track_id = "vision";
	bad_prerequisite.tracks[1].track_id = "agent";
	AddReplayVersion(bad_prerequisite.tracks[0], "v1", no_requirements);
	Vector<String> bad_req; bad_req.Add("vision:artifact:missing");
	AddReplayVersion(bad_prerequisite.tracks[1], "v1", bad_req);
	passed = MultiAtlasReplayEnsemble(bad_prerequisite, context);
	ReplayCheck(!passed && context.total_errors == 1 && context.messages[0].severity == "error",
	           "unsatisfied cross-track artifact prerequisite remains an error");
	bad_prerequisite.tracks[1].versions[0].requires.Clear();
	bad_prerequisite.tracks[1].versions[0].requires.Add("vision:tested:");
	passed = MultiAtlasReplayEnsemble(bad_prerequisite, context);
	ReplayCheck(!passed && !context.fatal_error.IsEmpty() && context.total_errors == 0,
	           "malformed tested contract remains an error");

	// Anchors use the named snapshot, even when a later version removes the atom.
	input.tracks[0].events.Clear();
	input.tracks[0].events.Add(ReplayEvent("track1", "0.1.999", "gate", "", no_requirements));
	input.tracks[0].events.Top().effects.Add("tested:0.1.999");
	AddReplayVersion(input.tracks[0], "0.2.0", no_requirements);
	MultiAtlasEvent retract = ReplayEvent("track1", "0.2.0", "retract", "", no_requirements);
	retract.effects.Add("!tested:0.1.999"); input.tracks[0].events.Add(pick(retract));
	for(MultiAtlasVersion& version : input.tracks[1].versions)
		if(version.vid == "v2") {
			version.requires.Clear();
			version.requires.Add("track1@0.1.999:tested:0.1.999");
		}
	input.tracks[1].rules.actions.Clear();
	input.meta.versions[0].requires.Clear();
	input.meta.versions[0].requires.Add("track1@0.1.999:tested:0.1.999");
	input.meta.versions[0].requires.Add("track2@v2:tested:v2");
	passed = MultiAtlasReplayEnsemble(input, context);
	ReplayCheck(passed && !ContainsReplayAtom(context.version_worlds_by_track.Get("track1").Get("0.2.0"), "tested:0.1.999") &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("track2"), "tested:v2"),
	           "version-anchored import reads historical snapshot rather than latest state");

	// Cycles are rejected before any replay mutations.
	MultiAtlasReplayInput cyclic;
	cyclic.tracks.SetCount(2);
	cyclic.tracks[0].track_id = "alpha"; cyclic.tracks[1].track_id = "beta";
	Vector<String> alpha_req; alpha_req.Add("beta:artifact:x");
	Vector<String> beta_req; beta_req.Add("alpha:artifact:y");
	AddReplayVersion(cyclic.tracks[0], "a1", alpha_req);
	AddReplayVersion(cyclic.tracks[1], "b1", beta_req);
	bool cycle_rejected = !MultiAtlasReplayEnsemble(cyclic, context);
	ReplayCheck(cycle_rejected && context.total_errors == 0 &&
	           context.fatal_error.Find("alpha -[alpha:artifact:y]-> beta") >= 0 && context.messages.IsEmpty(),
	           "cyclic dependencies fail before execution and do not create misleading track findings");

	// Missing anchors differ from a known false atom.
	MultiAtlasReplayInput anchored;
	anchored.tracks.SetCount(2); anchored.tracks[0].track_id = "producer";
	anchored.tracks[1].track_id = "consumer";
	AddReplayVersion(anchored.tracks[0], "v1", none);
	Vector<String> unknown_anchor; unknown_anchor.Add("producer@v0:tested:v0");
	AddReplayVersion(anchored.tracks[1], "v2", unknown_anchor);
	passed = MultiAtlasReplayEnsemble(anchored, context);
	ReplayCheck(!passed && context.total_errors == 1 && context.total_warnings == 0 &&
	           context.messages.GetCount() == 1 &&
	           context.messages[0].text.Find("unknown upstream track or anchor") >= 0,
	           "undeclared anchor is an error even for a tested gate");
	anchored.tracks[1].versions[0].requires[0] = "missing:tested:v1";
	passed = MultiAtlasReplayEnsemble(anchored, context);
	ReplayCheck(!passed && context.fatal_error.Find("unknown track 'missing'") >= 0 &&
	           context.messages.IsEmpty(),
	           "unregistered upstream track is rejected before replay even for a tested gate");

	MultiAtlasReplayInput private_atom;
	private_atom.tracks.SetCount(2);
	private_atom.tracks[0].track_id = "source"; private_atom.tracks[1].track_id = "sink";
	Vector<String> private_atom_initial; private_atom_initial.Add("built:internal");
	private_atom.tracks[0].rules.initial <<= private_atom_initial;
	AddReplayVersion(private_atom.tracks[0], "p1", none);
	Vector<String> private_requirement; private_requirement.Add("source:built:internal");
	AddReplayVersion(private_atom.tracks[1], "c1", private_requirement);
	passed = MultiAtlasReplayEnsemble(private_atom, context);
	ReplayCheck(!passed && context.exported_atoms_by_track.Get("source").IsEmpty() &&
	           context.messages.GetCount() == 1 && context.messages[0].literal == "source:built:internal",
	           "private local atoms remain in snapshots but never cross track boundaries");

	MultiAtlasReplayInput bad_effect;
	bad_effect.tracks.SetCount(1); bad_effect.tracks[0].track_id = "writer";
	AddReplayVersion(bad_effect.tracks[0], "v1", none);
	MultiAtlasEvent forged = ReplayEvent("writer", "v1", "forged", "", none);
	forged.effects.Add("other:artifact:forged"); bad_effect.tracks[0].events.Add(pick(forged));
	passed = MultiAtlasReplayEnsemble(bad_effect, context);
	ReplayCheck(!passed && context.exported_atoms_by_track.Get("writer").IsEmpty() &&
	           context.messages.GetCount() == 1 && context.messages[0].rule == "invalid-effect",
	           "cross-track effects are rejected and cannot forge another track's state");
	bad_effect.tracks[0].events.Clear();
	MultiAtlasEvent malformed = ReplayEvent("writer", "v1", "malformed", "", none);
	malformed.effects.Add("artifact:"); bad_effect.tracks[0].events.Add(pick(malformed));
	passed = MultiAtlasReplayEnsemble(bad_effect, context);
	ReplayCheck(!passed && context.messages.GetCount() == 1 && context.messages[0].rule == "invalid-effect",
	           "malformed local effect literals are rejected with their exact subject");

	MultiAtlasReplayInput negated_block;
	negated_block.tracks.SetCount(2);
	negated_block.tracks[0].track_id = "blocked";
	negated_block.tracks[1].track_id = "guard";
	AddReplayVersion(negated_block.tracks[0], "b1", none);
	MultiAtlasEvent fails = ReplayEvent("blocked", "b1", "fail", "", none);
	fails.requires.Add("artifact:never"); fails.effects.Add("tested:b1");
	negated_block.tracks[0].events.Add(pick(fails));
	Vector<String> negative; negative.Add("!blocked:artifact:never");
	negative.Add("!blocked@b1:artifact:never");
	AddReplayVersion(negated_block.tracks[1], "g1", negative);
	negated_block.tracks[1].events.Add(ReplayEvent("guard", "g1", "gate", "", none));
	negated_block.tracks[1].events.Top().effects.Add("tested:g1");
	passed = MultiAtlasReplayEnsemble(negated_block, context);
	int failed_negations = 0;
	for(const MultiAtlasRoadmapMessage& message : context.messages)
		if(message.track_id == "guard" && message.text.Find("not true") >= 0 && message.severity == "error")
			failed_negations++;
	ReplayCheck(!passed && failed_negations == 2 && context.exported_atoms_by_track.Get("blocked").IsEmpty() &&
	           context.exported_atoms_by_track.Get("guard").IsEmpty(),
	           "negation cannot turn a blocked upstream timeline into a passing contract");
	negated_block.tracks[0].events.Clear();
	passed = MultiAtlasReplayEnsemble(negated_block, context);
	ReplayCheck(passed && context.messages.IsEmpty() &&
	           ContainsReplayAtom(context.exported_atoms_by_track.Get("guard"), "tested:g1"),
	           "negation can still use an empty export from a passed snapshot");

	Cout() << "MultiAtlasReplayEngineTest checks: " << (s_replay_checks - s_replay_failures)
	       << "/" << s_replay_checks << " passed\n";
	if(s_replay_failures) SetExitCode(1);
}

END_UPP_NAMESPACE
