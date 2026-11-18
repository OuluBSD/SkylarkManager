#include "MultiAtlasRegistry.h"

#ifndef _WIN32
#include <unistd.h>
#include <time.h>
#endif

NAMESPACE_UPP

static int s_registry_checks, s_registry_failures;

static void RegistryCheck(bool ok, const String& message)
{
	s_registry_checks++;
	if(!ok) { s_registry_failures++; Cout() << "FAIL: " << message << '\n'; }
}

static void WriteRegistry(const String& site, const Value& value)
{
	String dir = AppendFileName(AppendFileName(site, "engine"), "multiatlas");
	RealizeDirectory(dir);
	if(!SaveFile(AppendFileName(dir, "projects.json"), AsJSON(value)))
		Cout() << "fixture write failed: " << dir << '\n';
}

static Value TrackValue(const String& repo, const String& id = "vision",
	                    const String& store = "multiatlas/brownbear-vision/store")
{
	ValueMap track;
	track.Add("track_id", id); track.Add("family", "brownbear-" + id);
	track.Add("repo", repo); track.Add("branch", "track/" + id); track.Add("store", store);
	return track;
}

static Value EnsembleValue(const String& repo, const String& name = "brownbear-suite",
	                       const String& track = "vision",
	                       const String& store = "multiatlas/brownbear-vision/store")
{
	ValueMap meta;
	meta.Add("family", "brownbear-meta"); meta.Add("repo", repo);
	meta.Add("store", "multiatlas/brownbear-meta/store");
	ValueArray tracks; tracks.Add(TrackValue(repo, track, store));
	ValueMap ensemble;
	ensemble.Add("name", name); ensemble.Add("title", "BrownBear");
	ensemble.Add("meta", meta); ensemble.Add("tracks", tracks);
	return ensemble;
}

static Value RootValue(const ValueArray& ensembles)
{
	ValueMap root; root.Add("ensembles", ensembles); return root;
}

static bool MakeDirectory(const String& path)
{
	return RealizeDirectory(path) && DirectoryExists(path);
}

void RunMultiAtlasReplayTests()
{
	s_registry_checks = s_registry_failures = 0;
	String root = AppendFileName(GetTempPath(), "multiatlas-registry-test-" + AsString((int)time(NULL)));
	DeleteFolderDeep(root);
	String site = AppendFileName(root, "site"), repo = AppendFileName(root, "repo");
	String outside = AppendFileName(root, "outside");
	MakeDirectory(site);
	MakeDirectory(AppendFileName(repo, "multiatlas/brownbear-meta/store"));
	MakeDirectory(AppendFileName(repo, "multiatlas/brownbear-vision/store"));
	MakeDirectory(AppendFileName(repo, "atlas/legacy/store"));
	MakeDirectory(outside);

	Vector<MultiAtlasEnsembleDef> ensembles;
	String error;
	RegistryCheck(MultiAtlasLoadRegistry(site, ensembles, error) && ensembles.IsEmpty() && error.IsEmpty(),
	              "missing registry is harmless on a new install");
	MultiAtlasEnsembleDef found;
	RegistryCheck(!MultiAtlasFindEnsemble(site, "absent", found, error) && error.Find("not found") >= 0,
	              "named lookup reports a missing ensemble");

	ValueArray list; list.Add(EnsembleValue(repo)); WriteRegistry(site, RootValue(list));
	RegistryCheck(MultiAtlasLoadRegistry(site, ensembles, error) && ensembles.GetCount() == 1 &&
	              ensembles[0].name == "brownbear-suite" && ensembles[0].title == "BrownBear" &&
	              ensembles[0].meta_family == "brownbear-meta" && ensembles[0].tracks.GetCount() == 1 &&
	              ensembles[0].tracks[0].track_id == "vision" &&
	              ensembles[0].tracks[0].store == "multiatlas/brownbear-vision/store",
	              "registry parses meta and complete track configuration");
	RegistryCheck(MultiAtlasFindEnsemble(site, "brownbear-suite", found, error) &&
	              found.tracks.GetCount() == 1 && found.meta_repo == repo,
	              "named lookup returns matching ensemble");

	list.Clear(); list.Add(EnsembleValue(repo)); list.Add(EnsembleValue(repo));
	WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("duplicate ensemble") >= 0,
	              "duplicate ensemble names are rejected");
	ValueMap duplicate_ensemble = EnsembleValue(repo);
	ValueArray duplicate_tracks; duplicate_tracks.Add(TrackValue(repo)); duplicate_tracks.Add(TrackValue(repo));
	duplicate_ensemble.Set("tracks", duplicate_tracks);
	list.Clear(); list.Add(duplicate_ensemble); WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("duplicate track_id") >= 0,
	              "duplicate track IDs are rejected");

	list.Clear(); list.Add(EnsembleValue(repo, "brownbear-suite", "tested")); WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("invalid track_id") >= 0,
	              "reserved atom-family track IDs are rejected");
	list.Clear(); list.Add(EnsembleValue(repo, "brownbear-suite", "vision", "../outside/store")); WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("safe relative path") >= 0,
	              "parent traversal in store paths is rejected");
	list.Clear(); list.Add(EnsembleValue(repo, "brownbear-suite", "vision", "atlas/legacy/store")); WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("multiatlas/") >= 0,
	              "legacy Atlas namespace is rejected");
	list.Clear(); list.Add(EnsembleValue(repo, "brownbear-suite", "vision", "multiatlas/missing/store")); WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("does not exist") >= 0,
	              "missing store directories are reported");
	list.Clear(); list.Add(EnsembleValue(AppendFileName(root, "missing-repo"))); WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("repo directory does not exist") >= 0,
	              "missing repository directories are reported");

