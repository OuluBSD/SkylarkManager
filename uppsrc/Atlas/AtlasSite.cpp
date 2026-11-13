#include "AtlasSite.h"
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// Atlas: the topological map webapp. The map is the authoritative plan of
// record for spatial structure, status and connections. Reads need a signed-in
// account; every mutation needs the "admin" role, a fenced POST (the engine
// checks Origin and the CSRF token), and the revision the form was rendered
// from, so a stale page cannot overwrite a newer map.

String AtlasStylesheet();  // AtlasClient.cpp
String AtlasScript();      // AtlasClient.cpp

static const char *ATLAS_COLLECTION = "maps";
static const char *ATLAS_ADMIN_ROLE = "admin";

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

// JSON placed inside a <script> element: no markup-significant characters
// may survive, or a name could close the element.
static String ScriptJson(const String& json)
{
	StringBuffer out;
	for(int i = 0; i < json.GetCount(); i++) {
		char c = json[i];
		if(c == '<')
			out << "\\u003c";
		else if(c == '>')
			out << "\\u003e";
		else if(c == '&')
			out << "\\u0026";
		else
			out.Cat(c);
	}
	return String(out);
}

static String NowIso()
{
	Time t = GetUtcTime();
	return Format("%04d-%02d-%02dT%02d:%02d:%02dZ", t.year, t.month, t.day, t.hour, t.minute, t.second);
}

static String JoinLines(const Vector<String>& v)
{
	StringBuffer b;
	for(int i = 0; i < v.GetCount(); i++) {
		if(i)
			b << "; ";
		b << v[i];
	}
	return String(b);
}

static IRecordCollection& Maps(const String& data_root)
{
	return TheStoreManager().JsonCollection("atlas", data_root, ATLAS_COLLECTION);
}

String AtlasDocKey(const String& map, const String& version)
{
	return map + "-" + version;
}

bool AtlasLoadMap(const String& data_root, const String& key, AtlasMap& m, String& revision, String& error)
{
	String json;
	StoreResult g = Maps(data_root).Get(key, json, revision);
	if(!g.ok) {
		error = "map not found: " + key;
		return false;
	}
	m = AtlasMap();
	if(!LoadFromJson(m, json)) {
		error = "map record is unreadable: " + key;
		return false;
	}
	return true;
}

bool AtlasSaveMap(const String& data_root, const String& key, const AtlasMap& m,
                  const String& expected_revision, String& new_revision, String& error)
{
	StoreResult p = Maps(data_root).Put(key, StoreAsJson(m), expected_revision, new_revision);
	if(!p.ok) {
		error = p.error;
		return false;
	}
	return true;
}

static bool CanEdit(WebRequest& r)
{
	for(const String& role : r.User().roles)
		if(role == ATLAS_ADMIN_ROLE)
			return true;
	return false;
}

static bool RequireLogin(WebRequest& r)
{
	if(r.HasUser())
		return true;
	r.Redirect(r.Site().Url("session/login"));
	return false;
}

static String CsrfField(WebRequest& r)
{
	return "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + Esc(r.CsrfToken()) + "\">";
}

static String RevisionField(const String& revision)
{
	return "<input type=\"hidden\" name=\"expected_revision\" value=\"" + Esc(revision) + "\">";
}

static void Page(WebRequest& r, const String& title, const String& body, const String& body_attrs = String())
{
	r.Html("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
	       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	       "<title>" + Esc(title) + " - Atlas</title>" + AtlasStylesheet() + "</head>"
	       "<body" + body_attrs + ">"
	       "<nav class=\"atlas-nav\"><a href=\"" + Esc(r.Site().Url("atlas")) + "\">Atlas</a> " + SessionChromeHtml(r) + "</nav>"
	       "<main><h1>" + Esc(title) + "</h1>" + body + "</main></body></html>");
}

