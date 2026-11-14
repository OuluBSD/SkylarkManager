#include "CompanyViews.h"

NAMESPACE_UPP

// Company pages on a composed WebEngine site. The shell is plain HTML with the
// original stylesheet; the engine supplies the session chrome and theme.

CompanyStore* CompanyViews::s_store = NULL;

String CompanyHtmlEsc(const String& text)
{
	StringBuffer out;
	for(int i = 0; i < text.GetCount(); i++) {
		char c = text[i];
		switch(c) {
		case '&': out << "&amp;"; break;
		case '<': out << "&lt;"; break;
		case '>': out << "&gt;"; break;
		case '"': out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default: out.Cat(c);
		}
	}
	return String(out);
}

String CompanyViews::RenderCsrfField(WebRequest& http)
{
	return "<input type=\"hidden\" name=\"__wf_csrf__\" value=\"" + CompanyHtmlEsc(http.CsrfToken()) + "\">";
}

String CompanyViews::RenderPage(WebRequest& http, const String& title, const String& active_nav,
                                const String& body_content, const WebAuthIdentity& identity)
{
	(void)active_nav;
	StringBuffer nav;
	nav << "<a href=\"/\">Overview</a>";
	if(identity.authenticated) {
		nav << "<a href=\"/dashboard\">Dashboard</a>"
		    << "<a href=\"/proposals\">Proposals</a>"
		    << "<a href=\"/work\">Work</a>"
		    << "<a href=\"/account\">Account</a>"
		    << "<a href=\"/account/settings\">Settings</a>";
		if(identity.HasRole(COMPANY_ROLE_REVIEWER) || identity.IsAdmin())
			nav << "<a href=\"/review/queue\">Reviews</a>";
		if(identity.IsAdmin())
			nav << "<a href=\"/admin/users\">Administration</a>";
	}
	else
		nav << "<a href=\"/login\">Sign in</a>";

	StringBuffer b;
	b << "<!doctype html><html lang=\"en\"" << ThemeHtmlAttrs(http) << "><head><meta charset=\"utf-8\">"
	  << ThemeHeadScript()
	  << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
	  << "<title>" << CompanyHtmlEsc(title) << " - CompanySkylark</title>"
	  << "<style>\n" << CompanyCss() << "\n</style></head><body>"
	  << "<header class=\"cs-nav\"><a href=\"/\" class=\"cs-brand\">CompanySkylark</a>"
	  << "<nav>" << String(nav) << "</nav>" << SessionChromeHtml(http) << "</header>"
	  << "<main class=\"cs-main\">" << body_content << "</main>"
	  << "<footer class=\"cs-footer\">CompanySkylark &bull; proposals, review, and compensation for human and AI contributors</footer>"
	  << "</body></html>";
	return String(b);
}

String CompanyViews::RenderAccountSettings(WebRequest& http, const WebAuthIdentity& identity,
                                           const String& error_msg, const String& notice_msg)
{
	String roles;
	for(int i = 0; i < identity.roles.GetCount(); i++) {
		if(i > 0)
			roles << ", ";
		roles << identity.roles[i];
	}
	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Settings</div>\n"
	     << "<dl class=\"cs-facts\">"
	     << "<dt>Username</dt><dd>" << CompanyHtmlEsc(identity.username) << "</dd>"
	     << "<dt>Display name</dt><dd>" << CompanyHtmlEsc(identity.display_name) << "</dd>"
	     << "<dt>Actor type</dt><dd>" << CompanyHtmlEsc(identity.actor_type) << "</dd>"
	     << "<dt>Roles</dt><dd>" << CompanyHtmlEsc(roles.IsEmpty() ? String("none") : roles) << "</dd>"
	     << "</dl></div>\n";
	if(!error_msg.IsEmpty())
		body << RenderFormError(error_msg);
	if(!notice_msg.IsEmpty())
		body << "<div class=\"cs-alert\">" << CompanyHtmlEsc(notice_msg) << "</div>\n";
	if(identity.IsHuman())
		body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Change Password</div>\n"
		     << "<form method=\"POST\" action=\"/account/settings/password\" accept-charset=\"utf-8\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<div class=\"cs-form-row\"><label>Current password</label>"
		     << "<input type=\"password\" name=\"current_password\" required class=\"cs-input\"></div>\n"
		     << "<div class=\"cs-form-row\"><label>New password (at least 8 characters)</label>"
		     << "<input type=\"password\" name=\"new_password\" required minlength=\"8\" class=\"cs-input\"></div>\n"
		     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Update Password</button>\n"
		     << "</form></div>\n";
	return RenderPage(http, "Settings", "account_settings", body, identity);
}

String CompanyViews::RenderStatusBadge(const String& status)
{
	String color = "neutral";
	if(status == COMPANY_PROPOSAL_ACCEPTED || status == COMPANY_STATE_COMPLETED ||
	   status == COMPANY_STATE_COMPENSATED || status == COMPANY_REVIEW_PASSED ||
	   status == COMPANY_REVIEW_APPROVED)
		color = "emerald";
	else if(status == COMPANY_PROPOSAL_REJECTED || status == COMPANY_STATE_REJECTED ||
	        status == COMPANY_STATE_BLOCKED || status == COMPANY_REVIEW_FAILED ||
	        status == COMPANY_REVIEW_REJECTED)
		color = "red";
	else if(status == COMPANY_PROPOSAL_CHANGES || status == COMPANY_STATE_CHANGES ||
	        status == COMPANY_REVIEW_REQUESTED)
		color = "amber";
	else if(status == COMPANY_PROPOSAL_PROPOSED || status == COMPANY_STATE_MECHANICAL ||
	        status == COMPANY_STATE_OPERATOR)
		color = "cyan";
	else if(status == COMPANY_STATE_IMPLEMENTATION)
		color = "violet";
	return "<span class=\"cs-badge cs-badge-" + color + "\">" + CompanyHtmlEsc(status) + "</span>";
}

String CompanyViews::RenderRoleBadge(const String& role)
{
	String color = role == COMPANY_ROLE_ADMIN ? "red" :
	               role == COMPANY_ROLE_REVIEWER ? "violet" :
	               role == COMPANY_ROLE_AUTOMATION ? "amber" : "cyan";
	return "<span class=\"cs-badge cs-badge-" + color + "\">" + CompanyHtmlEsc(role) + "</span>";
}

String CompanyViews::RenderStatCard(const String& label, const String& value, const String& icon, const String& accent)
{
	String h;
	h << "<div class=\"cs-stat\">"
	  << "<div style=\"display:flex; justify-content:space-between; align-items:center;\">"
	  << "<span class=\"cs-stat-label\">" << CompanyHtmlEsc(label) << "</span>"
	  << VividIcon::Get(icon, 18)
	  << "</div>"
	  << "<span class=\"cs-stat-value" << (accent.IsEmpty() ? String() : String(" cs-") + accent) << "\">"
	  << CompanyHtmlEsc(value) << "</span>"
	  << "</div>";
	return h;
}

String CompanyViews::RenderEmptyState(const String& icon, const String& message)
{
	return "<div style=\"text-align:center; padding:2.5rem 1rem; color:var(--wf-fg-inactive,#8b919d);\">"
	       + VividIcon::Get(icon, 28) + "<p>" + CompanyHtmlEsc(message) + "</p></div>";
}

String CompanyViews::RenderFormError(const String& message)
{
	return "<div class=\"cs-alert cs-alert-danger\">" + CompanyHtmlEsc(message) + "</div>";
}

