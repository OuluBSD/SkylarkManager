#include "AtlasModel.h"

NAMESPACE_UPP

static bool MergeJsonEqual(const Value& a, const Value& b)
{
	return AsJSON(a, true) == AsJSON(b, true);
}

static String MergeJsonText(const Value& value)
{
	return value.IsNull() ? String("(missing)") : AsJSON(value, true);
}

struct AtlasMergeContext {
	const Vector<AtlasMergeResolution>& choices;
	Vector<AtlasMergeConflict>& conflicts;
	Vector<String> used_choices;
	String actor;
	String at;
	String error;
};

static Value MergeConflictValue(const String& path, const Value& base, const Value& ours, const Value& theirs,
	                           AtlasMergeContext& context)
{
	AtlasMergeConflict conflict;
	conflict.path = path;
	conflict.base_value = MergeJsonText(base);
	conflict.ours_value = MergeJsonText(ours);
	conflict.theirs_value = MergeJsonText(theirs);
	conflict.resolution = "none";
	for(const AtlasMergeResolution& choice : context.choices) {
		if(choice.path != path)
			continue;
		if(choice.choice != "ours" && choice.choice != "theirs") {
			context.error = "merge resolution for " + path + " must be ours or theirs";
			break;
		}
		conflict.resolution = choice.choice;
		conflict.resolved_by = context.actor;
		conflict.resolved_at = context.at;
		context.used_choices.Add(path);
		break;
	}
	String resolution = conflict.resolution;
	context.conflicts.Add(pick(conflict));
	return resolution == "theirs" ? theirs : ours;
}

static bool MergeStringMap(const Value& value, const String& path, ValueMap& out, String& error)
{
	out.Clear();
	if(value.IsNull())
		return true;
	if(!value.Is<ValueMap>()) {
		error = path + " must be an object";
		return false;
	}
	out = value;
	return true;
}

static Value MergeJson(const Value& base, const Value& ours, const Value& theirs,
	                   const String& path, AtlasMergeContext& context);

static bool MergeArrayById(const Value& value, const String& id_field, const String& path,
	                       ValueMap& by_id, Vector<String>& order, AtlasMergeContext& context)
{
	by_id.Clear();
	if(value.IsNull())
		return true;
	if(!value.Is<ValueArray>()) {
		context.error = path + " must be an array";
		return false;
	}
	ValueArray array = value;
	for(int i = 0; i < array.GetCount(); i++) {
		if(!array[i].Is<ValueMap>()) {
			context.error = path + " entry must be an object";
			return false;
		}
		ValueMap item = array[i];
		String id = item[id_field].ToString();
		if(id.IsEmpty() || by_id.Find(id) >= 0) {
			context.error = path + " contains a missing or duplicate " + id_field;
			return false;
		}
		by_id.Add(id, array[i]);
		order.Add(id);
	}
	return true;
}

static Value MergeIdentifiedArray(const Value& base, const Value& ours, const Value& theirs,
	                             const String& id_field, const String& path, AtlasMergeContext& context)
{
	ValueMap b, o, t;
	Vector<String> bo, oo, to;
	if(!MergeArrayById(base, id_field, path, b, bo, context)
	|| !MergeArrayById(ours, id_field, path, o, oo, context)
	|| !MergeArrayById(theirs, id_field, path, t, to, context))
		return Value();
	Index<String> ids;
	for(const String& id : bo) ids.FindAdd(id);
	for(const String& id : oo) ids.FindAdd(id);
	for(const String& id : to) ids.FindAdd(id);
	ValueArray merged;
	for(const String& id : ids) {
		Value bv = b.Find(id) < 0 ? Value() : b.Get(id);
		Value ov = o.Find(id) < 0 ? Value() : o.Get(id);
		Value tv = t.Find(id) < 0 ? Value() : t.Get(id);
		Value value = MergeJson(bv, ov, tv, path + "/" + id, context);
		if(!value.IsNull())
			merged.Add(value);
		if(!context.error.IsEmpty())
			return Value();
	}
	return merged;
}

static bool MergeLiteralStates(const Value& value, const String& path, ValueMap& states,
	                           Vector<String>& order, String& error)
{
	states.Clear();
	if(value.IsNull())
		return true;
	if(!value.Is<ValueArray>()) {
		error = path + " must be an array of literals";
		return false;
	}
	ValueArray literals = value;
	for(int i = 0; i < literals.GetCount(); i++) {
		if(!IsString(literals[i])) {
			error = path + " contains a non-string literal";
			return false;
		}
		String literal = literals[i].ToString();
		String atom = literal.StartsWith("!") ? literal.Mid(1) : literal;
		if(atom.IsEmpty()) {
			error = path + " contains an empty literal";
			return false;
		}
		int found = states.Find(atom);
		if(found >= 0 && states.GetValue(found).ToString() != literal) {
			error = path + " contains both polarities of " + atom;
			return false;
		}
		if(found < 0) {
			states.Add(atom, literal);
			order.Add(atom);
		}
	}
	return true;
}

