#include "ManagerSkylark.h"
#include "ReviewPackage.h"

NAMESPACE_UPP

static bool IsReviewPackageRecord(const DataDocument& doc)
{
	return ReviewPackage::ValidateStoredRecord(doc);
}

static bool IsReviewRecordVisible(const ManagerSkylarkApp& app, const DataDocument& doc)
{
	String source = MapGetString(doc.data, "source_identity");
	return source.IsEmpty() || app.IsAssessmentSourceVisible(doc.workspace_id, source);
}

static bool IsReviewRelatedVisible(const ManagerSkylarkApp& app, const DataDocument& doc, const String& workspace)
{
	if(doc.workspace_id != workspace || !doc.valid || !doc.validated ||
	   doc.kind == "review-package-revision" || doc.kind == "review-thread" || doc.kind == "review-comment")
		return false;
	String visibility = ToLower(doc.visibility);
	String classification = ToLower(MapGetString(doc.data, "classification"));
	Value envelope_value = MapGet(doc.data, "envelope");
	String envelope_visibility;
	if(envelope_value.Is<ValueMap>()) envelope_visibility = ToLower(MapGetString((ValueMap)envelope_value, "visibility"));
	if((!visibility.IsEmpty() && visibility != "public" && visibility != "visible") ||
	   (!classification.IsEmpty() && classification != "public") ||
	   (visibility.IsEmpty() && !envelope_visibility.IsEmpty() && envelope_visibility != "public" && envelope_visibility != "visible"))
		return false;
	return IsReviewRecordVisible(app, doc);
}

static ValueMap MakeReviewLink(const String& workspace, const DataDocument& doc, const String& label)
{
	ValueMap link;
	String title = doc.title.IsEmpty() ? MapGetString(doc.data, "name") : doc.title;
	if(title.IsEmpty()) title = MapGetString(doc.data, "title");
	if(title.IsEmpty()) title = doc.kind;
	link.Add("label", label);
	link.Add("type", doc.kind);
	link.Add("title", title);
	link.Add("href", "/api/reviews/related?workspace=" + UrlEncode(workspace) +
	                    "&kind=" + UrlEncode(doc.kind) + "&ref=" + UrlEncode(doc.id));
	return link;
}

static void AddReviewReference(const ManagerSkylarkApp& app, const String& workspace, const String& id,
	const String& expected_kind, const String& label, ValueArray& links, bool& unavailable)
{
	if(id.IsEmpty()) return;
	const DataDocument *match = NULL;
	int count = 0;
	for(const DataDocument& doc : app.GetDataStore().GetDocuments())
		if(doc.workspace_id == workspace && doc.id == id && (expected_kind.IsEmpty() || doc.kind == expected_kind)) { match = &doc; count++; }
	if(count != 1 || !match || !IsReviewRelatedVisible(app, *match, workspace)) {
		unavailable = true;
		return;
	}
	links.Add(MakeReviewLink(workspace, *match, label));
}

static ValueMap MakeReviewSummary(const ManagerSkylarkApp& app, const DataDocument& doc)
{
	ValueMap summary;
	summary.Add("workspace_id", doc.workspace_id);
	summary.Add("package_id", MapGetString(doc.data, "package_id"));
	summary.Add("revision_id", doc.id);
	summary.Add("base_state", MapGetString(doc.data, "base_state"));
	summary.Add("base_revision", MapGetString(doc.data, "base_revision"));
	summary.Add("head_revision", MapGetString(doc.data, "head_revision"));
	summary.Add("author_state", MapGetString(doc.data, "author_state"));
	summary.Add("capture_state", MapGetString(doc.data, "capture_state"));
	ValueArray related;
	bool unavailable = false;
	AddReviewReference(app, doc.workspace_id, MapGetString(doc.data, "goal_id"), "goal", "Goal", related, unavailable);
	AddReviewReference(app, doc.workspace_id, MapGetString(doc.data, "scope_id"), "topology", "Topology", related, unavailable);
	summary.Add("related_records", related);
	if(unavailable) summary.Add("related_unavailable", true);
	summary.Add("detail_url", "/review-package?workspace=" + UrlEncode(doc.workspace_id) +
	                           "&package=" + UrlEncode(MapGetString(doc.data, "package_id")) + "&revision=" + UrlEncode(doc.id));
	return summary;
}