const char* CompanyCss()
{
	return
	":root{--cs-accent:#00a1c5;--cs-accent-soft:rgba(0,161,197,0.12);--cs-emerald:#10b981;"
	"--cs-amber:#f59e0b;--cs-red:#ef4444;--cs-violet:#8b5cf6}\n"
	"main.wf-page-body{max-width:var(--wf-container-width,1500px);margin:0 auto}\n"
	".cs-card{background:var(--wf-bg-view,#151d2b);border:1px solid var(--wf-border-color,rgba(0,161,197,0.2));"
	"border-radius:var(--wf-radius,8px);padding:var(--wf-pad-card,1.25rem);margin-bottom:var(--wf-gap-grid,1.25rem);"
	"box-shadow:var(--wf-shadow)}\n"
	".cs-card-header{font-size:1.1rem;font-weight:700;margin-bottom:0.85rem;color:var(--wf-fg-normal,#d3dae3);"
	"border-bottom:1px solid var(--wf-border-color,rgba(0,161,197,0.2));padding-bottom:0.5rem;"
	"display:flex;justify-content:space-between;align-items:center;gap:0.75rem}\n"
	".cs-card-header .cs-subtle{font-size:0.82rem;font-weight:500;color:var(--wf-fg-inactive,#8b919d)}\n"
	".cs-grid{display:grid;gap:var(--wf-gap-grid,1.25rem)}\n"
	".cs-grid-2{grid-template-columns:1fr 1fr}.cs-grid-3{grid-template-columns:repeat(3,1fr)}\n"
	".cs-grid-4{grid-template-columns:repeat(auto-fit,minmax(240px,1fr))}\n"
	"@media (max-width:900px){.cs-grid-2,.cs-grid-3{grid-template-columns:1fr}}\n"
	".cs-stat{background:var(--wf-bg-view,#151d2b);border:1px solid var(--wf-border-color,rgba(0,161,197,0.2));"
	"border-radius:var(--wf-radius,8px);padding:var(--wf-pad-card,1.25rem);display:flex;flex-direction:column;"
	"gap:4px;box-shadow:var(--wf-shadow)}\n"
	".cs-stat-value{font-size:2rem;font-weight:800;color:var(--cs-accent,#00a1c5);line-height:1.1}\n"
	".cs-stat-value.cs-emerald{color:var(--cs-emerald)}.cs-stat-value.cs-amber{color:var(--cs-amber)}\n"
	".cs-stat-value.cs-violet{color:var(--cs-violet)}\n"
	".cs-stat-label{font-size:0.8rem;color:var(--wf-fg-inactive,#8b919d);text-transform:uppercase;"
	"letter-spacing:0.05em;font-weight:600}\n"
	".cs-table{width:100%;border-collapse:collapse;margin-top:0.5rem}\n"
	".cs-table th{text-align:left;padding:10px 12px;background:var(--wf-bg-alt,#1c202f);"
	"color:var(--wf-fg-inactive,#8b919d);font-weight:600;font-size:0.8rem;"
	"border-bottom:1px solid var(--wf-border-color,rgba(0,161,197,0.2));text-transform:uppercase;"
	"letter-spacing:0.04em}\n"
	".cs-table td{padding:10px 12px;border-bottom:1px solid var(--wf-border-color,rgba(0,161,197,0.2));"
	"font-size:0.9rem;color:var(--wf-fg-normal,#d3dae3);vertical-align:top}\n"
	".cs-table tr:hover td{background:var(--wf-surface-hover,rgba(0,161,197,0.08))}\n"
	".cs-table a{color:var(--cs-accent);text-decoration:none}\n"
	".cs-table a:hover{text-decoration:underline}\n"
	".cs-badge{display:inline-block;font-size:0.74rem;font-weight:600;padding:2px 9px;border-radius:999px;"
	"letter-spacing:0.02em;white-space:nowrap}\n"
	".cs-badge-neutral{background:rgba(139,145,157,0.18);color:#aeb6c2}\n"
	".cs-badge-cyan{background:var(--cs-accent-soft);color:var(--cs-accent)}\n"
	".cs-badge-emerald{background:rgba(16,185,129,0.15);color:var(--cs-emerald)}\n"
	".cs-badge-amber{background:rgba(245,158,11,0.15);color:var(--cs-amber)}\n"
	".cs-badge-red{background:rgba(239,68,68,0.15);color:var(--cs-red)}\n"
	".cs-badge-violet{background:rgba(139,92,246,0.15);color:var(--cs-violet)}\n"
	".cs-btn{display:inline-block;padding:7px 16px;border-radius:var(--wf-radius,8px);font-weight:600;"
	"font-size:0.88rem;text-decoration:none;border:1px solid transparent;cursor:pointer;"
	"transition:all 0.15s ease}\n"
	".cs-btn-primary{background:var(--cs-accent);color:#fff}\n"
	".cs-btn-primary:hover{filter:brightness(1.1)}\n"
	".cs-btn-secondary{background:transparent;color:var(--cs-accent);border-color:var(--cs-accent)}\n"
	".cs-btn-secondary:hover{background:var(--cs-accent-soft)}\n"
	".cs-btn-danger{background:transparent;color:var(--cs-red);border-color:var(--cs-red)}\n"
	".cs-btn-danger:hover{background:rgba(239,68,68,0.12)}\n"
	".cs-btn-small{padding:4px 10px;font-size:0.78rem}\n"
	".cs-form-row{margin-bottom:1rem}\n"
	".cs-form-row label{display:block;font-size:0.82rem;font-weight:600;margin-bottom:4px;"
	"color:var(--wf-fg-normal,#d3dae3)}\n"
	".cs-input,.cs-textarea,.cs-select{width:100%;box-sizing:border-box;background:var(--wf-bg-alt,#1c202f);"
	"border:1px solid var(--wf-border-color,rgba(0,161,197,0.2));border-radius:var(--wf-radius,8px);"
	"color:var(--wf-fg-normal,#d3dae3);padding:9px 12px;font-size:0.92rem;font-family:inherit}\n"
	".cs-textarea{min-height:110px;resize:vertical}\n"
	".cs-input:focus,.cs-textarea:focus,.cs-select:focus{outline:none;border-color:var(--cs-accent);"
	"box-shadow:0 0 0 2px var(--cs-accent-soft)}\n"
	".cs-form-actions{display:flex;gap:0.75rem;margin-top:1.25rem;align-items:center}\n"
	".cs-alert{border-radius:var(--wf-radius,8px);padding:12px 16px;font-size:0.9rem;margin-bottom:1rem;"
	"border:1px solid transparent}\n"
	".cs-alert-danger{background:rgba(239,68,68,0.1);border-color:rgba(239,68,68,0.4);color:#fca5a5}\n"
	".cs-alert-success{background:rgba(16,185,129,0.1);border-color:rgba(16,185,129,0.4);color:#6ee7b7}\n"
	".cs-alert-info{background:var(--cs-accent-soft);border-color:rgba(0,161,197,0.4);color:#7dd3fc}\n"
	".cs-login-card{max-width:430px;margin:3rem auto}\n"
	".cs-login-logo{color:var(--cs-accent)}\n"
	".cs-timeline{list-style:none;margin:0.75rem 0 0;padding:0}\n"
	".cs-timeline li{position:relative;padding:0 0 1rem 1.6rem;"
	"border-left:2px solid var(--wf-border-color,rgba(0,161,197,0.2));margin-left:6px}\n"
	".cs-timeline li:last-child{padding-bottom:0}\n"
	".cs-timeline li::before{content:\"\";position:absolute;left:-7px;top:2px;width:12px;height:12px;"
	"border-radius:50%;background:var(--cs-accent);border:2px solid var(--wf-bg-view,#151d2b)}\n"
	".cs-timeline .cs-timeline-meta{font-size:0.78rem;color:var(--wf-fg-inactive,#8b919d)}\n"
	".cs-code{background:var(--wf-bg-alt,#1c202f);color:var(--cs-accent);padding:1rem;"
	"border-radius:var(--wf-radius,8px);font-family:ui-monospace,monospace;font-size:0.84rem;"
	"overflow-x:auto;border:1px solid var(--wf-border-color,rgba(0,161,197,0.2));"
	"white-space:pre-wrap;word-break:break-word}\n"
	".cs-check-list{list-style:none;padding:0;margin:0.5rem 0}\n"
	".cs-check-list li{display:flex;align-items:center;gap:8px;padding:4px 0;font-size:0.9rem}\n"
	".cs-check-list .cs-ok{color:var(--cs-emerald)}.cs-check-list .cs-bad{color:var(--cs-red)}\n"
	".cs-muted{color:var(--wf-fg-inactive,#8b919d);font-size:0.85rem}\n"
	".cs-mono{font-family:ui-monospace,monospace;font-size:0.85em}\n"
	".cs-right{text-align:right}\n"
	".cs-actions-cell{display:flex;gap:6px;flex-wrap:wrap}\n";
}

// ---------------------------------------------------------------------------
// Shared components
// ---------------------------------------------------------------------------

String CompanyViews::RenderTableHead(const Vector<String>& columns)
{
	String h = "<table class=\"cs-table\"><thead><tr>";
	for(int i = 0; i < columns.GetCount(); i++)
		h << "<th>" << CompanyHtmlEsc(columns[i]) << "</th>";
	h << "</tr></thead><tbody>";
	return h;
}

String CompanyViews::RenderTableRow(const Vector<String>& cells)
{
	String h = "<tr>";
	for(int i = 0; i < cells.GetCount(); i++)
		h << "<td>" << cells[i] << "</td>";
	h << "</tr>";
	return h;
}

String CompanyViews::RenderDataTable(const Vector<String>& columns, const Vector<Vector<String>>& rows,
                                     const String& empty_message)
{
	if(rows.IsEmpty())
		return RenderEmptyState("inbox", empty_message);
	String h = RenderTableHead(columns);
	for(int i = 0; i < rows.GetCount(); i++)
		h << RenderTableRow(rows[i]);
	h << "</tbody></table>";
	return h;
}

// ---------------------------------------------------------------------------
// Semantic framework spec (agent discoverability)
// ---------------------------------------------------------------------------

