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

Vector<String> DataStore::GetKinds() const
{
	Vector<String> res;
	for(int i = 0; i < kind_order.GetCount(); i++)
		res.Add(kind_order[i]);
	Sort(res);
	return res;
}

Vector<String> DataStore::GetCollections() const
{
	Index<String> set;
	for(int i = 0; i < docs.GetCount(); i++)
		set.FindAdd(docs[i].collection);
	Vector<String> res;
	for(int i = 0; i < set.GetCount(); i++)
		res.Add(set[i]);
	Sort(res);
	return res;
}

int DataStore::GetKindCount(const String& kind) const
{
	int n = 0;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].kind == kind)
			n++;
	return n;
}

int DataStore::GetInvalidCount() const
{
	return invalid_count;
}

int DataStore::GetQuarantineCount() const
{
	return quarantine_count;
}

Vector<const DataDocument*> DataStore::GetDocumentsForKind(const String& kind) const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].kind == kind)
			res.Add(&docs[i]);
	return res;
}

Vector<const DataDocument*> DataStore::GetDocumentsForCollection(const String& collection) const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].collection == collection)
			res.Add(&docs[i]);
	return res;
}

Vector<const DataDocument*> DataStore::GetDocumentsForScope(const String& collection, const String& scope) const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].collection == collection && docs[i].scope == scope)
			res.Add(&docs[i]);
	return res;
}

Vector<const DataDocument*> DataStore::GetInvalidDocuments() const
{
	Vector<const DataDocument*> res;
	for(int i = 0; i < docs.GetCount(); i++)
		if(!docs[i].valid)
			res.Add(&docs[i]);
	return res;
}

const DataDocument* DataStore::FindDocument(const String& id) const
{
	const DataDocument* found = NULL;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].qualified_id == id) {
			if(found) return NULL;
			found = &docs[i];
		}
	if(found) return found;
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].id == id) {
			if(found) return NULL;
			found = &docs[i];
		}
	return found;
}

const DataDocument* DataStore::FindDocumentByRelPath(const String& rel) const
{
	for(int i = 0; i < docs.GetCount(); i++)
		if(docs[i].rel_path == rel)
			return &docs[i];
	return NULL;
}

const SchemaCard* DataStore::FindSchemaCard(const String& kind) const
{
	for(int i = 0; i < schemas.GetCount(); i++)
		if(schemas[i].kind == kind)
			return &schemas[i];
	return NULL;
}

END_UPP_NAMESPACE
