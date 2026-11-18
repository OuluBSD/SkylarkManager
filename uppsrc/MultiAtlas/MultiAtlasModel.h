#ifndef _MultiAtlas_MultiAtlasModel_h_
#define _MultiAtlas_MultiAtlasModel_h_

#include <Core/Core.h>

NAMESPACE_UPP

// MultiAtlas records intentionally mirror Atlas' Jsonize-based data shape.
// Store envelopes are kept outside the record data by WebStorage.
struct MultiAtlasGoal : Moveable<MultiAtlasGoal> {
	bool picked = false;
	String note;
	void Jsonize(JsonIO& io) { io("picked", picked)("note", note); }
};

struct MultiAtlasElement : Moveable<MultiAtlasElement> {
	String id, name, track_id, kind, status;
	int x = 0, y = 0, w = 0, h = 0;
	String parent, notes;
	MultiAtlasGoal goal;

	void Jsonize(JsonIO& io) {
		io("id", id)("name", name)("track_id", track_id)("kind", kind)("status", status)
		  ("x", x)("y", y)("w", w)("h", h)("parent", parent)("goal", goal)("notes", notes);
	}
};

struct MultiAtlasRoad : Moveable<MultiAtlasRoad> {
	String from, to, from_track, to_track, road_kind, label;

	void Jsonize(JsonIO& io) {
		io("from", from)("to", to)("from_track", from_track)("to_track", to_track)
		  ("road_kind", road_kind)("label", label);
	}
};

struct MultiAtlasWorld : Moveable<MultiAtlasWorld> {
	int width = 2400, height = 1600;

	void Jsonize(JsonIO& io) { io("width", width)("height", height); }
};

struct MultiAtlasMap : Moveable<MultiAtlasMap> {
	String map, track_id, version;
	Vector<MultiAtlasElement> elements;
	Vector<MultiAtlasRoad> roads;
	MultiAtlasWorld world;

	void Jsonize(JsonIO& io) {
		io("map", map)("track_id", track_id)("version", version)("elements", elements)
		  ("roads", roads)("world", world);
	}

	int FindElement(const String& id) const;
};

struct MultiAtlasVersion : Moveable<MultiAtlasVersion> {
	String vid;
	int code = -1;
	Vector<String> requires;

	void Jsonize(JsonIO& io) { io("vid", vid)("code", code)("requires", requires); }
};

struct MultiAtlasTrack : Moveable<MultiAtlasTrack> {
	String track_id, family, repo_path, branch, store_rel_path, head_key;
	Vector<MultiAtlasVersion> versions;

	void Jsonize(JsonIO& io) {
		io("track_id", track_id)("family", family)("repo_path", repo_path)("branch", branch)
		  ("store_rel_path", store_rel_path)("head_key", head_key)("versions", versions);
	}
};

struct MultiAtlasMetaFamily : Moveable<MultiAtlasMetaFamily> {
	String ensemble_name, title, meta_family;
	Vector<MultiAtlasTrack> tracks;
	Vector<MultiAtlasVersion> versions;

	void Jsonize(JsonIO& io) {
		io("ensemble_name", ensemble_name)("title", title)("meta_family", meta_family)
		  ("tracks", tracks)("versions", versions);
	}
};

struct MultiAtlasEvent : Moveable<MultiAtlasEvent> {
	String id, at, actor, track_id, vid, subject_kind, subject_id, type, action, text;
	Vector<String> effects, requires;

	void Jsonize(JsonIO& io) {
		io("id", id)("at", at)("actor", actor)("track_id", track_id)("vid", vid)
		  ("subject_kind", subject_kind)("subject_id", subject_id)("type", type)
		  ("action", action)("effects", effects)("requires", requires)("text", text);
	}
};

struct MultiAtlasCheck : Moveable<MultiAtlasCheck> {
	Vector<String> errors;
	bool Ok() const { return errors.IsEmpty(); }
};

// Returns -1 for unknown elements, missing ancestors, or cycles. Root elements
// have depth zero. Paths are root-to-element and empty for invalid chains.
int MultiAtlasDepth(const MultiAtlasMap& map, const String& id);
Vector<String> MultiAtlasPath(const MultiAtlasMap& map, const String& id);
Vector<String> MultiAtlasPlaneElements(const MultiAtlasMap& map, const String& parent);
void MultiAtlasValidate(const MultiAtlasMap& map, MultiAtlasCheck& out);

// Atlas records are stored as {"envelope": {...}, "data": {...}} by
// WebStorage. Accept that envelope and older direct-data JSON records.
template <class T>
bool MultiAtlasLoadRecord(T& record, const String& json, String& revision)
{
	try {
		Value root = ParseJSON(json);
		if(!root.Is<ValueMap>())
			return false;
		ValueMap doc = root;
		int data_key = doc.Find("data");
		int envelope_key = doc.Find("envelope");
		bool has_data = data_key >= 0;
		bool has_envelope = envelope_key >= 0;
		T parsed;
		String parsed_revision;
		if(has_data || has_envelope) {
			Value data = doc.Get("data");
			Value env_value = doc.Get("envelope");
			if(!has_data || !has_envelope || !data.Is<ValueMap>() || !env_value.Is<ValueMap>())
				return false;
			ValueMap env = env_value;
			parsed_revision = AsString(env.Get("revision"));
			LoadFromJsonValue(parsed, data);
			record = pick(parsed);
			revision = parsed_revision;
			return true;
		}
		LoadFromJsonValue(parsed, root);
		record = pick(parsed);
		revision.Clear();
		return true;
	}
	catch(ValueTypeError) { return false; }
	catch(JsonizeError) { return false; }
}

END_UPP_NAMESPACE

#endif