ValueMap CompanyViews::BuildFrameworkSpec()
{
	ValueMap spec;
	spec.Set("framework", "CompanySkylark");
	spec.Set("theme", "vivid-dark");
	spec.Set("template", "sidebar");

	ValueMap nav;
	nav.Set("overview", "/");
	nav.Set("proposals", "/proposals");
	nav.Set("work_items", "/work");
	nav.Set("reviews", "/review/queue");
	nav.Set("account", "/account");
	nav.Set("account_settings", "/account/settings");
	nav.Set("admin", "/admin/users");
	spec.Set("navigation", nav);

	ValueArray states;
	Vector<String> ws = CompanyWorkflow::WorkStates();
	for(int i = 0; i < ws.GetCount(); i++)
		states.Add(ws[i]);
	spec.Set("work_item_states", states);

	ValueArray actions;
	actions.Add("POST /api/proposals (proposals:write)");
	actions.Add("POST /api/work/bind (work:write)");
	actions.Add("POST /api/work/mechanical/request (work:write)");
	actions.Add("POST /api/work/operator/request (work:write)");
	actions.Add("POST /api/reviews/operator/decide (reviews:write, human reviewer)");
	actions.Add("POST /api/webhook/review-callback (callback:write, automation)");
	actions.Add("POST /api/ledger/correction (human admin)");
	spec.Set("actions", actions);

	ValueMap rules;
	rules.Set("automation_cannot", "grant roles, accept proposals, approve operator review, or issue compensation");
	rules.Set("operator_review", "human reviewer only, never the item contributor");
	rules.Set("compensation", "one credit per completed work item, idempotency keyed per work item");
	spec.Set("rules", rules);
	return spec;
}




// ---------------------------------------------------------------------------
// Welcome / login / dashboard
// ---------------------------------------------------------------------------

String CompanyViews::RenderWelcome(WebRequest& http, const WebAuthIdentity& identity)
{
	String body;
	if(!identity.authenticated) {
		body << "<div class=\"cs-card\" style=\"text-align:center; padding:3rem 2rem;\">\n"
		     << "<div class=\"cs-login-logo\">" << VividIcon::Get("shield", 40) << "</div>\n"
		     << "<h1 style=\"margin:0.75rem 0 0.5rem;\">Welcome to CompanySkylark</h1>\n"
		     << "<p class=\"cs-muted\" style=\"max-width:560px; margin:0 auto 1.25rem;\">"
		     << "A private collaboration platform where human contributors and AI agents propose work, "
		     << "implement it against GitHub pull requests, pass mechanical and Operator Review, and earn "
		     << "virtual compensation recorded on an append-only ledger.</p>\n"
		     << "<div class=\"cs-grid cs-grid-3\" style=\"text-align:left; margin:1.5rem 0;\">\n"
		     << "<div class=\"cs-card\"><div class=\"cs-card-header\">" << VividIcon::Get("file-plus", 16) << " Propose</div>"
		     << "<span class=\"cs-muted\">Contributors and automation agents submit proposals scoped to allowlisted repositories.</span></div>\n"
		     << "<div class=\"cs-card\"><div class=\"cs-card-header\">" << VividIcon::Get("git-pull-request", 16) << " Implement</div>"
		     << "<span class=\"cs-muted\">Accepted proposals become work items bound to pull requests with mechanical checks.</span></div>\n"
		     << "<div class=\"cs-card\"><div class=\"cs-card-header\">" << VividIcon::Get("check-square", 16) << " Review &amp; Earn</div>"
		     << "<span class=\"cs-muted\">Human operators approve evidence-backed work; compensation is credited idempotently.</span></div>\n"
		     << "</div>\n"
		     << "<a href=\"/login\" class=\"cs-btn cs-btn-primary\" style=\"padding:10px 28px;\">Sign in to continue</a>\n"
		     << "</div>\n";
		return RenderPage(http, "Welcome", "home", body, identity);
	}

	// Authenticated: role-aware welcome.
	String greeting = "Welcome Back, " + identity.display_name;
	body << "<h1 style=\"margin:0.5rem 0 1rem;\">" << CompanyHtmlEsc(greeting) << "</h1>\n";
	body << RenderDashboardContent(http, identity);
	return RenderPage(http, greeting, "home", body, identity);
}

String CompanyViews::RenderDashboardContent(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	String body;
	if(!store) {
		body << RenderFormError("Store is not initialized.");
		return body;
	}

	Vector<ProposalRecord> proposals = store->GetProposals();
	Vector<WorkItemRecord> items = store->GetWorkItems();
	int open_proposals = 0, in_flight = 0, awaiting_operator = 0;
	for(int i = 0; i < proposals.GetCount(); i++)
		if(proposals[i].status == COMPANY_PROPOSAL_PROPOSED)
			open_proposals++;
	for(int i = 0; i < items.GetCount(); i++) {
		if(items[i].state == COMPANY_STATE_IMPLEMENTATION || items[i].state == COMPANY_STATE_MECHANICAL ||
		   items[i].state == COMPANY_STATE_CHANGES)
			in_flight++;
		if(items[i].state == COMPANY_STATE_OPERATOR)
			awaiting_operator++;
	}

	body << "<div class=\"cs-grid cs-grid-4\">\n"
	     << RenderStatCard("Open Proposals", AsString(open_proposals), "file-plus")
	     << RenderStatCard("In Flight", AsString(in_flight), "git-pull-request")
	     << RenderStatCard("Awaiting Operator", AsString(awaiting_operator), "user-check", "amber")
	     << RenderStatCard("My Balance", AsString(store->LedgerBalance(identity.user_id)) + " CR", "wallet", "emerald")
	     << "</div>\n";

	// Role-aware quick actions.
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Quick Actions"
	     << "<span class=\"cs-subtle\">" << CompanyHtmlEsc(identity.actor_type) << " &middot; "
	     << (identity.roles.IsEmpty() ? String("scoped") : Join(identity.roles, ", ")) << "</span></div>\n"
	     << "<div class=\"cs-actions-cell\">\n";
	if(identity.HasScope(COMPANY_SCOPE_PROPOSALS_WRITE))
		body << "<a href=\"/proposals/new\" class=\"cs-btn cs-btn-primary cs-btn-small\">"
		     << VividIcon::Get("file-plus", 14) << " New Proposal</a>\n";
	body << "<a href=\"/work\" class=\"cs-btn cs-btn-secondary cs-btn-small\">"
	     << VividIcon::Get("git-pull-request", 14) << " My Work</a>\n"
	     << "<a href=\"/account\" class=\"cs-btn cs-btn-secondary cs-btn-small\">"
	     << VividIcon::Get("wallet", 14) << " Account</a>\n";
	if(identity.HasRole(COMPANY_ROLE_REVIEWER) || identity.IsAdmin())
		body << "<a href=\"/review/queue\" class=\"cs-btn cs-btn-secondary cs-btn-small\">"
		     << VividIcon::Get("check-square", 14) << " Review Queue</a>\n";
	if(identity.IsAdmin())
		body << "<a href=\"/admin/users\" class=\"cs-btn cs-btn-secondary cs-btn-small\">"
		     << VividIcon::Get("shield", 14) << " Administration</a>\n";
	body << "</div></div>\n";

	// Recent work items table.
	Vector<Vector<String>> rows;
	for(int i = items.GetCount() - 1; i >= 0 && rows.GetCount() < 8; i--) {
		const WorkItemRecord& w = items[i];
		Vector<String> row;
		row.Add("<a href=\"/work/detail/" + w.id + "\" class=\"cs-mono\">" + CompanyHtmlEsc(w.id) + "</a>");
		ProposalRecord p;
		row.Add(store->FindProposal(w.proposal_id, p) ? CompanyHtmlEsc(p.title) : String("&mdash;"));
		row.Add(RenderStatusBadge(w.state));
		row.Add(w.pr_number > 0 ? ("#" + AsString(w.pr_number)) : String("&mdash;"));
		rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Recent Work Items"
	     << "<a href=\"/work\" class=\"cs-subtle\">View all</a></div>\n"
	     << RenderDataTable(Vector<String>{"Work Item", "Proposal", "State", "PR"}, rows,
	                        "No work items yet.") << "</div>\n";
	return body;
}

String CompanyViews::RenderDashboard(WebRequest& http, const WebAuthIdentity& identity)
{
	String body = RenderDashboardContent(http, identity);
	return RenderPage(http, "Dashboard", "home", body, identity);
}

// ---------------------------------------------------------------------------
// Proposals
// ---------------------------------------------------------------------------

String CompanyViews::RenderProposals(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	String body;
	if(!store)
		return RenderPage(http, "Proposals", "proposals", RenderFormError("Store is not initialized."), identity);

	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Proposals"
	     << (identity.HasScope(COMPANY_SCOPE_PROPOSALS_WRITE)
	         ? String("<a href=\"/proposals/new\" class=\"cs-btn cs-btn-primary cs-btn-small\">") + VividIcon::Get("file-plus", 14) + " New Proposal</a>"
	         : String(""))
	     << "</div>\n";

	Vector<Vector<String>> rows;
	Vector<ProposalRecord> proposals = store->GetProposals();
	for(int i = 0; i < proposals.GetCount(); i++) {
		const ProposalRecord& p = proposals[i];
		Vector<String> row;
		row.Add("<a href=\"/proposals/detail/" + p.id + "\" class=\"cs-mono\">" + CompanyHtmlEsc(p.id) + "</a>");
		row.Add("<a href=\"/proposals/detail/" + p.id + "\">" + CompanyHtmlEsc(p.title) + "</a>");
		UserRecord author;
		row.Add(store->FindUser(p.author_user_id, author) ? CompanyHtmlEsc(author.display_name) : String("<span class=\"cs-badge cs-badge-amber\">agent</span>"));
		row.Add(CompanyFormatNumber(p.estimated_hours) + " h");
		row.Add(RenderStatusBadge(p.status));
		rows.Add(pick(row));
	}
	body << RenderDataTable(Vector<String>{"ID", "Title", "Author", "Est. Hours", "Status"}, rows,
	                        "No proposals yet.") << "</div>\n";
	return RenderPage(http, "Proposals", "proposals", body, identity);
}

String CompanyViews::RenderNewProposalForm(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "New Proposal", "proposals", RenderFormError("Store is not initialized."), identity);

	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">New Proposal"
	     << "<span class=\"cs-subtle\">Repository must be on the admin allowlist</span></div>\n"
	     << "<form method=\"POST\" action=\"/proposals/create\" accept-charset=\"utf-8\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-form-row\"><label>Title</label>"
	     << "<input type=\"text\" name=\"title\" required maxlength=\"200\" class=\"cs-input\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Description</label>"
	     << "<textarea name=\"description\" class=\"cs-textarea\" placeholder=\"What should be done and why\"></textarea></div>\n"
	     << "<div class=\"cs-form-row\"><label>Repository (owner/repo)</label>"
	     << "<input type=\"text\" name=\"repository\" required class=\"cs-input\" placeholder=\"OuluBSD/CompanySkylark\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Estimated workload (hours)</label>"
	     << "<input type=\"number\" name=\"estimated_hours\" min=\"1\" max=\"40\" step=\"1\" value=\"4\" class=\"cs-input\" required>"
	     << "<div class=\"cs-muted\">Whole hours. Compensation is paid up to this budget: estimated hours &times; your hourly rate.</div></div>\n"
	     << "<div class=\"cs-form-actions\">"
	     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Submit Proposal</button>"
	     << "<a href=\"/proposals\" class=\"cs-btn cs-btn-secondary\">Cancel</a>"
	     << "</div>\n"
	     << "</form></div>\n";
	return RenderPage(http, "New Proposal", "proposals", body, identity);
}

