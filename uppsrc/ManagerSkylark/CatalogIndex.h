#ifndef _ManagerSkylark_CatalogIndex_h_
#define _ManagerSkylark_CatalogIndex_h_

#include "ManagerSkylark.h"

NAMESPACE_UPP

struct CatalogIndexResult : Moveable<CatalogIndexResult> {
	String status;
	String diagnostic;
	String source_revision;
	int64 generation = 0;
	int count = 0;
};

struct CatalogIndexRow : Moveable<CatalogIndexRow> {
	String source_id, workspace_id, qualified_id, collection, kind, schema_id, schema_version;
	String revision, source_revision, scope, visibility, validation_state, title, status;
};

struct CatalogIndexFilter {
	String source_id, workspace_id, kind, collection, scope;
	bool data_documents_only = false;
};

class CatalogIndexPin {
	int64 generation_id = 0;
	String source_revision;
	String private_database_path;
	CatalogIndexPin(int64 id, const String& revision, const String& path)
		: generation_id(id), source_revision(revision), private_database_path(path) {}
	friend class CatalogIndex;
public:
	CatalogIndexPin() {}
	bool IsValid() const { return generation_id > 0 && !source_revision.IsEmpty() && !private_database_path.IsEmpty(); }
	int64 GetGenerationId() const { return generation_id; }
	String GetSourceRevision() const { return source_revision; }
};

enum CatalogIndexTestFailpoint {
	CATALOG_INDEX_FAIL_NONE,
	CATALOG_INDEX_FAIL_BEFORE_PUBLISH
};

// Explicitly invoked private projection. It never reads source paths or writes canonical records.
class CatalogIndex {
public:
	static CatalogIndexResult Rebuild(const CatalogGeneration& generation);
	static CatalogIndexResult Rebuild(const CatalogGeneration& generation, const String& database_path,
	                                  CatalogIndexTestFailpoint test_failpoint = CATALOG_INDEX_FAIL_NONE);
	static CatalogIndexPin Pin(const CatalogGeneration& generation, String& status);
	static Vector<CatalogIndexRow> Page(const CatalogIndexPin& pin, const CatalogIndexFilter& filter,
	                                   int offset, int limit, String& status);
	static CatalogIndexResult Check(const String& database_path, const String& expected_source_revision);
	static Vector<CatalogIndexRow> Page(const String& database_path, const String& source_revision,
	                                    int offset, int limit, String& status);
};

END_UPP_NAMESPACE

#endif
