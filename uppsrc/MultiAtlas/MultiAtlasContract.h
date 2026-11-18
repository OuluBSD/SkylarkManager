#ifndef _MultiAtlas_MultiAtlasContract_h_
#define _MultiAtlas_MultiAtlasContract_h_

#include "MultiAtlasModel.h"

NAMESPACE_UPP

struct MultiAtlasContractRef : Moveable<MultiAtlasContractRef> {
	String raw;
	String track_id;
	String vid_anchor;
	String family_kind;
	String atom_name;
	bool negated = false;
};

// Returns false and explains the first syntax error when lit is malformed.
bool MultiAtlasParseContract(const String& lit, MultiAtlasContractRef& out, String& error);
// Phase 2 registry loading must reject false: this also reserves family names
// and "meta", whose use would make an unanchored namespace ambiguous.
bool MultiAtlasIsValidTrackId(const String& track_id);

struct MultiAtlasTrackAtomRegistry : Moveable<MultiAtlasTrackAtomRegistry> {
	String track_id;
	Vector<String> declared_atoms;
	Vector<String> exported_atoms;
	Vector<String> imported_atoms;

	void Declare(const String& atom, bool explicitly_exported = false);
	void Import(const String& atom);
	bool IsDeclared(const String& atom) const;
	bool IsExported(const String& atom) const;
	bool IsImported(const String& atom) const;
	const Vector<String>& GetDeclared() const { return declared_atoms; }
	const Vector<String>& GetExported() const { return exported_atoms; }
	const Vector<String>& GetImported() const { return imported_atoms; }
};

END_UPP_NAMESPACE

#endif