String CompanyViews::RenderProposalDetail(WebRequest& http, const WebAuthIdentity& identity, const String& proposal_id)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Proposal", "proposals", RenderFormError("Store is not initialized."), identity);

	ProposalRecord p;
	if(!store->FindProposal(proposal_id, p))
		return RenderError(http, identity, "Not Found", "No proposal with id " + proposal_id, 404);

	String body;
	UserRecord author;
	String author_name = store->FindUser(p.author_user_id, author) ? author.display_name : String("automation agent");
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">" << CompanyHtmlEsc(p.title)
	     << RenderStatusBadge(p.status) << "</div>\n"
	     << "<p class=\"cs-muted\">Proposal <span class=\"cs-mono\">" << CompanyHtmlEsc(p.id)
	     << "</span> by " << CompanyHtmlEsc(author_name)
	     << (p.author_type == COMPANY_ACTOR_AGENT ? " (automation token)" : "")
	     << " &middot; repository <span class=\"cs-mono\">" << CompanyHtmlEsc(p.repository) << "</span>"
	     << (p.issue_number > 0 ? " &middot; issue #" + AsString(p.issue_number) : String())
	     << " &middot; estimated workload " << CompanyFormatNumber(p.estimated_hours) << " h" << "</p>\n"
	     << "<p>" << CompanyHtmlEsc(p.description) << "</p>\n";

	// Reviewer decision history.
	if(!p.reviewer_decision.IsEmpty()) {
		body << "<div class=\"cs-code\">reviewer decision: " << CompanyHtmlEsc((String)p.reviewer_decision.Get("action", ""))
		     << " by " << CompanyHtmlEsc((String)p.reviewer_decision.Get("actor_user_id", ""))
		     << " &mdash; " << CompanyHtmlEsc((String)p.reviewer_decision.Get("reason", ""))
		     << " (" << CompanyHtmlEsc((String)p.reviewer_decision.Get("at", "")) << ")</div>\n";
	}

	// Work item link.
	Vector<WorkItemRecord> items = store->GetWorkItems();
	for(int i = 0; i < items.GetCount(); i++)
		if(items[i].proposal_id == p.id)
			body << "<p><a href=\"/work/detail/" << items[i].id << "\" class=\"cs-btn cs-btn-secondary cs-btn-small\">"
			     << VividIcon::Get("git-pull-request", 14) << " Work item " << items[i].id << "</a></p>\n";

	// Author revise action when changes were requested.
	bool is_author = (!p.author_user_id.IsEmpty() && p.author_user_id == identity.user_id) ||
	                 (!p.author_token_id.IsEmpty() && p.author_token_id == identity.token_id);
	if(is_author && p.status == COMPANY_PROPOSAL_CHANGES) {
		body << "<form method=\"POST\" action=\"/proposals/revise\" accept-charset=\"utf-8\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<input type=\"hidden\" name=\"proposal_id\" value=\"" << CompanyHtmlEsc(p.id) << "\">\n"
		     << "<div class=\"cs-form-row\"><label>Revised description</label>"
		     << "<textarea name=\"description\" class=\"cs-textarea\">" << CompanyHtmlEsc(p.description) << "</textarea></div>\n"
		     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Submit Revision</button>\n"
		     << "</form>\n";
	}

	// Reviewer decision form (human reviewers; not while the proposal is closed).
	if(identity.IsHuman() && identity.HasRole(COMPANY_ROLE_REVIEWER) &&
	   (p.status == COMPANY_PROPOSAL_PROPOSED || p.status == COMPANY_PROPOSAL_CHANGES)) {
		body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Reviewer Decision</div>\n"
		     << "<form method=\"POST\" action=\"/review/decide\" accept-charset=\"utf-8\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<input type=\"hidden\" name=\"proposal_id\" value=\"" << CompanyHtmlEsc(p.id) << "\">\n"
		     << "<div class=\"cs-form-row\"><label>Reason (required for reject / changes)</label>"
		     << "<input type=\"text\" name=\"reason\" class=\"cs-input\"></div>\n"
		     << "<div class=\"cs-form-row\"><label>Assign contributor (accept only; defaults to author)</label>";
		if(p.author_type == COMPANY_ACTOR_AGENT) {
			body << "<select name=\"assign_contributor\" class=\"cs-select\">";
			Vector<UserRecord> users = store->GetUsers();
			for(int i = 0; i < users.GetCount(); i++)
				if(users[i].enabled && users[i].HasHumanRole())
					body << "<option value=\"" << CompanyHtmlEsc(users[i].id) << "\">"
					     << CompanyHtmlEsc(users[i].display_name) << "</option>";
			body << "</select>";
		}
		else
			body << "<input type=\"hidden\" name=\"assign_contributor\" value=\"" << CompanyHtmlEsc(p.author_user_id) << "\">"
			     << "<span class=\"cs-muted\">author: " << CompanyHtmlEsc(author_name) << "</span>";
		body << "</div>\n<div class=\"cs-form-actions\">\n"
		     << "<button type=\"submit\" name=\"action\" value=\"accept\" class=\"cs-btn cs-btn-primary\">Accept</button>\n"
		     << "<button type=\"submit\" name=\"action\" value=\"changes\" class=\"cs-btn cs-btn-secondary\">Request Changes</button>\n"
		     << "<button type=\"submit\" name=\"action\" value=\"reject\" class=\"cs-btn cs-btn-danger\">Reject</button>\n"
		     << "</div></form></div>\n";
	}

	body << "</div>\n";
	return RenderPage(http, "Proposal " + p.id, "proposal_detail:" + p.id, body, identity);
}

// ---------------------------------------------------------------------------
// Work items
// ---------------------------------------------------------------------------

