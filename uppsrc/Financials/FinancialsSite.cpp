#include "FinancialsSite.h"

NAMESPACE_UPP

// Financials: the read-only window over the Company module's money flows.
// Every read is a scope-checked broker op recorded in the diagnostics
// ledger; the site never touches the Company store. The viewer's role maps
// to the caller scopes: "viewer" carries ledger:read, "finance" carries
// ledger:read and admin (contracts and per-user rows are admin-gated in the
// module).

static const char *FINANCE_ROLE = "finance";

static String Esc(const String& s)
{
	StringBuffer out;
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
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

static bool RequireLogin(WebRequest& r)
{
	if(r.HasUser())
		return true;
	r.Redirect(r.Site().Url("session/login"));
	return false;
}

static bool IsFinance(WebRequest& r)
{
	for(const String& role : r.User().roles)
		if(role == FINANCE_ROLE)
			return true;
	return false;
}

static void Page(WebRequest& r, const String& title, const String& body)
{
	r.Html("<!doctype html><html><head><meta charset=\"utf-8\">"
	       "<title>" + Esc(title) + "</title></head><body>"
	       "<nav><a href=\"" + Esc(r.Site().Url("")) + "\">Financials</a> "
	       "<a href=\"" + Esc(r.Site().Url("ledger")) + "\">Ledger</a> "
	       "<a href=\"" + Esc(r.Site().Url("contracts")) + "\">Contracts</a> " + SessionChromeHtml(r) + "</nav>"
	       "<h1>" + Esc(title) + "</h1>" + body + "</body></html>");
}

// Broker payloads carry typed values; render numbers by their real type,
// strings verbatim.
static String CellText(const Value& v)
{
	if(v.Is<int>() || v.Is<int64>() || v.Is<double>() || v.Is<bool>())
		return AsString(v);
	return (String)v;
}

static void BrokerProblem(WebRequest& r, const String& error)
{
	r.Status(503, "the Company module did not answer");
	Page(r, "Financials unavailable",
	     "<p>The Company module did not answer, so this view cannot be shown.</p>"
	     "<p><code>" + Esc(error) + "</code></p>");
}

static void RoleProblem(WebRequest& r, const String& what)
{
	r.Status(403, "the finance role is required");
	Page(r, "Not available",
	     "<p>" + Esc(what) + " needs the finance role on this site.</p>");
}

// One broker call with the caller's role-mapped scopes. The Company module
// re-checks the scopes, so the site mapping is a convenience, not the gate.
static BrokerResult AskCompany(WebRequest& r, const String& op, const ValueMap& fields)
{
	BrokerCallContext ctx;
	ctx.from_site = r.Site().id;
	ctx.to_site = "company";
	Vector<String> scopes;
	scopes.Add("ledger:read");
	if(IsFinance(r))
		scopes.Add("admin");
	ctx.scopes <<= scopes;
	BrokerCall call;
	call.op = op;
	call.payload = fields;
	return TheWebEngine().Broker().Call(ctx, call);
}

static void Summary(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	BrokerResult res = AskCompany(r, "ledger-summary", ValueMap());
	if(!res.ok) {
		BrokerProblem(r, res.error);
		return;
	}
	StringBuffer b;
	b << "<table><tr><th>Flow</th><th>Total</th></tr>"
	  << "<tr><td>Credits</td><td>" << Esc(CellText(res.payload.Get("credit_total", 0))) << "</td></tr>"
	  << "<tr><td>Debits</td><td>" << Esc(CellText(res.payload.Get("debit_total", 0))) << "</td></tr>"
	  << "<tr><td>Corrections</td><td>" << Esc(CellText(res.payload.Get("correction_total", 0))) << "</td></tr>"
	  << "<tr><td>Entries</td><td>" << Esc(CellText(res.payload.Get("entry_count", 0))) << "</td></tr>"
	  << "</table>";
	String newest = (String)res.payload.Get("newest_entry_at", "");
	if(newest.GetCount())
		b << "<p>Newest entry: " << Esc(newest) << "</p>";
	Page(r, "Financials summary", String(b));
}

static void Ledger(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	if(!IsFinance(r)) {
		RoleProblem(r, "The ledger detail");
		return;
	}
	ValueMap f;
	f.Add("limit", 50);
	BrokerResult res = AskCompany(r, "ledger-recent", f);
	if(!res.ok) {
		BrokerProblem(r, res.error);
		return;
	}
	StringBuffer b;
	ValueArray rows;
	Value rv = res.payload.Get("entries");
	if(rv.Is<ValueArray>())
		rows = rv;
	if(rows.GetCount() == 0)
		b << "<p>No ledger entries yet.</p>";
	else {
		b << "<table><tr><th>Seq</th><th>Type</th><th>Amount</th><th>Reason</th><th>Recorded</th></tr>";
		for(const Value& v : rows) {
			ValueMap m = v;
			b << "<tr><td>" << Esc(CellText(m.Get("seq", 0))) << "</td><td>" << Esc(CellText(m.Get("type", "")))
			  << "</td><td>" << Esc(CellText(m.Get("amount", 0))) << "</td><td>"
			  << Esc(CellText(m.Get("reason", ""))) << "</td><td>" << Esc(CellText(m.Get("created_at", "")))
			  << "</td></tr>";
		}
		b << "</table>";
	}
	Page(r, "Ledger", String(b));
}

static void Contracts(WebRequest& r)
{
	if(!RequireLogin(r))
		return;
	if(!IsFinance(r)) {
		RoleProblem(r, "The contracts view");
		return;
	}
	BrokerResult res = AskCompany(r, "contracts-list", ValueMap());
	if(!res.ok) {
		BrokerProblem(r, res.error);
		return;
	}
	StringBuffer b;
	ValueArray rows;
	Value rv = res.payload.Get("contracts");
	if(rv.Is<ValueArray>())
		rows = rv;
	if(rows.GetCount() == 0)
		b << "<p>No contracts yet.</p>";
	else {
		b << "<table><tr><th>Id</th><th>Title</th><th>Status</th><th>Amount</th></tr>";
		for(const Value& v : rows) {
			ValueMap m = v;
			String amount = CellText(m.Get("amount", 0.0));
			String currency = CellText(m.Get("currency", ""));
			b << "<tr><td>" << Esc(CellText(m.Get("id", ""))) << "</td><td>" << Esc(CellText(m.Get("title", "")))
			  << "</td><td>" << Esc(CellText(m.Get("status", ""))) << "</td><td>" << Esc(amount)
			  << (currency.IsEmpty() ? String() : String(" ") + Esc(currency)) << "</td></tr>";
		}
		b << "</table>";
	}
	Page(r, "Contracts", String(b));
}

void InstallFinancialsSite(EngineSite& site)
{
	SiteRouter& rt = site.Router();
	rt.Add("", Summary);
	rt.Add("financials", Summary);
	rt.Add("ledger", Ledger);
	rt.Add("contracts", Contracts);

	CommandManifest& cm = TheWebEngine().Manifest();
	cm.Add(site.id, site.title, HttpCommand("financials-summary", "GET", "financials", "session account",
	                                        "html", "Ledger totals and entry count (read-only)", {}));
	cm.Add(site.id, site.title, HttpCommand("financials-ledger", "GET", "ledger", "finance role",
	                                        "html", "Recent ledger entries (read-only)", {}));
	cm.Add(site.id, site.title, HttpCommand("financials-contracts", "GET", "contracts", "finance role",
	                                        "html", "Client contracts and their statuses (read-only)", {}));
}

END_UPP_NAMESPACE