#ifndef _WIN32
	String track_dir = AppendFileName(repo, "multiatlas/brownbear-vision");
	String escaped_store = AppendFileName(track_dir, "escape");
	if(symlink(~outside, ~escaped_store) == 0) {
		list.Clear(); list.Add(EnsembleValue(repo, "brownbear-suite", "vision", "multiatlas/brownbear-vision/escape"));
		WriteRegistry(site, RootValue(list));
		RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("symlink") >= 0,
		              "symlinked store escapes are rejected");
	}
	String legacy_link = AppendFileName(track_dir, "legacy-link");
	if(symlink(~AppendFileName(repo, "atlas/legacy"), ~legacy_link) == 0) {
		list.Clear(); list.Add(EnsembleValue(repo, "brownbear-suite", "vision", "multiatlas/brownbear-vision/legacy-link/store"));
		WriteRegistry(site, RootValue(list));
		RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("namespace") >= 0,
		              "symlink to legacy Atlas store namespace is rejected");
	}
	String shared_link = AppendFileName(track_dir, "shared-store");
	if(symlink(~AppendFileName(repo, "multiatlas/brownbear-meta/store"), ~shared_link) == 0) {
		ValueMap aliased = EnsembleValue(repo);
		ValueArray aliased_tracks; aliased_tracks.Add(TrackValue(repo));
		ValueMap second = TrackValue(repo, "agent", "multiatlas/brownbear-vision/shared-store");
		aliased_tracks.Add(second);
		aliased.Set("tracks", aliased_tracks);
		list.Clear(); list.Add(aliased); WriteRegistry(site, RootValue(list));
		RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("same canonical store") >= 0,
		              "canonical store aliases cannot be shared across tracks");
	}
#endif

	ValueMap missing; missing.Add("name", "bad"); list.Clear(); list.Add(missing); WriteRegistry(site, RootValue(list));
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("title") >= 0,
	              "missing required fields produce a clear error");
	String path = AppendFileName(AppendFileName(AppendFileName(site, "engine"), "multiatlas"), "projects.json");
	FileOut invalid(path);
	invalid.Put("not json", 8);
	invalid.Close();
	RegistryCheck(!MultiAtlasLoadRegistry(site, ensembles, error) && error.Find("valid JSON") >= 0,
	              "malformed JSON is reported without throwing");

	Cout() << "MultiAtlasReplayTest checks: " << (s_registry_checks - s_registry_failures)
	       << "/" << s_registry_checks << " passed\n";
	DeleteFolderDeep(root);
	SetExitCode(s_registry_failures ? 1 : 0);
}

END_UPP_NAMESPACE