static void Refuse(WebRequest& r, int code, const String& message)
{
	r.Status(code, message);
	Page(r, "Atlas", "<p class=\"err\">" + Esc(message) + "</p>"
	     "<p><a href=\"" + Esc(r.Site().Url("atlas")) + "\">Back to the map list</a></p>");
}

static void JsonFail(WebRequest& r, int code, const String& error)
{
	ValueMap m;
	m.Add("ok", false);
	m.Add("error", error);
	r.Json(m, code);
}

static String KindOptions(const String& selected)
{
	static const char *kinds[] = { "continent", "block", "place", "sea" };
	StringBuffer b;
	for(const char *k : kinds)
		b << "<option value=\"" << k << "\"" << (selected == k ? " selected" : "") << ">" << k << "</option>";
	return String(b);
}

static String StatusOptions(const String& selected)
{
	static const char *statuses[] = { "hot", "active", "planned", "not-ready", "future", "settled", "trouble" };
	StringBuffer b;
	for(const char *s : statuses)
		b << "<option value=\"" << s << "\"" << (selected == s ? " selected" : "") << ">" << s << "</option>";
	return String(b);
}

static String ParentOptions(const AtlasMap& m, const String& selected)
{
	StringBuffer b;
	b << "<option value=\"\"" << (selected.IsEmpty() ? " selected" : "") << ">world plane</option>";
	for(const AtlasElement& e : m.elements)
		b << "<option value=\"" << Esc(e.id) << "\"" << (selected == e.id ? " selected" : "") << ">"
		  << Esc(e.name) << " (" << Esc(e.id) << ")</option>";
	return String(b);
}

static String ElementOptions(const AtlasMap& m, const String& selected)
{
	StringBuffer b;
	for(const AtlasElement& e : m.elements)
		b << "<option value=\"" << Esc(e.id) << "\"" << (selected == e.id ? " selected" : "") << ">"
		  << Esc(e.name) << " (" << Esc(e.id) << ")</option>";
	return String(b);
}

// ---------------------------------------------------------------------------
// Reads.
// ---------------------------------------------------------------------------

static void MapList(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	Vector<String> ids;
	Maps(r.Site().data_root).List(ids);
	StringBuffer b;
	if(ids.IsEmpty())
		b << "<p>No maps yet.</p>";
	else {
		b << "<table><tr><th>Map</th><th>Version</th><th>Title</th><th>Elements</th><th>Roads</th></tr>";
		for(const String& id : ids) {
			AtlasMap m;
			String rev, err;
			if(!AtlasLoadMap(r.Site().data_root, id, m, rev, err))
				continue;
			b << "<tr><td><a href=\"" << Esc(r.Site().Url("atlas/" + id)) << "\">" << Esc(m.map) << "</a></td><td>"
			  << Esc(m.version) << "</td><td>" << Esc(m.title) << "</td><td>" << m.elements.GetCount()
			  << "</td><td>" << m.roads.GetCount() << "</td></tr>";
		}
		b << "</table>";
	}
	if(CanEdit(r))
		b << "<details><summary>Create a map</summary>"
		  << "<form method=\"post\" action=\"" << Esc(r.Site().Url("atlas/create")) << "\">" << CsrfField(r)
		  << "<label>Map id <input name=\"map\" required pattern=\"[A-Za-z0-9_-]+\"></label>"
		  << "<label>Version <input name=\"version\" required value=\"v0.0.1\"></label>"
		  << "<label>Title <input name=\"title\" required></label>"
		  << "<label>World width <input name=\"width\" value=\"2400\"></label>"
		  << "<label>World height <input name=\"height\" value=\"1600\"></label>"
		  << "<button type=\"submit\">Create</button></form></details>";
	Page(r, "Maps", String(b));
}