static ValueMap BuildReviewDetail(const ManagerSkylarkApp& app, const String& workspace, const String& package_id,
	const String& revision_id, bool include_patch)
{
	ValueMap result;
	const DataDocument *package = NULL;
	int matches = 0;
	for(const DataDocument& doc : app.GetDataStore().GetDocuments())
		if(IsReviewPackageRecord(doc) && doc.workspace_id == workspace && MapGetString(doc.data, "package_id") == package_id &&
		   (revision_id.IsEmpty() || doc.id == revision_id) && IsReviewRecordVisible(app, doc)) { package = &doc; matches++; }
	if(matches != 1 || !package) { result.Add("status", "unavailable"); return result; }
	result.Add("status", "visible");
	ValueMap summary = MakeReviewSummary(app, *package);
	result.Add("package", summary);
	ValueArray related = MapGet(summary, "related_records");
	bool unavailable = MapGet(summary, "related_unavailable") == true;
	for(const char *field : { "artifact_refs", "check_refs" })
		for(const Value& ref : ValueArray(MapGet(package->data, field)))
			if(IsString(ref)) AddReviewReference(app, workspace, (String)ref, String(), "Related record", related, unavailable);

	ValueArray discussion;
	Index<String> visible_threads;
	for(const DataDocument& doc : app.GetDataStore().GetDocuments())
		if(doc.kind == "review-thread" && doc.workspace_id == workspace && MapGetString(doc.data, "package_id") == package_id &&
		   MapGetString(doc.data, "revision_id") == package->id && doc.valid && doc.validated && IsReviewRecordVisible(app, doc))
			visible_threads.FindAdd(doc.id);
	for(const DataDocument& doc : app.GetDataStore().GetDocuments()) {
		if(doc.kind != "review-comment" || doc.workspace_id != workspace || MapGetString(doc.data, "package_id") != package_id ||
		   MapGetString(doc.data, "revision_id") != package->id || visible_threads.Find(MapGetString(doc.data, "thread_id")) < 0)
			continue;
		if(!doc.valid || !doc.validated || !IsReviewRecordVisible(app, doc)) { unavailable = true; continue; }
		String author_kind = MapGetString(doc.data, "author_kind");
		String label = author_kind == "human" ? "Human comment" : author_kind == "agent" ? "Agent note" :
		               author_kind == "external" ? "External evidence" : String();
		if(label.IsEmpty()) { unavailable = true; continue; }
		ValueMap item;
		item.Add("type", label);
		item.Add("stale", MapGetString(doc.data, "revision_hash") != MapGetString(package->data, "patch_sha256"));
		ValueArray comment_links;
		bool comment_unavailable = false;
		for(const char *field : { "artifact_refs", "check_refs" })
			for(const Value& ref : ValueArray(MapGet(doc.data, field)))
				if(IsString(ref)) AddReviewReference(app, workspace, (String)ref, String(), "Related evidence", comment_links, comment_unavailable);
		item.Add("related_records", comment_links);
		if(comment_unavailable) unavailable = true;
		discussion.Add(item);
	}
	if(unavailable) { ValueMap marker; marker.Add("label", "Related record unavailable"); related.Add(marker); }
	result.Set("related_records", related);
	result.Add("discussion", discussion);
	if(include_patch) {
		String patch = MapGetString(package->data, "patch_bytes");
		if(patch.GetCount() <= ReviewPackage::MAX_PATCH_BYTES) result.Add("patch_text", patch);
	}
	return result;
}