String CompanyViews::RenderWorkItems(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	String body;
	if(!store)
		return RenderPage(http, "Work Items", "work", RenderFormError("Store is not initialized."), identity);

	Vector<Vector<String>> rows;
	Vector<WorkItemRecord> items = store->GetWorkItems();
	for(int i = 0; i < items.GetCount(); i++) {
		const WorkItemRecord& w = items[i];
		Vector<String> row;
		row.Add("<a href=\"/work/detail/" + w.id + "\" class=\"cs-mono\">" + CompanyHtmlEsc(w.id) + "</a>");
		row.Add(RenderStatusBadge(w.state));
		row.Add(w.pr_number > 0 ? ("<a href=\"" + CompanyHtmlEsc(w.pr_url) + "\">#" + AsString(w.pr_number) + "</a>") : String("&mdash;"));
		UserRecord c;
		row.Add(store->FindUser(w.contributor_user_id, c) ? CompanyHtmlEsc(c.display_name) :
		        (!w.contributor_token_id.IsEmpty() ? String("agent") : String("&mdash;")));
		rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Work Items</div>\n"
	     << RenderDataTable(Vector<String>{"ID", "State", "Pull Request", "Contributor"}, rows,
	                        "No work items yet.") << "</div>\n";
	return RenderPage(http, "Work Items", "work", body, identity);
}

String CompanyViews::RenderWorkItemDetail(WebRequest& http, const WebAuthIdentity& identity, const String& work_item_id)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Work Item", "work", RenderFormError("Store is not initialized."), identity);

	WorkItemRecord w;
	if(!store->FindWorkItem(work_item_id, w))
		return RenderError(http, identity, "Not Found", "No work item with id " + work_item_id, 404);

	ProposalRecord p;
	store->FindProposal(w.proposal_id, p);
	bool is_contributor = (!w.contributor_user_id.IsEmpty() && w.contributor_user_id == identity.user_id) ||
	                      (!w.contributor_token_id.IsEmpty() && w.contributor_token_id == identity.token_id);
	bool can_manage = is_contributor || identity.IsAdmin();

	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Work Item <span class=\"cs-mono\">"
	     << CompanyHtmlEsc(w.id) << "</span>" << RenderStatusBadge(w.state) << "</div>\n"
	     << "<p class=\"cs-muted\">For proposal <a href=\"/proposals/detail/" << CompanyHtmlEsc(w.proposal_id) << "\">"
	     << CompanyHtmlEsc(p.title) << "</a> &middot; repository <span class=\"cs-mono\">"
	     << CompanyHtmlEsc(w.repository) << "</span></p>\n";

	if(w.pr_number > 0)
		body << "<p><span class=\"cs-badge cs-badge-cyan\">PR #" << w.pr_number << "</span> "
		     << "<a href=\"" << CompanyHtmlEsc(w.pr_url) << "\">" << CompanyHtmlEsc(w.pr_url) << "</a>"
		     << (!w.branch.IsEmpty() ? " &middot; branch <span class=\"cs-mono\">" + CompanyHtmlEsc(w.branch) + "</span>" : String())
		     << "</p>\n";
	else
		body << "<p class=\"cs-muted\">No pull request bound yet.</p>\n";

	// Contributor actions.
	if(can_manage && (w.state == COMPANY_STATE_IMPLEMENTATION || w.state == COMPANY_STATE_CHANGES)) {
		body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Contributor Actions</div>\n"
		     << "<form method=\"POST\" action=\"/work/bind\" accept-charset=\"utf-8\" style=\"margin-bottom:1rem;\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<input type=\"hidden\" name=\"work_item_id\" value=\"" << CompanyHtmlEsc(w.id) << "\">\n"
		     << "<div class=\"cs-grid cs-grid-3\">\n"
		     << "<div class=\"cs-form-row\"><label>PR number</label>"
		     << "<input type=\"number\" name=\"pr_number\" min=\"1\" required class=\"cs-input\" value=\"" << w.pr_number << "\"></div>\n"
		     << "<div class=\"cs-form-row\"><label>PR URL</label>"
		     << "<input type=\"text\" name=\"pr_url\" class=\"cs-input\" value=\"" << CompanyHtmlEsc(w.pr_url) << "\"></div>\n"
		     << "<div class=\"cs-form-row\"><label>Branch</label>"
		     << "<input type=\"text\" name=\"branch\" class=\"cs-input\" value=\"" << CompanyHtmlEsc(w.branch) << "\"></div>\n"
		     << "</div>\n<button type=\"submit\" class=\"cs-btn cs-btn-primary cs-btn-small\">"
		     << VividIcon::Get("git-branch", 14) << " Bind Pull Request</button>\n</form>\n";
	}
	if(can_manage && w.pr_number > 0 &&
	   (w.state == COMPANY_STATE_IMPLEMENTATION || w.state == COMPANY_STATE_CHANGES)) {
		body << "<form method=\"POST\" action=\"/work/request-mechanical\" accept-charset=\"utf-8\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<input type=\"hidden\" name=\"work_item_id\" value=\"" << CompanyHtmlEsc(w.id) << "\">\n"
		     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary cs-btn-small\">"
		     << VividIcon::Get("check-square", 14) << " Request Mechanical Review</button>\n</form>\n";
	}
	if(can_manage && w.state == COMPANY_STATE_OPERATOR) {
		body << "<form method=\"POST\" action=\"/work/request-operator\" accept-charset=\"utf-8\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<input type=\"hidden\" name=\"work_item_id\" value=\"" << CompanyHtmlEsc(w.id) << "\">\n"
		     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary cs-btn-small\">"
		     << VividIcon::Get("user-check", 14) << " Request Operator Review</button>\n</form>\n";
	}
	if(can_manage && w.state == COMPANY_STATE_CHANGES) {
		body << "<form method=\"POST\" action=\"/work/resume\" accept-charset=\"utf-8\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<input type=\"hidden\" name=\"work_item_id\" value=\"" << CompanyHtmlEsc(w.id) << "\">\n"
		     << "<div class=\"cs-form-row\"><label>Note</label><input type=\"text\" name=\"reason\" class=\"cs-input\"></div>\n"
		     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary cs-btn-small\">Resume Work</button>\n</form>\n";
	}

	// Review records timeline with evidence.
	Vector<ReviewRecord> reviews = store->ReviewsForWorkItem(w.id);
	if(reviews.GetCount() > 0) {
		body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Reviews &amp; Evidence</div>\n<ul class=\"cs-timeline\">\n";
		for(int i = 0; i < reviews.GetCount(); i++) {
			const ReviewRecord& r = reviews[i];
			body << "<li><div><span class=\"cs-badge cs-badge-" << (r.kind == "mechanical" ? "cyan" : "violet") << "\">"
			     << CompanyHtmlEsc(r.kind) << "</span> " << RenderStatusBadge(r.status) << "</div>\n"
			     << "<div class=\"cs-timeline-meta\">" << CompanyHtmlEsc(CompanyTimeIso(r.updated_at)) << "</div>\n";
			if(!r.evidence.IsEmpty()) {
				body << "<div class=\"cs-code\">";
				if(r.evidence.Find("run_id") >= 0)
					body << "run: " << CompanyHtmlEsc((String)r.evidence["run_id"]) << "\n";
				if(r.evidence.Find("comment_url") >= 0)
					body << "comment: " << CompanyHtmlEsc((String)r.evidence["comment_url"]) << "\n";
				if(r.evidence.Find("findings") >= 0)
					body << "findings: " << CompanyHtmlEsc((String)r.evidence["findings"]) << "\n";
				if(r.evidence.Find("checks") >= 0 && r.evidence["checks"].Is<ValueArray>()) {
					ValueArray checks = r.evidence["checks"];
					for(int c = 0; c < checks.GetCount(); c++) {
						ValueMap cm = checks[c];
						body << "check: " << CompanyHtmlEsc((String)cm.Get("name", ""))
						     << " = " << CompanyHtmlEsc((String)cm.Get("status", "")) << "\n";
					}
				}
				body << "</div>\n";
			}
			if(r.decision_history.GetCount() > 0) {
				body << "<ul class=\"cs-check-list\">\n";
				for(int d = 0; d < r.decision_history.GetCount(); d++) {
					ValueMap dm = r.decision_history[d];
					body << "<li>" << CompanyHtmlEsc((String)dm.Get("action", ""))
					     << " <span class=\"cs-timeline-meta\">by " << CompanyHtmlEsc((String)dm.Get("actor_user_id", ""))
					     << " (" << CompanyHtmlEsc((String)dm.Get("actor_type", "")) << ") "
					     << CompanyHtmlEsc((String)dm.Get("at", "")) << "</span></li>\n";
				}
				body << "</ul>\n";
			}
			body << "</li>\n";
		}
		body << "</ul></div>\n";
	}

	body << "</div>\n";
	return RenderPage(http, "Work " + w.id, "work_detail:" + w.id, body, identity);
}

// ---------------------------------------------------------------------------
// Account & ledger
// ---------------------------------------------------------------------------

