#include "WebAppHost.h"
#include <WebEngine/WebEngine.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// Blob routes of one site: POST blobs/<id> uploads the request body, GET blobs/<id> returns the bytes.
// Upload parameters: Content-Type header, width, height, checksum (optional declared SHA-256) and
// revision (optional; empty means create-only). The handlers are stateless: each request builds a
// BlobStore over the site's own data root, and the store's file lock serializes the writes.

static bool HasAnyRole(WebRequest& r, const Vector<String>& roles)
{
	if(!r.HasUser())
		return false;
	for(const String& role : roles)
		if(FindIndex(r.User().roles, role) >= 0)
			return true;
	return false;
}

static ValueMap ErrorBody(const String& error)
{
	ValueMap e;
	e.Add("ok", false);
	e.Add("error", error);
	return e;
}

static void BlobUpload(WebRequest& r, String collection, int64 max_bytes, String write_roles)
{
	r.SetHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	if(!HasAnyRole(r, Split(write_roles, ','))) {
		r.Json(ErrorBody("upload role required"), r.HasUser() ? 403 : 401);
		return;
	}
	String id = r.PathArg(0);
	String body = r.Body();
	if(body.GetCount() > max_bytes) {
		r.Json(ErrorBody("blob exceeds the upload limit"), 413);
		return;
	}
	String content_type = r.Header("content-type");
	int width = r.Param("width").IsEmpty() ? 0 : r.IntParam("width");
	int height = r.Param("height").IsEmpty() ? 0 : r.IntParam("height");
	String revision = r.Param("revision");
	String expected = revision.IsEmpty() ? String(RecordAbsent()) : revision;

	BlobStore store(r.Site().id, r.Site().data_root, collection, max_bytes);
	BlobRecord rec;
	StoreResult result = store.Put(id, body, content_type, width, height, r.Param("checksum"), expected, rec);
	if(!result) {
		int code = result.conflict ? 409 : result.error.Find("upload limit") >= 0 ? 413
		         : result.error.Find("checksum mismatch") >= 0 ? 422 : 400;
		r.Json(ErrorBody(result.error), code);
		return;
	}
	ValueMap out;
	out.Add("ok", true);
	out.Add("id", rec.id);
	out.Add("checksum", rec.checksum);
	out.Add("byte_size", (int)rec.byte_size);
	out.Add("width", rec.width);
	out.Add("height", rec.height);
	out.Add("content_type", rec.content_type);
	r.Json(out, 201);
}

// Uploads are client bytes under a client type. Only these types are rendered inline; any other stored
// type (text/html, image/svg+xml, ...) would run as markup on the site origin, so it is served as an
// attachment of octet-stream instead.
static bool InlineSafeBlobType(const String& content_type)
{
	static const char *safe[] = { "image/png", "image/jpeg", "image/gif", "image/webp" };
	String type = ToLower(content_type);
	for(const char *s : safe)
		if(type == s)
			return true;
	return false;
}

static void BlobRead(WebRequest& r, String collection, int64 max_bytes, String read_roles)
{
	r.SetHeader("Cache-Control", "no-store, no-cache, must-revalidate");
	if(!HasAnyRole(r, Split(read_roles, ','))) {
		r.Json(ErrorBody("read role required"), r.HasUser() ? 403 : 401);
		return;
	}
	BlobStore store(r.Site().id, r.Site().data_root, collection, max_bytes);
	String bytes;
	BlobRecord rec;
	StoreResult result = store.Get(r.PathArg(0), bytes, rec);
	if(!result) {
		r.Json(ErrorBody(result.error), result.error.Find("not found") >= 0 || result.error.Find("missing") >= 0 ? 404 : 400);
		return;
	}
	r.SetHeader("ETag", "\"" + rec.checksum + "\"");
	r.SetHeader("X-Content-Type-Options", "nosniff");
	r.SetHeader("Content-Security-Policy", "default-src 'none'; sandbox");
	if(InlineSafeBlobType(rec.content_type))
		r.Plain(bytes, ~ToLower(rec.content_type));
	else {
		r.SetHeader("Content-Disposition", "attachment");
		r.Plain(bytes, "application/octet-stream");
	}
}

void InstallBlobRoutes(EngineSite& site, const String& collection, int64 max_bytes,
                       const String& write_roles, const String& read_roles)
{
	// Role lists are comma-separated; an empty list admits nobody.
	site.Router().Add("POST", "blobs/*", callback3(BlobUpload, collection, max_bytes, write_roles));
	site.Router().Add("GET", "blobs/*", callback3(BlobRead, collection, max_bytes, read_roles));
}

END_UPP_NAMESPACE
