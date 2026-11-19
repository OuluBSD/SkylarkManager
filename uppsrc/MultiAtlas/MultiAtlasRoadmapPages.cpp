#include "MultiAtlasRoadmapPages.h"
#include "MultiAtlasCanvas.h"
#include "MultiAtlasClient.h"
#include "MultiAtlasRegistry.h"
#include "MultiAtlasReplayLoader.h"
#include "MultiAtlasStore.h"
#include "MultiAtlasContract.h"
#include "MultiAtlasDashboard.h"
#include <WebEngine/Request.h>
#include <WebsiteTemplates/WebsiteTemplates.h>

NAMESPACE_UPP

namespace {

String MultiAtlasRoadmapSiteRoot(const EngineSite& site) { return GetFileFolder(GetFileFolder(site.data_root)); }
String MultiAtlasRoadmapEsc(const String& s)
{
	StringBuffer b;
	for(int i = 0; i < s.GetCount(); i++) switch(s[i]) {
	case '&': b << "&amp;"; break; case '<': b << "&lt;"; break; case '>': b << "&gt;"; break;
	case '"': b << "&quot;"; break; case '\'': b << "&#39;"; break; default: b.Cat(s[i]);
	}
	return String(b);
}

struct MultiAtlasCatalogProject : Moveable<MultiAtlasCatalogProject> {
	String name, title, level;
};

// The selector mirrors Atlas' project catalog, but reads only project names,
// titles, and visibility labels. MultiAtlas map/family/store content is always
// loaded from the independent MultiAtlas registry and stores below.
static bool LoadAtlasProjectCatalog(const String& site_dir,
                                    Vector<MultiAtlasCatalogProject>& projects, String& error)
{
	projects.Clear();
	error.Clear();
	String path = AppendFileName(AppendFileName(AppendFileName(site_dir, "engine"), "atlas"), "projects.json");
	if(!FileExists(path)) return true;
	FileIn in(path);
	if(!in.IsOpen()) {
		error = "cannot read Atlas project catalog";
		return false;
	}
	if(in.GetLeft() > 4 * 1024 * 1024) {
		error = "Atlas project catalog exceeds 4 MiB";
		return false;
	}
	String json;
	while(!in.IsEof()) {
		int count = min(8192, (int)in.GetLeft());
		if(count <= 0) break;
		json.Cat(in.Get(count));
	}
	Value root;
	try {
		root = ParseJSON(json);
	}
	catch(...) {
		error = "Atlas project catalog is not valid JSON";
		return false;
	}
	if(!root.Is<ValueMap>()) {
		error = "Atlas project catalog root must be an object";
		return false;
	}
	ValueMap top = root;
	Value list = top.Get("projects");
	if(!list.Is<ValueArray>()) return true;
	ValueArray entries = list;
	for(const Value& value : entries) {
		if(!value.Is<ValueMap>()) {
			error = "Atlas project catalog entry must be an object";
			projects.Clear();
			return false;
		}
		ValueMap row = value;
		MultiAtlasCatalogProject project;
		Value name = row.Get("name"), title = row.Get("title"), level = row.Get("level");
		if(!name.Is<String>() || TrimBoth((String)name).IsEmpty()) {
			error = "Atlas project catalog entry has no project name";
			projects.Clear();
			return false;
		}
		project.name = TrimBoth((String)name);
		project.title = title.Is<String>() ? TrimBoth((String)title) : String();
		if(project.title.IsEmpty()) project.title = project.name;
		project.level = level.Is<String>() ? ToLower(TrimBoth((String)level)) : "private";
		for(const MultiAtlasCatalogProject& prior : projects)
			if(prior.name == project.name) {
				error = "Atlas project catalog contains duplicate project names";
				projects.Clear();
				return false;
			}
		projects.Add(pick(project));
	}
	return true;
}

static const MultiAtlasEnsembleDef *FindCatalogEnsemble(const Vector<MultiAtlasEnsembleDef>& ensembles,
                                                        const String& name)
{
	for(const MultiAtlasEnsembleDef& ensemble : ensembles)
		if(ensemble.name == name) return &ensemble;
	return NULL;
}

static String MultiAtlasProjectRows(WebRequest& r, const Vector<MultiAtlasCatalogProject>& projects,
                                    const Vector<MultiAtlasEnsembleDef>& ensembles)
{
	StringBuffer rows;
	int count = 0;
	for(const MultiAtlasCatalogProject& project : projects) {
		// Match Atlas' public project index for anonymous viewers. MultiAtlas
		// visibility is checked independently on the matched ensemble below.
		if(project.level != "public") continue;
		const MultiAtlasEnsembleDef *ensemble = FindCatalogEnsemble(ensembles, project.name);
		rows << "<tr><td>" << MultiAtlasRoadmapEsc(project.title) << "</td><td>"
		     << MultiAtlasRoadmapEsc(project.name) << "</td><td>";
		if(!ensemble) rows << "Not configured for MultiAtlas";
		else if(!ensemble->public_read && !r.HasUser()) rows << "Sign-in required";
		else rows << "<a href=\"" << MultiAtlasRoadmapEsc(r.Site().Url("multiatlas/" + ensemble->name))
		          << "\">Open MultiAtlas map</a>";
		rows << "</td><td>" << (ensemble ? AsString(ensemble->tracks.GetCount()) : String("—")) << "</td></tr>";
		count++;
	}
	if(!count) return "<p>No public Atlas projects are listed.</p>";
	return "<table><thead><tr><th>Project</th><th>Project ID</th><th>MultiAtlas</th><th>Tracks</th></tr></thead><tbody>"
	     + String(rows) + "</tbody></table>";
}

static void ProjectList(WebRequest& r)
{
	String site_dir = MultiAtlasRoadmapSiteRoot(r.Site()), error;
	Vector<MultiAtlasEnsembleDef> ensembles;
	if(!MultiAtlasLoadRegistry(site_dir, ensembles, error)) {
		r.Status(500, "MultiAtlas project registry unavailable");
		return;
	}
	Vector<MultiAtlasCatalogProject> projects;
	String catalog_error;
	bool catalog_ok = LoadAtlasProjectCatalog(site_dir, projects, catalog_error);
	StringBuffer body;
	body << "<main><h1>MultiAtlas projects</h1><p>Select a project to open its multi-map roadmap."
	         " This selector uses Atlas' public project names; every map, track, replay, and event comes from the separate MultiAtlas registry and stores.</p>";
	if(!catalog_ok)
		body << "<p role=\"status\">Atlas project list unavailable; showing registered MultiAtlas ensembles.</p>";
	if(catalog_ok && !projects.IsEmpty())
		body << MultiAtlasProjectRows(r, projects, ensembles);
	else {
		StringBuffer rows;
		int count = 0;
		for(const MultiAtlasEnsembleDef& ensemble : ensembles) {
			if(!ensemble.public_read && !r.HasUser()) continue;
			rows << "<tr><td>" << MultiAtlasRoadmapEsc(ensemble.title) << "</td><td>"
			     << MultiAtlasRoadmapEsc(ensemble.name) << "</td><td><a href=\""
			     << MultiAtlasRoadmapEsc(r.Site().Url("multiatlas/" + ensemble.name))
			     << "\">Open MultiAtlas map</a></td><td>" << ensemble.tracks.GetCount() << "</td></tr>";
			count++;
		}
		if(count)
			body << "<table><thead><tr><th>Project</th><th>Project ID</th><th>MultiAtlas</th><th>Tracks</th></tr></thead><tbody>"
			     << String(rows) << "</tbody></table>";
		else body << "<p>No MultiAtlas projects are configured.</p>";
	}
	body << "</main>";
	WebsiteShell shell;
	shell.title = "MultiAtlas projects";
	shell.brand = "MultiAtlas";
	shell.brand_href = r.Site().Url("");
	shell.html_attributes = " lang=\"en\"" + WebsiteThemeHtmlAttributes(String(), String());
	shell.head_script = WebsiteThemeHeadScript();
	shell.topbar_html = WebsiteThemeControlsHtml();
	shell.body = String(body);
	r.Html(RenderAdminShell(shell));
}

static String MultiAtlasRoadmapTrackTitle(const String& id)
{
	if(id == "vision") return "Track: Vision";
	if(id == "ai") return "Track: AI";
	if(id == "agent") return "Track: Agent";
	if(id == "hud") return "Track: HUD";
	return "Track: " + id;
}

static int MultiAtlasRoadmapElementForRoad(const MultiAtlasMap& map, const String& atom, bool prefer_gate)
{
	for(int i = 0; i < map.elements.GetCount(); i++) {
		const MultiAtlasElement& element = map.elements[i];
		if(element.id.Find(atom) >= 0 || element.name.Find(atom) >= 0) return i;
	}
	for(int i = 0; i < map.elements.GetCount(); i++) {
		const MultiAtlasElement& element = map.elements[i];
		if((element.kind == "place") == prefer_gate) return i;
	}
	return map.elements.IsEmpty() ? -1 : 0;
}

static MultiAtlasMap MultiAtlasRoadmapCopyMap(const MultiAtlasMap& source)
{
	MultiAtlasMap copy;
	copy.map = source.map; copy.track_id = source.track_id; copy.version = source.version;
	copy.world.width = source.world.width; copy.world.height = source.world.height;
	for(const MultiAtlasElement& element : source.elements) copy.elements.Add(clone(element));
	for(const MultiAtlasRoad& road : source.roads) copy.roads.Add(clone(road));
	return copy;
}

bool LoadCanvas(const MultiAtlasEnsembleDef& def, MultiAtlasCanvasData& data, String& error)
{
	MultiAtlasReplayInput input;
	Vector<MultiAtlasWarningAck> acks;
	if(!MultiAtlasLoadReplayInput(def, input, acks, error)) return false;
	MultiAtlasReplayContext replay_context;
	MultiAtlasReplayEnsemble(input, replay_context);
	data.ensemble = def.name; data.title = def.title;
	MultiAtlasCanvasLane meta;
	meta.id = "meta"; meta.title = "Meta Release Spine";
	meta.map.track_id = "meta";
	for(const MultiAtlasVersion& v : input.meta.versions) {
		MultiAtlasVersion c; c.vid=v.vid; c.code=v.code; c.requires <<= v.requires; meta.versions.Add(pick(c));
		if(v.code % 1000 == 999) {
			String gate = Format("%d.%d.%d", v.code / 1000000, (v.code / 1000) % 1000, v.code % 1000);
			bool passed = replay_context.exported_atoms_by_track.Find("meta") >= 0 &&
				FindIndex(replay_context.exported_atoms_by_track.Get("meta"), "tested:" + gate) >= 0;
			meta.gate_status.GetAdd(Format("%d.%d", v.code / 1000000, (v.code / 1000) % 1000)) = passed ? "PASS" : "PENDING";
		}
	}
	String root = MultiAtlasResolveTrackStore(def, "meta"), rev;
	if(!MultiAtlasLoadTrackMap(root, def.meta_family, meta.map, rev, error)) return false;
	for(const MultiAtlasEvent& ev : input.meta.events)
		if(ev.subject_kind == "element" && !ev.subject_id.IsEmpty()) meta.element_versions.GetAdd(ev.subject_id) = ev.vid;
	for(const MultiAtlasElement& element : meta.map.elements)
		if(meta.element_versions.Find(element.id) < 0 && !meta.map.version.IsEmpty())
			meta.element_versions.GetAdd(element.id) = meta.map.version;
	data.lanes.Add(pick(meta));
	for(const MultiAtlasTrackDef& track : def.tracks) {
		MultiAtlasCanvasLane lane;
		lane.id = track.track_id;
		lane.title = MultiAtlasRoadmapTrackTitle(track.track_id);
		lane.branch = track.branch;
		for(const MultiAtlasTrackReplayInput& replay : input.tracks)
			if(replay.track_id == track.track_id) {
				for(const MultiAtlasVersion& v : replay.versions) {
					MultiAtlasVersion c; c.vid=v.vid; c.code=v.code; c.requires <<= v.requires; lane.versions.Add(pick(c));
					if(v.code % 1000 == 999) {
						String gate = Format("%d.%d.%d", v.code / 1000000, (v.code / 1000) % 1000, v.code % 1000);
						bool passed = replay_context.exported_atoms_by_track.Find(track.track_id) >= 0 &&
						             FindIndex(replay_context.exported_atoms_by_track.Get(track.track_id), "tested:" + gate) >= 0;
						lane.gate_status.GetAdd(Format("%d.%d", v.code / 1000000, (v.code / 1000) % 1000)) = passed ? "PASS" : "PENDING";
					}
				}
				for(const MultiAtlasEvent& ev : replay.events)
					if(ev.subject_kind == "element" && !ev.subject_id.IsEmpty()) lane.element_versions.GetAdd(ev.subject_id) = ev.vid;
			}
		String path = MultiAtlasResolveTrackStore(def, track.track_id);
		if(!MultiAtlasLoadTrackMap(path, track.family, lane.map, rev, error)) return false;
		for(const MultiAtlasElement& element : lane.map.elements)
			if(lane.element_versions.Find(element.id) < 0 && !lane.map.version.IsEmpty())
				lane.element_versions.GetAdd(element.id) = lane.map.version;
		data.lanes.Add(pick(lane));
	}
	// Turn declared cross-track version requirements into visible producer-to-consumer roads.
	for(int target = 0; target < data.lanes.GetCount(); target++) {
		MultiAtlasCanvasLane& consumer = data.lanes[target];
		for(const MultiAtlasVersion& version : consumer.versions)
			for(const String& literal : version.requires) {
				MultiAtlasContractRef ref; String parse_error;
				if(!MultiAtlasParseContract(literal, ref, parse_error) || ref.track_id.IsEmpty()) continue;
				int source = -1;
				for(int i = 1; i < data.lanes.GetCount(); i++)
					if(data.lanes[i].id == ref.track_id) { source = i; break; }
				if(source < 0) continue;
				int from = MultiAtlasRoadmapElementForRoad(data.lanes[source].map, ref.atom_name, false);
				int to = MultiAtlasRoadmapElementForRoad(consumer.map, ref.atom_name, true);
				if(from < 0 || to < 0) continue;
				MultiAtlasRoad road;
				road.from = data.lanes[source].map.elements[from].id;
				road.to = consumer.map.elements[to].id;
				road.from_track = data.lanes[source].id;
				road.to_track = consumer.id;
				road.road_kind = "dependency";
				road.label = literal;
				consumer.map.roads.Add(pick(road));
			}
	}
	return true;
}

void Overview(WebRequest& r)
{
	MultiAtlasEnsembleDef def; String error;
	if(!MultiAtlasFindEnsemble(MultiAtlasRoadmapSiteRoot(r.Site()), r.PathArg(0), def, error)) {
		r.Status(error.Find("not found") >= 0 ? 404 : 500, "MultiAtlas ensemble unavailable"); return;
	}
	if(!def.public_read && !r.HasUser()) { r.Status(404, "not found"); return; }
	MultiAtlasCanvasData data;
	if(!LoadCanvas(def, data, error)) { r.Status(500, "MultiAtlas overview data unavailable"); return; }
	WebsiteShell shell;
	shell.title = def.title + " - MultiAtlas"; shell.brand = "MultiAtlas";
	shell.brand_href = r.Site().Url("multiatlas/" + def.name);
	shell.html_attributes = " lang=\"en\"" + WebsiteThemeHtmlAttributes(String(), String());
	shell.head_script = WebsiteThemeHeadScript() + MultiAtlasCanvasStylesheet();
	shell.topbar_html = WebsiteThemeControlsHtml();
	shell.body = MultiAtlasCanvasStylesheet() + "<main><h1>" + MultiAtlasRoadmapEsc(def.title) + "</h1><p>Read-only ensemble overview. Select a lane or inspect the aligned version bands.</p><p><a href=\"/multiatlas/" + MultiAtlasRoadmapEsc(def.name) + "/timeline\">Open release timeline</a></p>" + MultiAtlasCanvasHtml(data) + "</main>" + MultiAtlasCanvasScript();
	r.Html(RenderAdminShell(shell));
}

void TrackDetail(WebRequest& r)
{
	String ensemble = r.PathArg(0), track_id = r.PathArg(1), error;
	MultiAtlasEnsembleDef def;
	if(!MultiAtlasFindEnsemble(MultiAtlasRoadmapSiteRoot(r.Site()), ensemble, def, error)) {
		r.Status(error.Find("not found") >= 0 ? 404 : 500, "MultiAtlas ensemble unavailable"); return;
	}
	if(!def.public_read && !r.HasUser()) { r.Status(404, "not found"); return; }
	const MultiAtlasTrackDef *track = NULL;
	for(const MultiAtlasTrackDef& candidate : def.tracks)
		if(candidate.track_id == track_id) { track = &candidate; break; }
	if(!track) { r.Status(404, "MultiAtlas track not found"); return; }

	// Load the selected track's full snapshot directly; the overview load also
	// derives cross-track roads, which the detail view reuses without copying its UI.
	MultiAtlasCanvasData overview;
	if(!LoadCanvas(def, overview, error)) { r.Status(500, "MultiAtlas track data unavailable"); return; }
	MultiAtlasTrackDetail detail;
	detail.ensemble = def.name; detail.ensemble_title = def.title;
	detail.track_id = track_id; detail.title = MultiAtlasRoadmapTrackTitle(track_id);
	detail.branch = track->branch;
	// These same-origin root paths keep navigation on the composed engine
	// listener instead of following the process-wide public host origin.
	detail.overview_url = "/multiatlas/" + def.name;
	detail.track_route_base = "/multiatlas/" + def.name + "/track";
	detail.timeline_url = "/multiatlas/" + def.name + "/timeline";
	bool found_lane = false;
	for(const MultiAtlasCanvasLane& lane : overview.lanes)
		if(lane.id == track_id) {
			detail.map = MultiAtlasRoadmapCopyMap(lane.map);
			int gates = lane.gate_status.GetCount();
			if(gates) detail.gate = lane.gate_status[gates - 1];
			found_lane = true; break;
		}
	if(!found_lane) { r.Status(404, "MultiAtlas track not found"); return; }
	// Cross-track requirements are stored on their consumer map. Mirror the
	// endpoint road onto the producer map as well so either side can follow it.
	for(const MultiAtlasCanvasLane& lane : overview.lanes)
		for(const MultiAtlasRoad& road : lane.map.roads) {
			String from_track = road.from_track.IsEmpty() ? lane.id : road.from_track;
			String to_track = road.to_track.IsEmpty() ? lane.id : road.to_track;
			if(from_track != track_id || to_track == track_id) continue;
			bool exists = false;
			for(const MultiAtlasRoad& current : detail.map.roads)
				if(current.from == road.from && current.to == road.to &&
				   current.from_track == road.from_track && current.to_track == road.to_track) {
					exists = true; break;
				}
			if(!exists) detail.map.roads.Add(clone(road));
		}
	String root = MultiAtlasResolveTrackStore(def, track_id), revision;
	MultiAtlasTrack family;
	if(root.IsEmpty() || !MultiAtlasLoadTrackFamily(root, track->family, family, revision, error) ||
	   !MultiAtlasLoadTrackRules(root, track->family, detail.rules, revision, error) ||
	   !MultiAtlasLoadTrackEvents(root, track->family, track_id, detail.events, error)) {
		r.Status(500, "MultiAtlas track detail unavailable"); return;
	}
	if(family.track_id != track_id || family.family != track->family) {
		r.Status(500, "MultiAtlas track identity mismatch"); return;
	}
	detail.family = pick(family);
	if(!detail.family.versions.IsEmpty()) {
		const MultiAtlasVersion& head = detail.family.versions[detail.family.versions.GetCount() - 1];
		detail.version = Format("%d.%d.%d", head.code / 1000000, (head.code / 1000) % 1000, head.code % 1000);
	}
	else detail.version = detail.map.version.IsEmpty() ? "No version" : detail.map.version;
	WebsiteShell shell;
	shell.title = detail.title + " - " + detail.ensemble_title;
	shell.brand = "MultiAtlas"; shell.brand_href = detail.overview_url;
	shell.html_attributes = " lang=\"en\"" + WebsiteThemeHtmlAttributes(String(), String());
	shell.head_script = WebsiteThemeHeadScript() + MultiAtlasClientStylesheet();
	shell.topbar_html = WebsiteThemeControlsHtml();
	shell.body = MultiAtlasClientStylesheet() + "<main><h1>" + MultiAtlasRoadmapEsc(detail.title) + "</h1><p><a href=\"" + MultiAtlasRoadmapEsc(detail.timeline_url) + "\">Release timeline</a> · "
	           + MultiAtlasRoadmapEsc(detail.branch) + " · gate " + MultiAtlasRoadmapEsc(detail.gate.IsEmpty() ? "not defined" : detail.gate)
	           + "</p>" + MultiAtlasClientHtml(detail) + "</main>" + MultiAtlasClientScript();
	r.Html(RenderAdminShell(shell));
}

}

void InstallMultiAtlasRoadmapPages(EngineSite& site)
{
	InstallMultiAtlasDashboard(site);
	site.Router().Add("", ProjectList);
	site.Router().Add("GET", "multiatlas/*/track/*", TrackDetail);
	site.Router().Add("GET", "multiatlas/*", Overview);
}

END_UPP_NAMESPACE