SKYLARK(ApiReviewPackages, "api/reviews/packages")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ValueArray packages;
	for(const DataDocument& doc : app.GetDataStore().GetDocuments())
		if(IsReviewPackageRecord(doc) && IsReviewRecordVisible(app, doc)) packages.Add(MakeReviewSummary(app, doc));
	ValueMap result; result.Add("status", "ok"); result.Add("packages", packages);
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ApiReviewPackage, "api/reviews/package")
{
	String workspace = http["workspace"], package_id = http["package"], revision_id = http["revision"];
	if(workspace.IsEmpty() || package_id.IsEmpty() || revision_id.IsEmpty()) {
		http.ContentType("application/json; charset=utf-8") << "{\"status\":\"unavailable\"}";
		http.Response(404, "Review Package Unavailable");
		return;
	}
	ValueMap result = BuildReviewDetail(ManagerSkylarkApp::Get(), workspace, package_id, revision_id, false);
	if(MapGetString(result, "status") != "visible") http.Response(404, "Review Package Unavailable");
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ApiReviewRelated, "api/reviews/related")
{
	String workspace = http["workspace"], kind = http["kind"], ref = http["ref"];
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	const DataDocument *match = NULL; int count = 0;
	for(const DataDocument& doc : app.GetDataStore().GetDocuments())
		if(doc.workspace_id == workspace && doc.kind == kind && doc.id == ref) { match = &doc; count++; }
	if(count != 1 || !match || !IsReviewRelatedVisible(app, *match, workspace)) {
		http.ContentType("application/json; charset=utf-8") << "{\"status\":\"unavailable\"}";
		http.Response(404, "Related Record Unavailable");
		return;
	}
	ValueMap result;
	result.Add("status", "visible"); result.Add("id", match->id); result.Add("kind", match->kind);
	result.Add("title", match->title); result.Add("status_value", match->status); result.Add("revision", match->revision);
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ReviewIndexPage, "reviews")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	String body = "<section class=\"card\"><h1>Review Packages</h1><p>Immutable, read-only snapshots. Package capture, comment writes, and requests are disabled.</p><ul>";
	for(const DataDocument& doc : app.GetDataStore().GetDocuments()) if(IsReviewPackageRecord(doc) && IsReviewRecordVisible(app, doc)) {
		ValueMap summary = MakeReviewSummary(app, doc);
		body << "<li><a href=\"" << EscapeHtml(MapGetString(summary, "detail_url")) << "\">"
		     << EscapeHtml(MapGetString(summary, "package_id")) << " @ " << EscapeHtml(MapGetString(summary, "revision_id"))
		     << "</a> (" << EscapeHtml(MapGetString(summary, "capture_state")) << ")</li>";
	}
	body << "</ul></section>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Review Packages", "reviews", body, &http);
}

SKYLARK(ReviewDetailPage, "review-package")
{
	String workspace = http["workspace"], package_id = http["package"], revision_id = http["revision"];
	ValueMap view = BuildReviewDetail(ManagerSkylarkApp::Get(), workspace, package_id, revision_id, true);
	if(MapGetString(view, "status") != "visible") {
		http.Response(404, "Review Package Unavailable");
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Review unavailable", "reviews", "<p>Review package unavailable.</p>", &http);
		return;
	}
	ValueMap package = MapGet(view, "package");
	String body = "<section class=\"card\"><h1>Review Package " + EscapeHtml(MapGetString(package, "package_id")) + "</h1>"
	              "<p>Revision " + EscapeHtml(MapGetString(package, "revision_id")) + "; base " + EscapeHtml(MapGetString(package, "base_revision")) +
	              "; head " + EscapeHtml(MapGetString(package, "head_revision")) + "; capture " + EscapeHtml(MapGetString(package, "capture_state")) + "</p><h2>Related records</h2><ul>";
	for(const Value& value : ValueArray(MapGet(view, "related_records"))) if(value.Is<ValueMap>()) {
		ValueMap item = value;
		if(!MapGetString(item, "href").IsEmpty()) body << "<li><a href=\"" << EscapeHtml(MapGetString(item, "href")) << "\">" << EscapeHtml(MapGetString(item, "label")) << ": " << EscapeHtml(MapGetString(item, "title")) << "</a></li>";
		else body << "<li>" << EscapeHtml(MapGetString(item, "label")) << "</li>";
	}
	body << "</ul><h2>Discussion</h2><ul>";
	for(const Value& value : ValueArray(MapGet(view, "discussion"))) if(value.Is<ValueMap>()) {
		ValueMap item = value;
		body << "<li>" << EscapeHtml(MapGetString(item, "type")) << (MapGet(item, "stale") == true ? " (stale revision)" : "") << "</li>";
	}
	body << "</ul><h2>Patch</h2><pre class=\"review-patch\">" << EscapeHtml(MapGetString(view, "patch_text")) << "</pre></section>";
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Review Package", "reviews", body, &http);
}

END_UPP_NAMESPACE
