#include "DataStoreInternal.h"
#include <stdio.h>
#include <errno.h>
#ifdef PLATFORM_POSIX
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <dirent.h>
#endif

NAMESPACE_UPP

static bool IsJsonString(const Value& v) { return IsString(v); }
static bool IsJsonBoolean(const Value& v) { return v.Is<bool>(); }
static bool IsJsonInteger(const Value& v)
{
	if(v.Is<int>() || v.Is<int64>()) return true;
	if(!v.Is<double>()) return false;
	double number = v;
	return number >= -9223372036854775808.0 && number < 9223372036854775808.0 && (double)(int64)number == number;
}
static bool IsJsonObject(const Value& v) { return v.Is<ValueMap>(); }
static bool IsJsonArray(const Value& v) { return v.Is<ValueArray>(); }
static bool IsJsonNull(const Value& v) { return !v.Is<ValueMap>() && !v.Is<ValueArray>() && IsNull(v); }

static void ValidateObjectFields(const ValueMap& object, const Vector<SchemaCard::Field>& fields,
	                             const String& path, Vector<String>& diagnostics, int depth,
	                             bool allow_additional_properties = true);

static void ValidateFieldValue(const Value& value, const SchemaCard::Field& field, const String& path,
	                           Vector<String>& diagnostics, int depth)
{
	if(depth > 32) { diagnostics.Add("nested_depth_limit:" + path); return; }
	bool type_ok = field.type == "string" ? IsJsonString(value) :
	               field.type == "object" ? IsJsonObject(value) :
	               field.type == "array" ? IsJsonArray(value) :
	               (field.type == "boolean" || field.type == "bool") ? IsJsonBoolean(value) :
	               field.type == "integer" ? IsJsonInteger(value) : false;
	if(!type_ok) { diagnostics.Add("type:" + path); return; }
	if(field.type == "string" && field.max_utf8_bytes >= 0 && AsString(value).GetCount() > field.max_utf8_bytes)
		diagnostics.Add("max_bytes:" + path);
	if(field.type == "integer") {
		int64 number = value.Is<int>() ? (int64)(int)value : value.Is<int64>() ? (int64)value : (int64)(double)value;
		if(number < field.min_integer || number > field.max_integer)
			diagnostics.Add("integer_range:" + path);
	}
	if(field.enum_values.GetCount()) {
		String text = value;
		bool found = false;
		for(const String& allowed : field.enum_values) if(text == allowed) found = true;
		if(!found) diagnostics.Add("enum:" + path);
	}
	if(field.type == "object" && !field.properties.IsEmpty()) {
		ValueMap nested = value;
		ValidateObjectFields(nested, field.properties, path, diagnostics, depth + 1, field.allow_additional_properties);
	}
	if(field.type == "array" && !field.item_type.IsEmpty()) {
		ValueArray items = value;
		if(field.max_items >= 0 && items.GetCount() > field.max_items)
			diagnostics.Add("max_items:" + path);
		for(int i = 0; i < items.GetCount(); i++) {
			String item_path = path + "[" + AsString(i) + "]";
			bool item_type_ok = field.item_type == "object" ? items[i].Is<ValueMap>() :
			                    field.item_type == "array" ? items[i].Is<ValueArray>() :
			                    field.item_type == "string" ? IsString(items[i]) :
							field.item_type == "boolean" ? IsJsonBoolean(items[i]) :
							field.item_type == "integer" ? IsJsonInteger(items[i]) : false;
			if(!item_type_ok) { diagnostics.Add("nested_type:" + path); continue; }
			if(field.item_type == "string" && field.item_max_utf8_bytes >= 0 && AsString(items[i]).GetCount() > field.item_max_utf8_bytes)
				diagnostics.Add("item_max_bytes:" + path);
			if(field.item_type == "integer") {
				int64 number = items[i].Is<int>() ? (int64)(int)items[i] : items[i].Is<int64>() ? (int64)items[i] : (int64)(double)items[i];
				if(number < field.min_integer || number > field.max_integer)
					diagnostics.Add("item_integer_range:" + path);
			}
			if(field.item_type == "object" && !field.item_properties.IsEmpty()) {
				ValueMap item = items[i];
				ValidateObjectFields(item, field.item_properties, item_path, diagnostics, depth + 1);
			}
		}
	}
}

static void ValidateObjectFields(const ValueMap& object, const Vector<SchemaCard::Field>& fields,
	                             const String& path, Vector<String>& diagnostics, int depth,
	                             bool allow_additional_properties)
{
	if(!allow_additional_properties) {
		for(int i = 0; i < object.GetCount(); i++) {
			String key = object.GetKey(i);
			bool known = false;
			for(const SchemaCard::Field& field : fields) if(field.name == key) { known = true; break; }
			if(!known) diagnostics.Add("additional_property:" + path);
		}
	}
	for(const SchemaCard::Field& field : fields) {
		Value value;
		String field_path = path + "." + field.name;
		if(!GetTypedField(object, field.name, value)) {
			if(field.required) diagnostics.Add("required:" + field_path);
			continue;
		}
		ValidateFieldValue(value, field, field_path, diagnostics, depth + 1);
	}
}