static void ElementTable(WebRequest& r, const AtlasMap& m, const String& key, StringBuffer& b)
{
	b << "<table><tr><th>Name</th><th>Kind</th><th>Status</th><th>Parent</th><th>Inside</th><th>Goal</th></tr>";
	for(const AtlasElement& e : m.elements) {
		String parent_name;
		int p = m.FindElement(e.parent);
		if(p >= 0)
			parent_name = m.elements[p].name;
		b << "<tr><td><a href=\"" << Esc(r.Site().Url("atlas/" + key + "/element/" + e.id)) << "\">"
		  << Esc(e.name) << "</a></td><td>" << Esc(e.kind) << "</td><td><span class=\"status status-"
		  << Esc(e.status) << "\">" << Esc(e.status) << "</span></td><td>" << Esc(parent_name)
		  << "</td><td>" << AtlasChildCount(m, e.id) << "</td><td>" << (e.goal.picked ? "picked" : "")
		  << "</td></tr>";
	}
	b << "</table>";
}

static void RoadTable(const AtlasMap& m, StringBuffer& b)
{
	if(m.roads.IsEmpty()) {
		b << "<p>No roads yet.</p>";
		return;
	}
	b << "<table><tr><th>From</th><th>To</th><th>Kind</th><th>Label</th></tr>";
	for(const AtlasRoad& rd : m.roads) {
		int f = m.FindElement(rd.from);
		int t = m.FindElement(rd.to);
		b << "<tr><td>" << Esc(f >= 0 ? m.elements[f].name : rd.from) << "</td><td>"
		  << Esc(t >= 0 ? m.elements[t].name : rd.to) << "</td><td>" << Esc(rd.kind) << "</td><td>"
		  << Esc(rd.label) << "</td></tr>";
	}
	b << "</table>";
}

static String EditPanel(WebRequest& r, const AtlasMap& m, const String& key, const String& revision)
{
	String base = r.Site().Url("atlas/" + key);
	StringBuffer b;
	b << "<details class=\"edit\"><summary>Edit (admin)</summary>"
	  << "<form method=\"post\" action=\"" << Esc(base + "/element/add") << "\">" << CsrfField(r) << RevisionField(revision)
	  << "<h3>Add an element</h3>"
	  << "<label>Name <input name=\"name\" required></label>"
	  << "<label>Kind <select name=\"kind\">" << KindOptions("place") << "</select></label>"
	  << "<label>Status <select name=\"status\">" << StatusOptions("planned") << "</select></label>"
	  << "<label>Parent <select name=\"parent\">" << ParentOptions(m, String()) << "</select></label>"
	  << "<label>x <input name=\"x\" type=\"number\" value=\"40\"></label>"
	  << "<label>y <input name=\"y\" type=\"number\" value=\"40\"></label>"
	  << "<label>w <input name=\"w\" type=\"number\" value=\"180\"></label>"
	  << "<label>h <input name=\"h\" type=\"number\" value=\"100\"></label>"
	  << "<button type=\"submit\">Add element</button></form>";
	b << "<form method=\"post\" action=\"" << Esc(base + "/road/add") << "\">" << CsrfField(r) << RevisionField(revision)
	  << "<h3>Add a road</h3>"
	  << "<label>From <select name=\"from\">" << ElementOptions(m, String()) << "</select></label>"
	  << "<label>To <select name=\"to\">" << ElementOptions(m, String()) << "</select></label>"
	  << "<label>Kind <select name=\"kind\"><option>progress</option><option>communication</option><option>dependency</option></select></label>"
	  << "<label>Label <input name=\"label\"></label>"
	  << "<button type=\"submit\">Add road</button></form>";
	b << "<form method=\"post\" action=\"" << Esc(base + "/seed") << "\">" << CsrfField(r) << RevisionField(revision)
	  << "<h3>Seed import (empty maps only)</h3>"
	  << "<p class=\"hint\">Two spaces per containment level: <code>Name | kind | status | note</code>. "
	  << "Edges: <code>A -&gt; B: label</code> progress, <code>A =&gt; B</code> dependency, <code>A ~&gt; B</code> communication.</p>"
	  << "<textarea name=\"seed\" rows=\"12\" cols=\"80\"></textarea>"
	  << "<button type=\"submit\">Import</button></form></details>";
	return String(b);
}