String CompanyViews::RenderAccount(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Account", "account", RenderFormError("Store is not initialized."), identity);

	int balance = store->LedgerBalance(identity.user_id);
	String body;
	body << "<div class=\"cs-grid cs-grid-4\">\n"
	     << RenderStatCard("Balance", AsString(balance) + " CR", "wallet", "emerald")
	     << "</div>\n";

	Vector<Vector<String>> rows;
	Vector<LedgerEntry> entries = store->GetLedger();
	for(int i = entries.GetCount() - 1; i >= 0; i--) {
		const LedgerEntry& e = entries[i];
		if(e.user_id != identity.user_id && !identity.IsAdmin())
			continue;
		Vector<String> row;
		row.Add(AsString(e.seq));
		row.Add(e.amount > 0 ? ("<span class=\"cs-badge cs-badge-emerald\">+" + AsString(e.amount) + "</span>")
		                     : ("<span class=\"cs-badge cs-badge-red\">" + AsString(e.amount) + "</span>"));
		row.Add(CompanyHtmlEsc(e.type));
		row.Add(CompanyHtmlEsc(e.reason));
		row.Add(CompanyHtmlEsc(CompanyTimeIso(e.created_at)));
		rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Ledger Entries"
	     << "<span class=\"cs-subtle\">append-only</span></div>\n"
	     << RenderDataTable(Vector<String>{"Seq", "Amount", "Type", "Reason", "Recorded"}, rows,
	                        "No ledger entries yet.") << "</div>\n";
	return RenderPage(http, "Account", "account", body, identity);
}


// ---------------------------------------------------------------------------
// Review pages
// ---------------------------------------------------------------------------

String CompanyViews::RenderReviewQueue(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Reviews", "review", RenderFormError("Store is not initialized."), identity);

	String body;

	// Open proposals.
	Vector<Vector<String>> prop_rows;
	Vector<ProposalRecord> proposals = store->GetProposals();
	for(int i = 0; i < proposals.GetCount(); i++) {
		const ProposalRecord& p = proposals[i];
		if(p.status != COMPANY_PROPOSAL_PROPOSED && p.status != COMPANY_PROPOSAL_CHANGES)
			continue;
		Vector<String> row;
		row.Add("<a href=\"/review/detail/" + p.id + "\" class=\"cs-mono\">" + CompanyHtmlEsc(p.id) + "</a>");
		row.Add("<a href=\"/review/detail/" + p.id + "\">" + CompanyHtmlEsc(p.title) + "</a>");
		row.Add(CompanyFormatNumber(p.estimated_hours) + " h");
		row.Add(RenderStatusBadge(p.status));
		prop_rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Proposal Queue</div>\n"
	     << RenderDataTable(Vector<String>{"ID", "Title", "Est. Hours", "Status"}, prop_rows,
	                        "The proposal queue is empty.") << "</div>\n";

	// Work items awaiting operator review.
	Vector<Vector<String>> op_rows;
	Vector<WorkItemRecord> items = store->GetWorkItems();
	for(int i = 0; i < items.GetCount(); i++) {
		const WorkItemRecord& w = items[i];
		if(w.state != COMPANY_STATE_OPERATOR)
			continue;
		Vector<String> row;
		row.Add("<a href=\"/review/operator/" + w.id + "\" class=\"cs-mono\">" + CompanyHtmlEsc(w.id) + "</a>");
		ProposalRecord p;
		row.Add(store->FindProposal(w.proposal_id, p) ? CompanyHtmlEsc(p.title) : String("&mdash;"));
		row.Add(w.pr_number > 0 ? ("#" + AsString(w.pr_number)) : String("&mdash;"));
		row.Add(RenderStatusBadge(w.state));
		op_rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Awaiting Operator Review</div>\n"
	     << RenderDataTable(Vector<String>{"Work Item", "Proposal", "PR", "State"}, op_rows,
	                        "Nothing awaiting operator review.") << "</div>\n";
	return RenderPage(http, "Reviews", "review_queue", body, identity);
}

String CompanyViews::RenderReviewDetail(WebRequest& http, const WebAuthIdentity& identity, const String& proposal_id)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Review", "review", RenderFormError("Store is not initialized."), identity);

	ProposalRecord p;
	if(!store->FindProposal(proposal_id, p))
		return RenderError(http, identity, "Not Found", "No proposal with id " + proposal_id, 404);

	String body = RenderProposalDetail(http, identity, proposal_id);
	return body;
}

String CompanyViews::RenderOperatorReview(WebRequest& http, const WebAuthIdentity& identity, const String& work_item_id)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Operator Review", "review", RenderFormError("Store is not initialized."), identity);

	WorkItemRecord w;
	if(!store->FindWorkItem(work_item_id, w))
		return RenderError(http, identity, "Not Found", "No work item with id " + work_item_id, 404);

	ProposalRecord p;
	store->FindProposal(w.proposal_id, p);

	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Operator Review: <span class=\"cs-mono\">"
	     << CompanyHtmlEsc(w.id) << "</span>" << RenderStatusBadge(w.state) << "</div>\n"
	     << "<p class=\"cs-muted\">Proposal <a href=\"/proposals/detail/" << CompanyHtmlEsc(w.proposal_id) << "\">"
	     << CompanyHtmlEsc(p.title) << "</a>"
	     << (w.pr_number > 0 ? " &middot; <a href=\"" + CompanyHtmlEsc(w.pr_url) + "\">PR #" + AsString(w.pr_number) + "</a>" : String())
	     << "</p>\n";

	// Mechanical evidence summary.
	Vector<ReviewRecord> reviews = store->ReviewsForWorkItem(w.id);
	for(int i = 0; i < reviews.GetCount(); i++) {
		const ReviewRecord& r = reviews[i];
		if(r.kind != "mechanical")
			continue;
		body << "<div class=\"cs-code\">mechanical: " << RenderStatusBadge(r.status) << "\n"
		     << "run: " << CompanyHtmlEsc((String)r.evidence.Get("run_id", "n/a")) << "\n"
		     << "findings: " << CompanyHtmlEsc((String)r.evidence.Get("findings", "none")) << "</div>\n";
	}

	// Decision form: human reviewers only, never the contributor.
	bool is_contributor = (!w.contributor_user_id.IsEmpty() && w.contributor_user_id == identity.user_id);
	if(identity.IsHuman() && identity.HasRole(COMPANY_ROLE_REVIEWER)) {
		if(is_contributor)
			body << RenderFormError("You are the contributor of this work item and cannot review it.");
		else if(w.state == COMPANY_STATE_OPERATOR)
			body << "<form method=\"POST\" action=\"/review/operator/decide\" accept-charset=\"utf-8\">\n"
			     << RenderCsrfField(http) << "\n"
			     << "<input type=\"hidden\" name=\"work_item_id\" value=\"" << CompanyHtmlEsc(w.id) << "\">\n"
			     << "<div class=\"cs-form-row\"><label>Reason (required for reject / changes)</label>"
			     << "<input type=\"text\" name=\"reason\" class=\"cs-input\"></div>\n"
			     << "<div class=\"cs-form-actions\">\n"
			     << "<button type=\"submit\" name=\"action\" value=\"approve\" class=\"cs-btn cs-btn-primary\">Approve</button>\n"
			     << "<button type=\"submit\" name=\"action\" value=\"changes\" class=\"cs-btn cs-btn-secondary\">Request Changes</button>\n"
			     << "<button type=\"submit\" name=\"action\" value=\"reject\" class=\"cs-btn cs-btn-danger\">Reject</button>\n"
			     << "</div></form>\n";
		else
			body << "<p class=\"cs-muted\">This work item is not awaiting operator review (state: "
			     << CompanyHtmlEsc(w.state) << ").</p>\n";
	}
	else
		body << "<p class=\"cs-muted\">Operator Review decisions require a human reviewer role.</p>\n";

	// Compensation action for completed items (admin/reviewer humans).
	if(w.state == COMPANY_STATE_COMPLETED && identity.IsHuman() &&
	   (identity.HasRole(COMPANY_ROLE_ADMIN) || identity.HasRole(COMPANY_ROLE_REVIEWER))) {
		body << "<form method=\"POST\" action=\"/admin/compensate\" accept-charset=\"utf-8\" style=\"margin-top:1rem;\">\n"
		     << RenderCsrfField(http) << "\n"
		     << "<input type=\"hidden\" name=\"work_item_id\" value=\"" << CompanyHtmlEsc(w.id) << "\">\n"
		     << "<div class=\"cs-form-row\"><label>Compensation note</label>"
		     << "<input type=\"text\" name=\"reason\" class=\"cs-input\"></div>\n"
		     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">"
		     << VividIcon::Get("award", 14) << " Issue Compensation</button>\n</form>\n";
	}

	body << "</div>\n";
	return RenderPage(http, "Operator Review", "operator_review:" + w.id, body, identity);
}

// ---------------------------------------------------------------------------
// Admin pages
// ---------------------------------------------------------------------------

String CompanyViews::RenderAdminUsers(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Users", "admin", RenderFormError("Store is not initialized."), identity);

	String body;
	Vector<Vector<String>> rows;
	Vector<UserRecord> users = store->GetUsers();
	for(int i = 0; i < users.GetCount(); i++) {
		const UserRecord& u = users[i];
		Vector<String> row;
		row.Add("<span class=\"cs-mono\">" + CompanyHtmlEsc(u.id) + "</span>");
		row.Add(CompanyHtmlEsc(u.username));
		row.Add(CompanyHtmlEsc(u.display_name));
		String roles;
		for(int r = 0; r < u.roles.GetCount(); r++)
			roles << RenderRoleBadge(u.roles[r]) << " ";
		row.Add(roles);
		row.Add(u.enabled ? "<span class=\"cs-badge cs-badge-emerald\">enabled</span>"
		                  : "<span class=\"cs-badge cs-badge-red\">disabled</span>");
		row.Add(u.hourly_rate > 0 ? CompanyFormatNumber(u.hourly_rate) + " CR/h"
		                          : "<span class=\"cs-muted\">not set</span>");
		rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Users"
	     << "<span class=\"cs-subtle\">role and rate changes are audited</span></div>\n"
	     << RenderDataTable(Vector<String>{"ID", "Username", "Display Name", "Roles", "State", "Hourly Rate"}, rows,
	                        "No users exist; use --bootstrap-admin to create the first one.")
	     << "</div>\n";

	// Role assignment form.
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Set User Roles</div>\n"
	     << "<form method=\"POST\" action=\"/admin/users/roles\" accept-charset=\"utf-8\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-grid cs-grid-2\">\n"
	     << "<div class=\"cs-form-row\"><label>User</label><select name=\"user_id\" class=\"cs-select\">";
	for(int i = 0; i < users.GetCount(); i++)
		body << "<option value=\"" << CompanyHtmlEsc(users[i].id) << "\">"
		     << CompanyHtmlEsc(users[i].username) << " (" << CompanyHtmlEsc(users[i].id) << ")</option>";
	body << "</select></div>\n"
	     << "<div class=\"cs-form-row\"><label>Roles (checkboxes replace the full set)</label>"
	     << "<div style=\"display:flex; flex-wrap:wrap; gap:12px; padding-top:6px;\">";
	Vector<String> all_roles = CompanyAllRoles();
	for(int i = 0; i < all_roles.GetCount(); i++)
		body << "<label style=\"font-weight:500;\"><input type=\"checkbox\" name=\"role_" << all_roles[i]
		     << "\" value=\"1\"> " << all_roles[i] << "</label>";
	body << "</div></div>\n</div>\n"
	     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Save Roles</button>\n"
	     << "</form></div>\n";

	// Hourly rate form (compensation input; admin-managed, audited).
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Set Hourly Rate</div>\n"
	     << "<p class=\"cs-muted\">Compensation is estimated_hours &times; hourly rate, paid up to the proposal budget. "
	     << "Rates are visible to admins only.</p>\n"
	     << "<form method=\"POST\" action=\"/admin/users/rate\" accept-charset=\"utf-8\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-grid cs-grid-2\">\n"
	     << "<div class=\"cs-form-row\"><label>User</label><select name=\"user_id\" class=\"cs-select\">";
	for(int i = 0; i < users.GetCount(); i++)
		body << "<option value=\"" << CompanyHtmlEsc(users[i].id) << "\">"
		     << CompanyHtmlEsc(users[i].username) << "</option>";
	body << "</select></div>\n"
	     << "<div class=\"cs-form-row\"><label>Hourly rate (CR/h, 0 = unset)</label>"
	     << "<input type=\"number\" name=\"hourly_rate\" min=\"0\" max=\"100000\" step=\"0.5\" class=\"cs-input\" required></div>\n"
	     << "</div>\n"
	     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Save Rate</button>\n"
	     << "</form></div>\n";

	// Create user form.
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Create User</div>\n"
	     << "<form method=\"POST\" action=\"/admin/users/create\" accept-charset=\"utf-8\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-grid cs-grid-2\">\n"
	     << "<div class=\"cs-form-row\"><label>Username</label><input type=\"text\" name=\"username\" required class=\"cs-input\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Display name</label><input type=\"text\" name=\"display_name\" class=\"cs-input\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Password (optional for token-only identities)</label><input type=\"password\" name=\"password\" class=\"cs-input\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Roles (comma separated)</label><input type=\"text\" name=\"roles\" required class=\"cs-input\" placeholder=\"contributor,reviewer\"></div>\n"
	     << "</div>\n<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Create User</button>\n"
	     << "</form></div>\n";
	return RenderPage(http, "Users", "admin_users", body, identity);
}

String CompanyViews::RenderAdminTokens(WebRequest& http, const WebAuthIdentity& identity, const String& new_raw_token)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Agent Tokens", "admin", RenderFormError("Store is not initialized."), identity);

	String body;
	if(!new_raw_token.IsEmpty())
		body << "<div class=\"cs-alert cs-alert-success\">Agent token created. Copy it now; it is shown once: "
		     << "<span class=\"cs-mono\">" << CompanyHtmlEsc(new_raw_token) << "</span></div>\n";

	Vector<Vector<String>> rows;
	Vector<AgentTokenRecord> tokens = store->GetTokens();
	for(int i = 0; i < tokens.GetCount(); i++) {
		const AgentTokenRecord& t = tokens[i];
		Vector<String> row;
		row.Add("<span class=\"cs-mono\">" + CompanyHtmlEsc(t.id) + "</span>");
		row.Add(CompanyHtmlEsc(t.name));
		String scopes;
		for(int s = 0; s < t.scopes.GetCount(); s++)
			scopes << "<span class=\"cs-badge cs-badge-cyan\">" << CompanyHtmlEsc(t.scopes[s]) << "</span> ";
		row.Add(scopes);
		row.Add(IsNull(t.revoked_at) ? "<span class=\"cs-badge cs-badge-emerald\">active</span>"
		                             : "<span class=\"cs-badge cs-badge-red\">revoked</span>");
		row.Add(IsNull(t.expires_at) ? String("never") : CompanyHtmlEsc(CompanyTimeIso(t.expires_at)));
		rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Agent Tokens"
	     << "<span class=\"cs-subtle\">only hashes are stored</span></div>\n"
	     << RenderDataTable(Vector<String>{"ID", "Name", "Scopes", "State", "Expires"}, rows,
	                        "No agent tokens yet.")
	     << "</div>\n";

	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Create Agent Token</div>\n"
	     << "<form method=\"POST\" action=\"/admin/tokens/create\" accept-charset=\"utf-8\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-grid cs-grid-2\">\n"
	     << "<div class=\"cs-form-row\"><label>Name</label><input type=\"text\" name=\"name\" required class=\"cs-input\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Owner user (optional)</label><input type=\"text\" name=\"owner_user_id\" class=\"cs-input\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Scopes (comma separated)</label><input type=\"text\" name=\"scopes\" required class=\"cs-input\" placeholder=\"proposals:read,proposals:write\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Expires (ISO, empty = never)</label><input type=\"text\" name=\"expires_at\" class=\"cs-input\" placeholder=\"2027-01-01 00:00:00\"></div>\n"
	     << "</div>\n<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Create Token</button>\n"
	     << "</form>\n"
	     << "<form method=\"POST\" action=\"/admin/tokens/revoke\" accept-charset=\"utf-8\" style=\"margin-top:1rem;\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-form-row\"><label>Revoke token id</label><input type=\"text\" name=\"token_id\" required class=\"cs-input\"></div>\n"
	     << "<button type=\"submit\" class=\"cs-btn cs-btn-danger\">Revoke</button>\n"
	     << "</form></div>\n";
	return RenderPage(http, "Agent Tokens", "admin_tokens", body, identity);
}

String CompanyViews::RenderAdminPolicy(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Policy", "admin", RenderFormError("Store is not initialized."), identity);

	const CompanyConfig& cfg = store->GetConfig();
	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Compensation &amp; Review Policy</div>\n"
	     << "<p class=\"cs-muted\">Proposals carry an estimated workload in hours; compensation is paid up to that budget "
	     << "at the contributor's hourly rate, capped by Max amount.</p>\n"
	     << "<form method=\"POST\" action=\"/admin/policy/save\" accept-charset=\"utf-8\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-grid cs-grid-2\">\n"
	     << "<div class=\"cs-form-row\"><label>Max hours per proposal</label>"
	     << "<input type=\"number\" name=\"max_hours\" min=\"1\" class=\"cs-input\" value=\""
	     << (cfg.reward_policy.Find("max_hours") >= 0 ? (int)cfg.reward_policy["max_hours"] : 40) << "\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Max amount (CR)</label>"
	     << "<input type=\"number\" name=\"max_amount\" class=\"cs-input\" value=\""
	     << (cfg.reward_policy.Find("max_amount") >= 0 ? (int)cfg.reward_policy["max_amount"] : 5000) << "\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Operator quorum</label>"
	     << "<input type=\"number\" name=\"operator_quorum\" min=\"1\" class=\"cs-input\" value=\""
	     << (cfg.review_policy.Find("operator_quorum") >= 0 ? (int)cfg.review_policy["operator_quorum"] : 1) << "\"></div>\n"
	     << "<div class=\"cs-form-row\"><label>Currency</label>"
	     << "<input type=\"text\" name=\"currency\" class=\"cs-input\" value=\""
	     << CompanyHtmlEsc(cfg.reward_policy.Find("currency") >= 0 ? (String)cfg.reward_policy["currency"] : String("CREDIT")) << "\"></div>\n"
	     << "</div>\n"
	     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Save Policy</button>\n"
	     << "</form></div>\n";
	return RenderPage(http, "Policy", "admin_policy", body, identity);
}

String CompanyViews::RenderAdminAudit(WebRequest& http, const WebAuthIdentity& identity, const String& filter_action)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Audit", "admin", RenderFormError("Store is not initialized."), identity);

	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Audit Log"
	     << "<span class=\"cs-subtle\">append-only event ledger</span></div>\n"
	     << "<form method=\"GET\" action=\"/admin/audit\" style=\"margin-bottom:0.75rem;\">\n"
	     << "<div style=\"display:flex; gap:8px;\">\n"
	     << "<input type=\"text\" name=\"action\" class=\"cs-input\" placeholder=\"Filter by action (e.g. operator.)\" value=\""
	     << CompanyHtmlEsc(filter_action) << "\">\n"
	     << "<button type=\"submit\" class=\"cs-btn cs-btn-secondary cs-btn-small\">Filter</button>\n"
	     << "</div></form>\n";

	Vector<Vector<String>> rows;
	Vector<AuditEvent> events = store->GetAudit();
	for(int i = events.GetCount() - 1; i >= 0 && rows.GetCount() < 200; i--) {
		const AuditEvent& e = events[i];
		if(!filter_action.IsEmpty() && e.action.Find(filter_action) < 0)
			continue;
		Vector<String> row;
		row.Add(AsString(e.seq));
		row.Add("<span class=\"cs-badge cs-badge-neutral\">" + CompanyHtmlEsc(e.action) + "</span>");
		row.Add(CompanyHtmlEsc(e.actor_type));
		row.Add(CompanyHtmlEsc(!e.actor_user_id.IsEmpty() ? e.actor_user_id : e.actor_token_id));
		row.Add("<span class=\"cs-mono\">" + CompanyHtmlEsc(e.target_id) + "</span>");
		row.Add(!e.prior_status.IsEmpty() || !e.new_status.IsEmpty()
		        ? (CompanyHtmlEsc(e.prior_status) + " &rarr; " + CompanyHtmlEsc(e.new_status)) : String("&mdash;"));
		row.Add(CompanyHtmlEsc(e.reason));
		rows.Add(pick(row));
	}
	body << RenderDataTable(Vector<String>{"Seq", "Action", "Actor Type", "Actor", "Target", "Status", "Reason"}, rows,
	                        "No audit events recorded.") << "</div>\n";
	return RenderPage(http, "Audit", "admin_audit", body, identity);
}

String CompanyViews::RenderAdminRepos(WebRequest& http, const WebAuthIdentity& identity)
{
	CompanyStore* store = GetStore();
	if(!store)
		return RenderPage(http, "Repositories", "admin", RenderFormError("Store is not initialized."), identity);

	const CompanyConfig& cfg = store->GetConfig();
	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Repository Allowlist"
	     << "<span class=\"cs-subtle\">empty list permits nothing (fail closed)</span></div>\n"
	     << "<form method=\"POST\" action=\"/admin/repos/save\" accept-charset=\"utf-8\">\n"
	     << RenderCsrfField(http) << "\n"
	     << "<div class=\"cs-form-row\"><label>Allowed repositories (one per line, owner/repo)</label>"
	     << "<textarea name=\"repositories\" class=\"cs-textarea\">";
	for(int i = 0; i < cfg.repository_allowlist.GetCount(); i++)
		body << CompanyHtmlEsc(cfg.repository_allowlist[i]) << "\n";
	body << "</textarea></div>\n"
	     << "<button type=\"submit\" class=\"cs-btn cs-btn-primary\">Save Allowlist</button>\n"
	     << "</form></div>\n";
	return RenderPage(http, "Repositories", "admin_repos", body, identity);
}

// Admin Platform overview: reference-shaped bookkeeping snapshot (commands,
// review dispatches, evidence, trials, artifacts) built on PlatformWorkflow.
String CompanyViews::RenderAdminPlatform(WebRequest& http, const WebAuthIdentity& identity)
{
	if(!PlatformWorkflow::GetStore())
		return RenderPage(http, "Platform", "admin", RenderFormError("Store is not initialized."), identity);

	ValueMap snap = PlatformWorkflow::StatusSnapshot();
	String body;
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Platform Control Plane"
	     << "<span class=\"cs-subtle\">Command Listener &middot; Review Workflow &middot; Trials &middot; Artifacts</span></div>\n"
	     << "<p class=\"cs-muted\">Reference-shaped v2 records. GPU trials, reviewer agents, sandboxes, and artifact "
	     << "content stay in external services; this site owns commands, dispatches, evidence, and gates.</p>\n"
	     << "</div>\n";

	Vector<String> cols;
	Vector<Vector<String>> rows;
	cols.Add("Component"); cols.Add("Total"); cols.Add("By status");

	Vector<String> comp_names;
	comp_names.Add("Commands"); comp_names.Add("Review dispatches"); comp_names.Add("Evidence");
	comp_names.Add("Trials"); comp_names.Add("Artifacts");
	Vector<String> total_keys;
	total_keys.Add("commands_total"); total_keys.Add("dispatches_total"); total_keys.Add("evidence_total");
	total_keys.Add("trials_total"); total_keys.Add("artifacts_total");
	Vector<String> status_keys;
	status_keys.Add("commands_by_status"); status_keys.Add("dispatches_by_status"); status_keys.Add("evidence_by_outcome");
	status_keys.Add("trials_by_status"); status_keys.Add("");
	for(int c = 0; c < comp_names.GetCount(); c++) {
		Vector<String> row;
		row.Add(comp_names[c]);
		row.Add(AsString(snap.Get(total_keys[c], 0)));
		String detail;
		if(!status_keys[c].IsEmpty() && snap.Find(status_keys[c]) >= 0) {
			ValueMap by_status = snap[status_keys[c]];
			for(int i = 0; i < by_status.GetCount(); i++) {
				if(i) detail << ", ";
				detail << CompanyHtmlEsc((String)by_status.GetKey(i)) << ": " << AsString(by_status[i]);
			}
		}
		row.Add(detail.IsEmpty() ? "&mdash;" : detail);
		rows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Bookkeeping snapshot</div>\n"
	     << RenderDataTable(cols, rows, "No platform records yet.") << "</div>\n";

	// Recent commands.
	Vector<PlatformCommandRecord> commands = PlatformWorkflow::GetStore()->GetPlatformCommands();
	Vector<String> ccols;
	Vector<Vector<String>> crows;
	ccols.Add("Command"); ccols.Add("Source"); ccols.Add("Repository"); ccols.Add("Actor");
	ccols.Add("Status"); ccols.Add("Refusal reason");
	for(int i = commands.GetCount() - 1; i >= 0 && crows.GetCount() < 12; i--) {
		Vector<String> row;
		row.Add(CompanyHtmlEsc(commands[i].command));
		row.Add(CompanyHtmlEsc(commands[i].source));
		row.Add(CompanyHtmlEsc(commands[i].repository + (commands[i].pr_number ? " #" + AsString(commands[i].pr_number) : "")));
		row.Add(CompanyHtmlEsc(commands[i].actor_login));
		row.Add(CompanyHtmlEsc(commands[i].status));
		row.Add(commands[i].refusal_reason.IsEmpty() ? "&mdash;" : CompanyHtmlEsc(commands[i].refusal_reason));
		crows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Recent commands</div>\n"
	     << RenderDataTable(ccols, crows, "No commands ingested yet.") << "</div>\n";

	// Recent dispatches with head SHA binding.
	Vector<PlatformReviewDispatch> dispatches = PlatformWorkflow::GetStore()->GetReviewDispatches();
	Vector<String> dcols;
	Vector<Vector<String>> drows;
	dcols.Add("Dispatch"); dcols.Add("Work item"); dcols.Add("Kind"); dcols.Add("Head SHA");
	dcols.Add("Status");
	for(int i = dispatches.GetCount() - 1; i >= 0 && drows.GetCount() < 12; i--) {
		Vector<String> row;
		row.Add(CompanyHtmlEsc(dispatches[i].id));
		row.Add(CompanyHtmlEsc(dispatches[i].work_item_id + " (PR #" + AsString(dispatches[i].pr_number) + ")"));
		row.Add(CompanyHtmlEsc(dispatches[i].kind));
		row.Add(CompanyHtmlEsc(dispatches[i].head_sha.IsEmpty() ? "unbound" : dispatches[i].head_sha.Left(12) + "&hellip;"));
		row.Add(CompanyHtmlEsc(dispatches[i].status));
		drows.Add(pick(row));
	}
	body << "<div class=\"cs-card\"><div class=\"cs-card-header\">Review dispatches (head-SHA bound)</div>\n"
	     << RenderDataTable(dcols, drows, "No review dispatches yet.") << "</div>\n";

	return RenderPage(http, "Platform", "admin_platform", body, identity);
}

String CompanyViews::RenderError(WebRequest& http, const WebAuthIdentity& identity, const String& title,
                                 const String& message, int code)
{
	String body;
	body << "<div class=\"cs-card\" style=\"text-align:center; padding:3rem 2rem;\">\n"
	     << "<h1 style=\"margin-bottom:0.5rem;\">" << CompanyHtmlEsc(title) << "</h1>\n"
	     << "<p class=\"cs-muted\">" << CompanyHtmlEsc(message) << "</p>\n"
	     << "<p style=\"margin-top:1.25rem;\"><a href=\"/\" class=\"cs-btn cs-btn-secondary\">Back to overview</a></p>\n"
	     << "</div>\n";
	return RenderPage(http, title, "home", body, identity);
}

END_UPP_NAMESPACE