void DataStore::ValidateDocument(DataDocument& d, const SchemaCard* schema) const
{
	ValueMap& m = d.data;
	auto diagnostic = [&](const String& text) {
		d.diagnostics.Add(text);
		if(d.invalid_reason.IsEmpty()) d.invalid_reason = text;
	};
	ValueMap envelope;
	Value ev = MapGet(m, "envelope");
	if(!ev.Is<ValueMap>()) {
		d.validated = false; d.validation_state = "quarantined"; diagnostic("envelope_must_be_object"); return;
	}
	envelope = ev;
	String workspace_id = MapGetString(envelope, "workspace_id");
	String stable_id = MapGetString(envelope, "stable_id");
	d.schema_id = MapGetString(envelope, "schema_id");
	d.schema_version = MapGetString(envelope, "schema_version");
	d.revision = MapGetString(envelope, "revision");
	String workspace_pattern_error;
	bool workspace_id_valid = MatchIdPattern(workspace_id, "^[A-Za-z0-9][A-Za-z0-9._/-]*$", workspace_pattern_error);
	d.workspace_id = workspace_id;
	if(IsString(MapGet(envelope, "workspace_id")) && IsString(MapGet(envelope, "stable_id")) &&
	   workspace_id_valid && !stable_id.IsEmpty() && stable_id == d.id)
		d.qualified_id = workspace_id + ":" + stable_id;
	if(!d.diagnostics.IsEmpty()) {
		d.validation_state = "quarantined";
		d.validated = false;
		return;
	}
	if(!schema) {
		d.validation_state = "quarantined";
		diagnostic("unsupported_kind");
		return;
	}
	if(d.schema_id != schema->schema_id || d.schema_version != schema->schema_version) {
		d.validation_state = "quarantined";
		diagnostic(d.schema_id.IsEmpty() || d.schema_version.IsEmpty() ? "legacy_envelope_missing" : "unsupported_schema_version");
		return;
	}
	d.validated = true;
	d.validation_state = "invalid";
	if(!workspace_id_valid) { diagnostic("workspace_id_pattern_mismatch"); return; }
	if(!IsJsonString(MapGet(envelope, "schema_id")) ||
	   !IsJsonString(MapGet(envelope, "workspace_id")) ||
	   !IsJsonString(MapGet(envelope, "stable_id")) || stable_id.IsEmpty() || stable_id != d.id ||
	   !IsJsonString(MapGet(envelope, "record_kind")) || MapGetString(envelope, "record_kind") != d.kind ||
	   !IsJsonString(MapGet(envelope, "revision")) || MapGetString(envelope, "revision").IsEmpty() ||
	   !IsJsonString(MapGet(envelope, "visibility")) || MapGetString(envelope, "visibility").IsEmpty() ||
	   !IsJsonString(MapGet(envelope, "provenance_ref")) || MapGetString(envelope, "provenance_ref").IsEmpty()) {
		diagnostic("invalid_envelope"); return;
	}
	String visibility = MapGetString(envelope, "visibility");
	if(visibility != "public" && visibility != "private" && visibility != "restricted") {
		diagnostic("enum:envelope.visibility"); return;
	}
	for(int i = 0; i < schema->required.GetCount(); i++) {
		const String& name = schema->required[i];
		Value value;
		if(!GetTypedField(m, name, value) || IsJsonNull(value) || value.IsVoid() || (IsString(value) && AsString(value).IsEmpty()))
			diagnostic("required:" + name);
	}
	for(const SchemaCard::Field& field : schema->fields) {
		Value value;
		if(GetTypedField(m, field.name, value))
			ValidateFieldValue(value, field, field.name, d.diagnostics, 0);
	}
	if(d.id.IsEmpty() || !IsString(MapGet(m, "id"))) diagnostic("id_must_be_nonempty_string");
	String pattern_diagnostic;
	if(!MatchIdPattern(d.id, schema->id_pattern, pattern_diagnostic)) diagnostic(pattern_diagnostic);
	if(d.diagnostics.IsEmpty()) { d.valid = true; d.validation_state = "valid"; }
	else { d.valid = false; invalid_count++; }
}

bool DataStore::MatchIdPattern(const String& id, const String& pattern, String& diagnostic)
{
	diagnostic.Clear();
	int pos = 0;
	if(pattern.GetCount() < 8 || pattern[pos++] != '^') { diagnostic = "unsupported_id_pattern"; return false; }
	auto parse_class = [&](bool allowed[128]) -> bool {
		if(pos >= pattern.GetCount() || pattern[pos++] != '[') return false;
		bool any = false;
		while(pos < pattern.GetCount() && pattern[pos] != ']') {
			unsigned char first = pattern[pos++];
			if(first >= 128 || first == '\\') return false;
			if(pos + 1 < pattern.GetCount() && pattern[pos] == '-' && pattern[pos + 1] != ']') {
				pos++;
				unsigned char last = pattern[pos++];
				if(last >= 128 || last < first || last == '\\') return false;
				for(int c = first; c <= last; c++) allowed[c] = true;
			}
			else allowed[first] = true;
			any = true;
		}
		if(pos >= pattern.GetCount() || pattern[pos++] != ']') return false;
		return any;
	};
	bool first[128] = {};
	bool rest[128] = {};
	if(!parse_class(first) || !parse_class(rest) || pos >= pattern.GetCount()) { diagnostic = "unsupported_id_pattern"; return false; }
	char quantifier = pattern[pos++];
	if((quantifier != '*' && quantifier != '+') || pos + 1 != pattern.GetCount() || pattern[pos] != '$') { diagnostic = "unsupported_id_pattern"; return false; }
	if(id.IsEmpty() || !first[(byte)id[0]]) { diagnostic = "id_pattern_mismatch"; return false; }
	if(quantifier == '+' && id.GetCount() < 2) { diagnostic = "id_pattern_mismatch"; return false; }
	for(int i = 1; i < id.GetCount(); i++) {
		byte c = id[i];
		if(c >= 128 || !rest[c]) { diagnostic = "id_pattern_mismatch"; return false; }
	}
	return true;
}

END_UPP_NAMESPACE