static void MapView(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	String key = r.PathArg(0);
	AtlasMap m;
	String rev, err;
	if(!AtlasLoadMap(r.Site().data_root, key, m, rev, err)) {
		Refuse(r, 404, err);
		return;
	}
	bool edit = CanEdit(r);
	StringBuffer b;
	b << "<p class=\"meta\">" << Esc(m.map) << " " << Esc(m.version) << " &middot; " << m.elements.GetCount()
	  << " elements &middot; " << m.roads.GetCount() << " roads &middot; world " << m.world.width << " x "
	  << m.world.height << "</p>"
	  << "<div class=\"toolbar\"><span id=\"atlas-plane\"></span> <button type=\"button\" id=\"atlas-up\">Up</button> "
	  << "<span class=\"hint\">Wheel to zoom, drag to pan, click a block to go inside."
	  << (edit ? " Drag an element to move it." : "") << "</span></div>"
	  << "<svg id=\"atlas-canvas\" role=\"img\" aria-label=\"Topology map\"></svg>"
	  << "<noscript><p>The canvas needs JavaScript; the lists below show the same map.</p></noscript>"
	  << "<h2>Elements</h2>";
	ElementTable(r, m, key, b);
	b << "<h2>Roads</h2>";
	RoadTable(m, b);
	if(edit)
		b << EditPanel(r, m, key, rev);

	String data = "{\"revision\":" + AsJSON(Value(rev)) + ",\"map\":" + StoreAsJson(m) + "}";
	String attrs = String(" data-editable=\"") + (edit ? "1" : "0") + "\" data-csrf=\"" + Esc(r.CsrfToken())
	             + "\" data-base=\"" + Esc(r.Site().Url("atlas/" + key)) + "\"";
	b << "<script type=\"application/json\" id=\"atlas-data\">" << ScriptJson(data) << "</script>"
	  << "<script>" << AtlasScript() << "</script>";
	Page(r, m.title, String(b), attrs);
}