static Value MergeLiteralArray(const Value& base, const Value& ours, const Value& theirs,
	                           const String& path, AtlasMergeContext& context)
{
	ValueMap b, o, t;
	Vector<String> bo, oo, to;
	if(!MergeLiteralStates(base, path, b, bo, context.error)
	|| !MergeLiteralStates(ours, path, o, oo, context.error)
	|| !MergeLiteralStates(theirs, path, t, to, context.error))
		return Value();
	Index<String> atoms;
	for(const String& atom : bo) atoms.FindAdd(atom);
	for(const String& atom : oo) atoms.FindAdd(atom);
	for(const String& atom : to) atoms.FindAdd(atom);
	ValueArray merged;
	for(const String& atom : atoms) {
		Value bv = b.Find(atom) < 0 ? Value() : b.Get(atom);
		Value ov = o.Find(atom) < 0 ? Value() : o.Get(atom);
		Value tv = t.Find(atom) < 0 ? Value() : t.Get(atom);
		Value value = MergeJson(bv, ov, tv, path + "/" + atom, context);
		if(!value.IsNull())
			merged.Add(value);
		if(!context.error.IsEmpty())
			return Value();
	}
	return merged;
}

static Value MergeJsonObject(const Value& base, const Value& ours, const Value& theirs,
	                        const String& path, AtlasMergeContext& context)
{
	ValueMap b, o, t;
	if(!MergeStringMap(base, path, b, context.error) || !MergeStringMap(ours, path, o, context.error)
	|| !MergeStringMap(theirs, path, t, context.error))
		return Value();
	Index<String> keys;
	for(int i = 0; i < b.GetCount(); i++) keys.FindAdd(b.GetKey(i).ToString());
	for(int i = 0; i < o.GetCount(); i++) keys.FindAdd(o.GetKey(i).ToString());
	for(int i = 0; i < t.GetCount(); i++) keys.FindAdd(t.GetKey(i).ToString());
	ValueMap merged;
	for(const String& key : keys) {
		Value bv = b.Find(key) < 0 ? Value() : b.Get(key);
		Value ov = o.Find(key) < 0 ? Value() : o.Get(key);
		Value tv = t.Find(key) < 0 ? Value() : t.Get(key);
		String field_path = path + "/" + key;
		Value value;
		if(path == "map" && (key == "elements" || key == "roads"))
			value = MergeIdentifiedArray(bv, ov, tv, "id", field_path, context);
		else if(path == "family" && key == "versions")
			value = MergeIdentifiedArray(bv, ov, tv, "vid", field_path, context);
		else if(key == "requires" && path.StartsWith("family/versions/"))
			value = MergeLiteralArray(bv, ov, tv, field_path, context);
		else
			value = MergeJson(bv, ov, tv, field_path, context);
		if(!value.IsNull())
			merged.Add(key, value);
		if(!context.error.IsEmpty())
			return Value();
	}
	return merged;
}

static Value MergeJson(const Value& base, const Value& ours, const Value& theirs,
	                   const String& path, AtlasMergeContext& context)
{
	if(MergeJsonEqual(ours, theirs))
		return ours;
	if(MergeJsonEqual(ours, base))
		return theirs;
	if(MergeJsonEqual(theirs, base))
		return ours;
	if((base.Is<ValueMap>() || base.IsNull()) && ours.Is<ValueMap>() && theirs.Is<ValueMap>())
		return MergeJsonObject(base.IsNull() ? Value(ValueMap()) : base, ours, theirs, path, context);
	if(path.StartsWith("family/versions/") && path.EndsWith("/requires"))
		return MergeLiteralArray(base, ours, theirs, path, context);
	return MergeConflictValue(path, base, ours, theirs, context);
}

template <class T>
static Value MergeSnapshot(const T& base, const T& ours, const T& theirs,
	                       const String& name, AtlasMergeContext& context)
{
	return MergeJson(ParseJSON(StoreAsJson(base)), ParseJSON(StoreAsJson(ours)),
	                 ParseJSON(StoreAsJson(theirs)), name, context);
}

static bool LoadMergeSnapshot(const Value& value, AtlasMap& map, String& error)
{
	try {
		LoadFromJsonValue(map, value);
		return true;
	}
	catch(Exc& e) {
		error = String("merged map is invalid: ") + e;
		return false;
	}
}

static bool LoadMergeSnapshot(const Value& value, AtlasFamily& family, String& error)
{
	try {
		LoadFromJsonValue(family, value);
		return true;
	}
	catch(Exc& e) {
		error = String("merged family is invalid: ") + e;
		return false;
	}
}

bool AtlasThreeWayMerge(const AtlasMergeBase& base, const AtlasMap& ours_map, const AtlasFamily& ours_family,
	                    const AtlasMap& theirs_map, const AtlasFamily& theirs_family,
	                    const Vector<AtlasMergeResolution>& resolutions, const String& actor, const String& at,
	                    AtlasMap& merged_map, AtlasFamily& merged_family, Vector<AtlasMergeConflict>& conflicts,
	                    String& error)
{
	conflicts.Clear();
	error.Clear();
	Index<String> paths;
	for(const AtlasMergeResolution& resolution : resolutions) {
		if(resolution.path.IsEmpty() || (resolution.choice != "ours" && resolution.choice != "theirs")
		|| paths.Find(resolution.path) >= 0) {
			error = "merge resolutions need unique paths and choices of ours or theirs";
			return false;
		}
		paths.Add(resolution.path);
	}
	AtlasMergeContext context{ resolutions, conflicts, Vector<String>(), actor, at, String() };
	Value map = MergeSnapshot(base.base_map, ours_map, theirs_map, "map", context);
	Value family = MergeSnapshot(base.base_family, ours_family, theirs_family, "family", context);
	if(!context.error.IsEmpty()) {
		error = context.error;
		return false;
	}
	if(context.used_choices.GetCount() != resolutions.GetCount()) {
		error = "a merge resolution does not match a current conflict path";
		return false;
	}
	return LoadMergeSnapshot(map, merged_map, error) && LoadMergeSnapshot(family, merged_family, error);
}

END_UPP_NAMESPACE
