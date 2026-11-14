#ifndef _Company_CompanyViews_h_
#define _Company_CompanyViews_h_

#include <Core/Core.h>
#include <WebEngine/WebEngine.h>
#include <WebAuth/WebAuth.h>
#include "CompanyStore.h"
#include "CompanyWorkflow.h"
#include "PlatformWorkflow.h"

NAMESPACE_UPP

// The composed site renders text labels only; this stands in for the old
// WebFramework icon set so the page markup keeps its shape.
struct VividIcon {
	static String Get(const String& name, int size) { (void)name; (void)size; return String(); }
};

// HTML-escapes text for safe interpolation into generated markup.
String CompanyHtmlEsc(const String& text);

// Original CompanySkylark stylesheet, inlined into every page.
const char* CompanyCss();

// ---------------------------------------------------------------------------
// CompanyViews: the Company web pages, rendered directly as HTML on a composed
// WebEngine site. Every dynamic string passes CompanyHtmlEsc before it is
// interpolated. Forms carry the engine CSRF field (__wf_csrf__).
// ---------------------------------------------------------------------------
class CompanyViews {
public:
	static void SetStore(CompanyStore* store) { s_store = store; }
	static CompanyStore* GetStore()           { return s_store; }

	static String RenderPage(WebRequest& http, const String& title, const String& active_nav,
	                         const String& body_content, const WebAuthIdentity& identity);

	// ---------- Pages ----------
	static String RenderWelcome(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderDashboard(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderDashboardContent(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderProposals(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderProposalDetail(WebRequest& http, const WebAuthIdentity& identity, const String& proposal_id);
	static String RenderNewProposalForm(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderWorkItems(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderWorkItemDetail(WebRequest& http, const WebAuthIdentity& identity, const String& work_item_id);
	static String RenderAccount(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderAccountSettings(WebRequest& http, const WebAuthIdentity& identity,
	                                    const String& error_msg = "", const String& notice_msg = "");
	static String RenderReviewQueue(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderReviewDetail(WebRequest& http, const WebAuthIdentity& identity, const String& proposal_id);
	static String RenderOperatorReview(WebRequest& http, const WebAuthIdentity& identity, const String& work_item_id);
	static String RenderAdminUsers(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderAdminTokens(WebRequest& http, const WebAuthIdentity& identity, const String& new_raw_token);
	static String RenderAdminPolicy(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderAdminAudit(WebRequest& http, const WebAuthIdentity& identity, const String& filter_action);
	static String RenderAdminRepos(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderAdminPlatform(WebRequest& http, const WebAuthIdentity& identity);
	static String RenderError(WebRequest& http, const WebAuthIdentity& identity, const String& title,
	                          const String& message, int code);

	// ---------- Shared components ----------
	static String RenderStatusBadge(const String& status);
	static String RenderRoleBadge(const String& role);
	static String RenderStatCard(const String& label, const String& value, const String& icon, const String& accent = "");
	static String RenderEmptyState(const String& icon, const String& message);
	static String RenderFormError(const String& message);
	static String RenderCsrfField(WebRequest& http);

	// ---------- Semantic framework spec (agent discoverability) ----------
	static ValueMap BuildFrameworkSpec();

private:
	static CompanyStore* s_store;

	static String RenderTableHead(const Vector<String>& columns);
	static String RenderTableRow(const Vector<String>& cells);
	static String RenderDataTable(const Vector<String>& columns, const Vector<Vector<String>>& rows,
	                              const String& empty_message = "Nothing here yet.");
};

END_UPP_NAMESPACE

#endif