static void ElementView(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	String key = r.PathArg(0);
	AtlasMap m;
	String rev, err;
	if(!AtlasLoadMap(r.Site().data_root, key, m, rev, err)) {
		Refuse(r, 404, err);
		return;
	}
	String id = r.PathArg(1);
	int i = m.FindElement(id);
	if(i < 0) {
		Refuse(r, 404, "element not found: " + id);
		return;
	}
	const AtlasElement& e = m.elements[i];
	String base = r.Site().Url("atlas/" + key);

	StringBuffer b;
	b << "<p><a href=\"" << Esc(base) << "\">Back to the map</a></p>";
	b << "<dl class=\"facts\"><dt>Id</dt><dd>" << Esc(e.id) << "</dd><dt>Kind</dt><dd>" << Esc(e.kind)
	  << "</dd><dt>Status</dt><dd><span class=\"status status-" << Esc(e.status) << "\">" << Esc(e.status)
	  << "</span></dd><dt>Goal</dt><dd>" << (e.goal.picked ? "picked" : "not picked") << "</dd>"
	  << "<dt>Inside</dt><dd>" << AtlasChildCount(m, e.id) << " elements</dd>"
	  << "<dt>Updated</dt><dd>" << Esc(e.updated_at) << "</dd></dl>";

	b << "<h2>Containment path</h2><ol class=\"path\">";
	Vector<String> path = AtlasPath(m, e.id);
	for(const String& pid : path) {
		int p = m.FindElement(pid);
		b << "<li><a href=\"" << Esc(base + "/element/" + pid) << "\">" << Esc(m.elements[p].name) << "</a></li>";
	}
	b << "</ol>";

	b << "<h2>Roads</h2>";
	bool any = false;
	for(const AtlasRoad& rd : m.roads) {
		if(rd.from != e.id && rd.to != e.id)
			continue;
		if(!any)
			b << "<table><tr><th>From</th><th>To</th><th>Kind</th><th>Label</th></tr>";
		any = true;
		int f = m.FindElement(rd.from);
		int t = m.FindElement(rd.to);
		b << "<tr><td>" << Esc(f >= 0 ? m.elements[f].name : rd.from) << "</td><td>"
		  << Esc(t >= 0 ? m.elements[t].name : rd.to) << "</td><td>" << Esc(rd.kind) << "</td><td>"
		  << Esc(rd.label) << "</td></tr>";
	}
	if(any)
		b << "</table>";
	else
		b << "<p>No roads touch this element.</p>";

	b << "<h2>Notes</h2><p>" << (e.notes.IsEmpty() ? String("None.") : Esc(e.notes)) << "</p>";
	if(e.goal.picked || e.goal.note.GetCount())
		b << "<h2>Goal</h2><p>" << Esc(e.goal.note) << "</p>";

	if(CanEdit(r)) {
		b << "<details class=\"edit\"><summary>Edit (admin)</summary>"
		  << "<form method=\"post\" action=\"" << Esc(base + "/element/edit") << "\">" << CsrfField(r) << RevisionField(rev)
		  << "<input type=\"hidden\" name=\"id\" value=\"" << Esc(e.id) << "\">"
		  << "<label>Name <input name=\"name\" required value=\"" << Esc(e.name) << "\"></label>"
		  << "<label>Kind <select name=\"kind\">" << KindOptions(e.kind) << "</select></label>"
		  << "<label>Status <select name=\"status\">" << StatusOptions(e.status) << "</select></label>"
		  << "<label>Parent <select name=\"parent\">" << ParentOptions(m, e.parent) << "</select></label>"
		  << "<label>x <input name=\"x\" type=\"number\" value=\"" << e.x << "\"></label>"
		  << "<label>y <input name=\"y\" type=\"number\" value=\"" << e.y << "\"></label>"
		  << "<label>w <input name=\"w\" type=\"number\" value=\"" << e.w << "\"></label>"
		  << "<label>h <input name=\"h\" type=\"number\" value=\"" << e.h << "\"></label>"
		  << "<label><input type=\"checkbox\" name=\"goal_picked\" value=\"1\"" << (e.goal.picked ? " checked" : "")
		  << "> Picked as a goal of this version</label>"
		  << "<label>Goal note <input name=\"goal_note\" value=\"" << Esc(e.goal.note) << "\"></label>"
		  << "<label>Notes <textarea name=\"notes\" rows=\"4\">" << Esc(e.notes) << "</textarea></label>"
		  << "<button type=\"submit\">Save</button></form>"
		  << "<form method=\"post\" action=\"" << Esc(base + "/element/delete") << "\">" << CsrfField(r) << RevisionField(rev)
		  << "<input type=\"hidden\" name=\"id\" value=\"" << Esc(e.id) << "\">"
		  << "<button type=\"submit\">Delete</button> <span class=\"hint\">Refused while the element has children.</span></form>"
		  << "</details>";
	}
	Page(r, e.name, String(b));
}

// ---------------------------------------------------------------------------
// Writes. Each mutation: edit role, the map, the expected revision, the change,
// validation of the whole map, then one revision-checked save.
// ---------------------------------------------------------------------------

static bool BeginEdit(WebRequest& r, AtlasMap& m, String& revision)
{
	if(!RequireLogin(r))
		return false;
	if(!CanEdit(r)) {
		Refuse(r, 403, "editing needs the admin role on this site");
		return false;
	}
	String err;
	if(!AtlasLoadMap(r.Site().data_root, r.PathArg(0), m, revision, err)) {
		Refuse(r, 404, err);
		return false;
	}
	if(r.Param("expected_revision") != revision) {
		Refuse(r, 409, "the map changed since this page was opened; reload and try again");
		return false;
	}
	return true;
}

