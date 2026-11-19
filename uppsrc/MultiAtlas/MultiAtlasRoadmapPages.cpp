#include "MultiAtlasRoadmapPages.h"
#include "MultiAtlasCanvas.h"
#include "MultiAtlasRegistry.h"
#include "MultiAtlasReplayLoader.h"
#include "MultiAtlasStore.h"
#include "MultiAtlasContract.h"
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
	shell.body = "<main><h1>" + MultiAtlasRoadmapEsc(def.title) + "</h1><p>Read-only ensemble overview. Select a lane or inspect the aligned version bands.</p>" + MultiAtlasCanvasHtml(data) + "</main>" + MultiAtlasCanvasScript();
	r.Html(RenderAdminShell(shell));
}

}

void InstallMultiAtlasRoadmapPages(EngineSite& site)
{
	site.Router().Add("GET", "multiatlas/*", Overview);
}

END_UPP_NAMESPACE
