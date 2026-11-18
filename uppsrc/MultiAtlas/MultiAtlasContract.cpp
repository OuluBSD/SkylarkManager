#include "MultiAtlasContract.h"

NAMESPACE_UPP

static bool IsFamily(const String& s)
{
	return s == "tested" || s == "artifact" || s == "done" ||
	       s == "realized" || s == "task" || s == "built";
}

static bool IsPublicFamily(const String& s)
{
	return s == "tested" || s == "artifact" || s == "done";
}

static bool IsTokenChar(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
}

static bool IsTrackId(const String& s)
{
	if(s.IsEmpty() || !((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z')))
		return false;
	for(int i = 1; i < s.GetCount(); i++)
		if(!IsTokenChar((byte)s[i]) || s[i] == '.')
			return false;
	return s != "tested" && s != "artifact" && s != "done" &&
	       s != "realized" && s != "task" && s != "built" && s != "meta";
}

bool MultiAtlasIsValidTrackId(const String& track_id) { return IsTrackId(track_id); }

static bool IsAtomValue(const String& s)
{
	if(s.IsEmpty())
		return false;
	for(int i = 0; i < s.GetCount(); i++)
		if(!IsTokenChar((byte)s[i]))
			return false;
	return true;
}

static bool IsAnchor(const String& s)
{
	if(s.IsEmpty())
		return false;
	for(int i = 0; i < s.GetCount(); i++)
		if(!IsTokenChar((byte)s[i]))
			return false;
	return true;
}

static bool ParseLocal(const String& body, MultiAtlasContractRef& result, String& error)
{
	int colon = body.Find(':');
	if(colon <= 0) {
		error = "contract must contain an atom family and value separated by ':'";
		return false;
	}
	String family = body.Left(colon);
	if(!IsFamily(family)) {
		error = "unknown atom family '" + family + "'";
		return false;
	}
	String value = body.Mid(colon + 1);
	// Atlas legacy task contracts are task:<id>:done. Keep the entire suffix
	// as the task atom identity; its final ':done' is part of that old shape.
	if(family == "task" && value.Find(':') >= 0) {
		int tail = value.Find(':');
		String state = value.Mid(tail + 1);
		if(tail <= 0 || (state != "done" && state != "accepted") || value.Find(':', tail + 1) >= 0) {
			error = "legacy task contract must have the form task:<id>:done or task:<id>:accepted";
			return false;
		}
		if(!IsAtomValue(value.Left(tail))) {
			error = "task contract id contains invalid characters";
			return false;
		}
	}
	else if(!IsAtomValue(value)) {
		error = "atom value is empty or contains invalid characters";
		return false;
	}
	result.family_kind = family;
	result.atom_name = body;
	return true;
}

bool MultiAtlasParseContract(const String& lit, MultiAtlasContractRef& out, String& error)
{
	error.Clear();
	MultiAtlasContractRef parsed;
	parsed.raw = lit;
	if(lit.IsEmpty()) {
		error = "contract literal is empty";
		return false;
	}
	String body = lit;
	if(body[0] == '!') {
		parsed.negated = true;
		body = body.Mid(1);
	}
	if(body.IsEmpty() || body[0] == '!') {
		error = "negation must prefix a non-empty contract exactly once";
		return false;
	}

	int colon = body.Find(':');
	if(colon <= 0) {
		error = "contract must contain an atom family and value separated by ':'";
		return false;
	}
	String head = body.Left(colon);
	String suffix = body.Mid(colon + 1);
	// A recognized family first is always local, preserving task:<id>:done.
	if(IsFamily(head)) {
		if(!ParseLocal(body, parsed, error))
			return false;
	}
	else {
		int at = head.Find('@');
		String track = at < 0 ? head : head.Left(at);
		String anchor = at < 0 ? String() : head.Mid(at + 1);
		if(!IsTrackId(track)) {
			error = "invalid track namespace '" + track + "'";
			return false;
		}
		if(at >= 0 && !IsAnchor(anchor)) {
			error = "version anchor after '@' is empty or malformed";
			return false;
		}
		MultiAtlasContractRef atom;
		if(!ParseLocal(suffix, atom, error)) {
			error = "invalid namespaced atom: " + error;
			return false;
		}
		parsed.track_id = track;
		parsed.vid_anchor = anchor;
		parsed.family_kind = atom.family_kind;
		parsed.atom_name = suffix;
	}
	out = pick(parsed);
	return true;
}

static bool Contains(const Vector<String>& items, const String& value)
{
	for(const String& item : items)
		if(item == value)
			return true;
	return false;
}

static void AddUnique(Vector<String>& items, const String& value)
{
	if(!Contains(items, value))
		items.Add(value);
}

void MultiAtlasTrackAtomRegistry::Declare(const String& atom, bool explicitly_exported)
{
	MultiAtlasContractRef parsed;
	String error;
	if(!MultiAtlasParseContract(atom, parsed, error) || !parsed.track_id.IsEmpty())
		return;
	AddUnique(declared_atoms, parsed.atom_name);
	if(explicitly_exported || IsPublicFamily(parsed.family_kind))
		AddUnique(exported_atoms, parsed.atom_name);
}

void MultiAtlasTrackAtomRegistry::Import(const String& atom)
{
	MultiAtlasContractRef parsed;
	String error;
	if(!MultiAtlasParseContract(atom, parsed, error) || parsed.track_id.IsEmpty())
		return;
	String identity = parsed.track_id;
	if(!parsed.vid_anchor.IsEmpty())
		identity << "@" << parsed.vid_anchor;
	identity << ":" << parsed.atom_name;
	AddUnique(imported_atoms, identity);
}

bool MultiAtlasTrackAtomRegistry::IsDeclared(const String& atom) const { return Contains(declared_atoms, atom); }
bool MultiAtlasTrackAtomRegistry::IsExported(const String& atom) const { return Contains(exported_atoms, atom); }
bool MultiAtlasTrackAtomRegistry::IsImported(const String& atom) const { return Contains(imported_atoms, atom); }

END_UPP_NAMESPACE