static void FinishEdit(WebRequest& r, AtlasMap& m, const String& revision, const String& location)
{
	AtlasCheck check;
	AtlasValidate(m, check);
	if(!check.Ok()) {
		Refuse(r, 400, JoinLines(check.errors));
		return;
	}
	String new_rev, err;
	if(!AtlasSaveMap(r.Site().data_root, r.PathArg(0), m, revision, new_rev, err)) {
		Refuse(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return;
	}
	r.Redirect(location);
}

static String MapUrl(WebRequest& r)
{
	return r.Site().Url("atlas/" + r.PathArg(0));
}

static void AddElement(WebRequest& r)
{
	AtlasMap m;
	String rev;
	if(!BeginEdit(r, m, rev))
		return;
	AtlasElement e;
	e.name = r.Param("name");
	e.id = AtlasUniqueElementId(m, e.name);
	e.kind = r.Param("kind");
	e.status = r.Param("status");
	e.parent = r.Param("parent");
	e.x = r.IntParam("x");
	e.y = r.IntParam("y");
	e.w = r.IntParam("w");
	e.h = r.IntParam("h");
	e.updated_at = NowIso();
	m.elements.Add(pick(e));
	FinishEdit(r, m, rev, MapUrl(r));
}

static void EditElement(WebRequest& r)
{
	AtlasMap m;
	String rev;
	if(!BeginEdit(r, m, rev))
		return;
	int i = m.FindElement(r.Param("id"));
	if(i < 0) {
		Refuse(r, 404, "element not found");
		return;
	}
	AtlasElement& e = m.elements[i];
	e.name = r.Param("name");
	e.kind = r.Param("kind");
	e.status = r.Param("status");
	e.parent = r.Param("parent");
	e.x = r.IntParam("x");
	e.y = r.IntParam("y");
	e.w = r.IntParam("w");
	e.h = r.IntParam("h");
	e.notes = r.Param("notes");
	e.goal.picked = r.Param("goal_picked") == "1";
	e.goal.note = r.Param("goal_note");
	e.updated_at = NowIso();
	FinishEdit(r, m, rev, r.Site().Url("atlas/" + r.PathArg(0) + "/element/" + e.id));
}

static void DeleteElement(WebRequest& r)
{
	AtlasMap m;
	String rev;
	if(!BeginEdit(r, m, rev))
		return;
	String id = r.Param("id");
	int i = m.FindElement(id);
	if(i < 0) {
		Refuse(r, 404, "element not found");
		return;
	}
	if(AtlasChildCount(m, id) > 0) {
		Refuse(r, 400, "delete the element's children first");
		return;
	}
	for(int k = m.roads.GetCount() - 1; k >= 0; k--)
		if(m.roads[k].from == id || m.roads[k].to == id)
			m.roads.Remove(k);
	m.elements.Remove(i);
	FinishEdit(r, m, rev, MapUrl(r));
}

static void AddRoad(WebRequest& r)
{
	AtlasMap m;
	String rev;
	if(!BeginEdit(r, m, rev))
		return;
	AtlasRoad road;
	road.id = AtlasUniqueRoadId(m);
	road.from = r.Param("from");
	road.to = r.Param("to");
	road.kind = r.Param("kind");
	road.label = r.Param("label");
	m.roads.Add(pick(road));
	FinishEdit(r, m, rev, MapUrl(r));
}

static void DeleteRoad(WebRequest& r)
{
	AtlasMap m;
	String rev;
	if(!BeginEdit(r, m, rev))
		return;
	int i = m.FindRoad(r.Param("id"));
	if(i < 0) {
		Refuse(r, 404, "road not found");
		return;
	}
	m.roads.Remove(i);
	FinishEdit(r, m, rev, MapUrl(r));
}

static void SeedImport(WebRequest& r)
{
	AtlasMap m;
	String rev;
	if(!BeginEdit(r, m, rev))
		return;
	AtlasCheck check;
	if(!AtlasParseSeed(r.Param("seed"), m, check)) {
		Refuse(r, 400, JoinLines(check.errors));
		return;
	}
	FinishEdit(r, m, rev, MapUrl(r));
}

static void CreateMap(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	if(!CanEdit(r)) {
		Refuse(r, 403, "editing needs the admin role on this site");
		return;
	}
	AtlasMap m;
	m.map = r.Param("map");
	m.version = r.Param("version");
	m.title = r.Param("title");
	m.world.width = r.IntParam("width") > 0 ? r.IntParam("width") : 2400;
	m.world.height = r.IntParam("height") > 0 ? r.IntParam("height") : 1600;
	AtlasCheck check;
	AtlasValidate(m, check);
	String key = AtlasDocKey(m.map, m.version);
	if(!JsonFileStore::ValidId(key))
		check.errors.Add("map version has characters the store does not allow: " + m.version);
	if(!check.Ok()) {
		Refuse(r, 400, JoinLines(check.errors));
		return;
	}
	AtlasMap existing;
	String rev, err;
	if(AtlasLoadMap(r.Site().data_root, key, existing, rev, err)) {
		Refuse(r, 409, "a map with that id and version already exists");
		return;
	}
	if(!AtlasSaveMap(r.Site().data_root, key, m, String(), rev, err)) {
		Refuse(r, 500, err);
		return;
	}
	r.Redirect(r.Site().Url("atlas/" + key));
}

// The canvas's drag-to-move: a JSON answer, because the page does not reload.
static void MoveElement(WebRequest& r)
{
	if(!r.HasUser()) {
		JsonFail(r, 401, "not signed in");
		return;
	}
	if(!CanEdit(r)) {
		JsonFail(r, 403, "editing needs the admin role on this site");
		return;
	}
	AtlasMap m;
	String rev, err;
	if(!AtlasLoadMap(r.Site().data_root, r.PathArg(0), m, rev, err)) {
		JsonFail(r, 404, err);
		return;
	}
	if(r.Param("expected_revision") != rev) {
		JsonFail(r, 409, "the map changed since this page was opened");
		return;
	}
	int i = m.FindElement(r.Param("id"));
	if(i < 0) {
		JsonFail(r, 404, "element not found");
		return;
	}
	m.elements[i].x = r.IntParam("x");
	m.elements[i].y = r.IntParam("y");
	m.elements[i].updated_at = NowIso();
	AtlasCheck check;
	AtlasValidate(m, check);
	if(!check.Ok()) {
		JsonFail(r, 400, JoinLines(check.errors));
		return;
	}
	String new_rev;
	if(!AtlasSaveMap(r.Site().data_root, r.PathArg(0), m, rev, new_rev, err)) {
		JsonFail(r, err.Find("revision conflict") >= 0 ? 409 : 500, err);
		return;
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("revision", new_rev);
	r.Json(out);
}

void InstallAtlasSite(EngineSite& site)
{
	SiteRouter& rt = site.Router();
	rt.Add("", MapList); // the root is the map list, gated like every Atlas read (task 036)
	rt.Add("atlas", MapList);
	rt.Add("POST", "atlas/create", CreateMap);
	rt.Add("atlas/*", MapView);
	rt.Add("atlas/*/element/*", ElementView);
	rt.Add("POST", "atlas/*/element/move", MoveElement);
	rt.Add("POST", "atlas/*/element/add", AddElement);
	rt.Add("POST", "atlas/*/element/edit", EditElement);
	rt.Add("POST", "atlas/*/element/delete", DeleteElement);
	rt.Add("POST", "atlas/*/road/add", AddRoad);
	rt.Add("POST", "atlas/*/road/delete", DeleteRoad);
	rt.Add("POST", "atlas/*/seed", SeedImport);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("atlas-maps", "GET", "atlas", "session account",
	                                        "html", "List the map versions (read-only)", {}));
	cm.Add(site.id, site.title, HttpCommand("atlas-map", "GET", "atlas/*", "session account",
	                                        "html", "One map: canvas, elements and roads", { "map:string:required" }));
	cm.Add(site.id, site.title, HttpCommand("atlas-element", "GET", "atlas/*/element/*", "session account",
	                                        "html", "One element: containment path, roads, goal",
	                                        { "map:string:required", "id:string:required" }));
}

END_UPP_NAMESPACE
