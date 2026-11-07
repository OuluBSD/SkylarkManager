#include "ManagerSkylark.h"
#include "CatalogIndex.h"
#include "ScopedPolicy.h"
#include "EvidenceIngestion.h"
#include "PaperSourceImport.h"
#include "PaperHttpTransport.h"
#include "PaperResearch.h"
#include "HistoricalAssessmentMigration.h"
#include "AssessmentService.h"
#include "JobService.h"
#include "AssessmentView.h"
#include "CandidateLineage.h"
#include "CandidateWorkflow.h"
#include "ReviewPackage.h"
#include "ReviewDiscussion.h"
#include <dirent.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sqlite3.h>
#include <thread>
#include <atomic>

NAMESPACE_UPP

static bool SafeSyntheticPaperEnvironment(const String& env)
{
#ifdef PLATFORM_WIN32
	if(env.GetCount()<2*(int)sizeof(WCHAR)||env.GetCount()%(int)sizeof(WCHAR)) return false;
	const WCHAR *p=(const WCHAR *)env.Begin(); int count=env.GetCount()/sizeof(WCHAR); Index<String> keys;
	for(int i=0;i<count;) {
		if(!p[i]) return i==count-1&&keys.GetCount()==4;
		int start=i; while(i<count&&p[i]&&p[i]!='=') i++;
		if(i==count||p[i]!='='||i==start) return false;
		String key; for(int j=start;j<i;j++) { if(p[j]>127) return false; key.Cat((char)p[j]); }
		key=ToLower(key);
		if((key!="home"&&key!="path"&&key!="systemroot"&&key!="temp")||keys.Find(key)>=0) return false;
		keys.Add(key); while(i<count&&p[i]) i++; if(i==count) return false; i++;
	}
	return false;
#else
	Index<String> keys; int start=0;
	for(int i=0;i<env.GetCount();i++) if(!env[i]) {
		if(i==start) return i==env.GetCount()-1&&keys.GetCount()==2&&keys.Find("HOME")>=0&&keys.Find("PATH")>=0;
		String entry=env.Mid(start,i-start); int eq=entry.Find('='); if(eq<=0) return false;
		String key=entry.Left(eq),value=entry.Mid(eq+1);
		if((key!="HOME"&&key!="PATH")||keys.Find(key)>=0||(key=="HOME"&&value!="/nonexistent")||(key=="PATH"&&value!="/usr/bin:/bin")) return false;
		keys.Add(key); start=i+1;
	}
	return false;
#endif
}

class SyntheticPaperHttpRunner : public PaperHttpRunner {
public:
	Vector<String> replies;
	Vector<int> timed_out, succeeds;
	Vector<String> arguments;
	int next=0;
	bool Run(const String&, const Vector<String>& args, const String& env, int, int, String& output, bool& timeout) override {
		String joined; for(const String& arg:args) joined<<arg<<"\n"; arguments.Add(joined);
		if(!SafeSyntheticPaperEnvironment(env) || next>=replies.GetCount()) return false;
		output=replies[next]; timeout=timed_out[next]!=0; bool ok=succeeds[next]!=0; next++; return ok;
	}
	void Reset() { replies.Clear(); timed_out.Clear(); succeeds.Clear(); arguments.Clear(); next=0; }
	void Add(const String& reply,bool timeout=false,bool success=true) { replies.Add(reply); timed_out.Add(timeout); succeeds.Add(success); }
};

class SyntheticPaperDnsResolver : public PaperDnsResolver {
public:
	Vector<String> addresses;
	Vector<Vector<String>> sequences;
	int calls=0;
	bool Resolve(const String&,Vector<String>& result) override { int i=calls++; result=i<sequences.GetCount()?clone(sequences[i]):clone(addresses); return !result.IsEmpty(); }
};

static bool RunPaperTransportTests()
{
	PublicPaperSourceRule rule; rule.rule_id="t81-rule"; rule.hostname="papers.synthetic-example.org"; rule.path_prefix="/approved"; rule.max_bytes=64;
	SyntheticPaperHttpRunner process; SyntheticPaperDnsResolver dns; dns.addresses.Add("8.8.8.8");
	CurlPaperTransport transport(rule,"/usr/bin/curl",process,dns); PaperTransportResponse response; String error;
	process.Add("HTTP/1.1 301 Moved\r\nLocation: /approved/next\r\n\r\n");
	process.Add("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nContent-Length: 4\r\n\r\nPDF!");
	if(!transport.Fetch("https://papers.synthetic-example.org/approved/start",response,error)||response.body!="PDF!"||response.redirects.GetCount()!=1||response.final_url!="https://papers.synthetic-example.org/approved/next"||!response.attestation.Matches(response.url,response.final_url,response.redirects,response.body,response.mime_type)) return false;
	String args=process.arguments[0];
	for(const String& required:{"--disable\n","--proxy\n\n","--noproxy\n*\n","--max-redirs\n0\n","--proto\n=https\n","--proto-redir\n=https\n","--resolve\npapers.synthetic-example.org:443:8.8.8.8\n"}) if(args.Find(required)<0) return false;
	for(const String& forbidden:{"--insecure","--cookie","--netrc","--config","--user"}) if(args.Find(forbidden)>=0) return false;
	if(dns.calls!=2) return false;
	CurlPaperTransport missing_curl(rule,"",process,dns); PaperTransportResponse absent;
	if(missing_curl.Fetch("https://papers.synthetic-example.org/approved/start",absent,error)) return false;
	auto reject = [&](const String& first, int replies, const String& ip="8.8.8.8", bool timeout=false) {
	process.Reset(); dns.calls=0; dns.sequences.Clear(); dns.addresses.Clear(); dns.addresses.Add(ip);
		for(int i=0;i<replies;i++) process.Add(first,timeout);
		PaperTransportResponse out; String why; return !transport.Fetch("https://papers.synthetic-example.org/approved/start",out,why)&&why=="paper_transport_failed";
	};
	String invalid_header="HTTP/1.1 200 OK\r\nX-Test: "; invalid_header.Cat(1); invalid_header<<"\r\nContent-Type: application/pdf\r\n\r\nPDF!";
	if(!reject("HTTP/1.1 301 Moved\r\nLocation: https://attacker.invalid/approved/x\r\n\r\n",1)||
	   !reject("HTTP/1.1 301 Moved\r\nLocation: http://papers.example.invalid/approved/x\r\n\r\n",1)||
	   !reject("HTTP/1.1 301 Moved\r\nLocation: /approved/x\r\nLocation: /approved/y\r\n\r\n",1)||
	   !reject("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nno",1)||
	   !reject("HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\n\r\nno",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nno",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nContent-Length: xyz\r\n\r\nPDF!",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nContent-Length: 4\r\nContent-Length: 5\r\n\r\nPDF!",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nContent-Length: 5\r\n\r\nPDF!",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nTransfer-Encoding: gzip\r\n\r\nPDF!",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\nPDF!",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nContent-Length: 4\r\nTransfer-Encoding: chunked\r\n\r\nPDF!",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nContent-Type: text/plain\r\n\r\nPDF!",1)||
	   !reject(invalid_header,1)||
	   !reject("HTTP/1.1 20x Invalid\r\nContent-Type: application/pdf\r\n\r\nPDF!",1)||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n\r\n"+String('x',65),1)||
	   !reject(String('h',17000),1)||!reject("",1,"127.0.0.1")||
	   !reject("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n\r\nPDF!",1,"8.8.8.8",true)) return false;
	process.Reset(); dns.calls=0; dns.sequences.Clear(); dns.addresses.Clear(); dns.addresses.Add("8.8.8.8");
	process.Add("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nTransfer-Encoding: chunked\r\n\r\nPDF!");
	PaperTransportResponse chunked; if(!transport.Fetch("https://papers.synthetic-example.org/approved/start",chunked,error)) return false;
	process.Reset(); dns.calls=0; dns.sequences.Clear(); dns.addresses.Clear(); dns.addresses.Add("8.8.8.8");
	for(int i=0;i<3;i++) process.Add("HTTP/1.1 302 Found\r\nLocation: /approved/step"+AsString(i)+"\r\n\r\n");
	process.Add("HTTP/2 200\r\nContent-Type: text/plain\r\n\r\nok");
	PaperTransportResponse three_hops;
	if(!transport.Fetch("https://papers.synthetic-example.org/approved/start",three_hops,error)||three_hops.redirects.GetCount()!=3) return false;
	process.Reset(); dns.calls=0; dns.sequences.Clear(); dns.addresses.Clear(); dns.addresses.Add("8.8.8.8"); dns.addresses.Add("10.0.0.1");
	process.Add("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n\r\npdf");
	PaperTransportResponse mixed_dns; if(transport.Fetch("https://papers.synthetic-example.org/approved/start",mixed_dns,error)) return false;
	process.Reset(); dns.calls=0; dns.sequences.Clear(); dns.addresses.Clear(); dns.addresses.Add("2001:4860:4860::8888");
	process.Add("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nok");
	PaperTransportResponse ipv6; if(!transport.Fetch("https://papers.synthetic-example.org/approved/start",ipv6,error)||process.arguments[0].Find("[2001:4860:4860::8888]")<0) return false;
	process.Reset(); dns.calls=0; dns.sequences.Clear(); dns.addresses.Clear(); dns.addresses.Add("8.8.8.8");
	for(int i=0;i<4;i++) process.Add("HTTP/1.1 301 Moved\r\nLocation: /approved/next\r\n\r\n");
	PaperTransportResponse excessive; if(transport.Fetch("https://papers.synthetic-example.org/approved/start",excessive,error)) return false;
	process.Reset(); dns.calls=0; dns.sequences.Clear(); dns.sequences.Add(Vector<String>()); dns.sequences.Top().Add("8.8.8.8"); dns.sequences.Add(Vector<String>()); dns.sequences.Top().Add("169.254.1.1");
	process.Add("HTTP/1.1 301 Moved\r\nLocation: /approved/next\r\n\r\n");
	if(transport.Fetch("https://papers.synthetic-example.org/approved/start",excessive,error)) return false;
	return true;
}

static bool RejectUnavailableCatalogSource(Http& http, const CatalogGeneration& catalog, bool data_source)
{
	String status = data_source ? catalog.data_status : catalog.v2_status;
	if(status != "load_error") return false;
	ValueMap response;
	response.Add("success", false);
	response.Add("error", "catalog_source_unavailable");
	response.Add("source_status", status);
	response.Add("catalog_generation", (int64)catalog.generation);
	ValueArray diagnostics;
	for(const String& diagnostic : catalog.diagnostics) diagnostics.Add(diagnostic);
	response.Add("diagnostics", diagnostics);
	http.ContentType("application/json");
	http.Response(503, "Source Unavailable");
	http << AsJSON(response, true);
	return true;
}

SKYLARK(ApiHealth, "api/health")
{
	ValueMap m;
	m.Add("status", "ok");
	m.Add("app", "ManagerSkylark");
	m.Add("version", "1.0.0");
	m.Add("v2_supported", true);
	http.ContentType("application/json") << AsJSON(m, true);
}

static void WriteAssessmentJson(Http& http, const String& detail=String())
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=AssessmentViewService::Build(app,*catalog,(String)http["workspace"],(String)http["run"],
	                                            (String)http["filter"],(String)http["sort"],(String)http["direction"],(String)http["dimension"],detail);
	http.ContentType("application/json; charset=utf-8")<<AsJSON(view,true);
}
SKYLARK(ApiAssessmentOverview,"api/assessments") { WriteAssessmentJson(http); }
SKYLARK(ApiAssessmentDetail,"api/assessments/detail/*") { WriteAssessmentJson(http,(String)http[0]); }
SKYLARK(ApiAssessmentCompare,"api/assessments/compare") { WriteAssessmentJson(http); }

SKYLARK(ApiReviews,"api/reviews")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=ReviewPackageViewService::Build(app,*catalog);
	http.ContentType("application/json; charset=utf-8")<<AsJSON(view,true);
}
SKYLARK(ApiReviewPackage,"api/reviews/package/*/*")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=ReviewPackageViewService::Build(app,*catalog,(String)http[0],(String)http[1]);
	if(MapGetString(view,"view_state")=="unavailable") http.Response(404,"Review Package Unavailable");
	http.ContentType("application/json; charset=utf-8")<<AsJSON(view,true);
}
SKYLARK(ApiReviewThread,"api/reviews/thread/*")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); auto catalog=app.PinCatalog();
	ValueMap view=ReviewPackageViewService::BuildThread(app,*catalog,(String)http[0]);
	if(MapGetString(view,"view_state")=="unavailable") http.Response(404,"Review Thread Unavailable");
	http.ContentType("application/json; charset=utf-8")<<AsJSON(view,true);
}
SKYLARK(ApiReviewComment,"api/reviews/comment:POST_RAW")
{
	String bytes=http.GetRequestContent(); if(bytes.GetCount()>16384) { http.Response(413,"Request Too Large"); http.ContentType("application/json")<<"{\"status\":\"invalid\",\"reason\":\"request_too_large\"}"; return; }
	Value parsed=ParseJSON(bytes); static const char *keys[]={"workspace_id","package_id","revision_id","thread_id","body","record_type","operation_id"};
	if(!parsed.Is<ValueMap>()) { http.Response(400,"Invalid Request"); http.ContentType("application/json")<<"{\"status\":\"invalid\"}"; return; }
	ValueMap in=parsed; bool exact=in.GetCount()==(int)__countof(keys); for(int i=0;exact&&i<in.GetCount();i++){ bool found=false; for(const char *k:keys) found|=in.GetKey(i)==k; exact=found; }
	if(!exact) { http.Response(400,"Invalid Request"); http.ContentType("application/json")<<"{\"status\":\"invalid\",\"reason\":\"unknown_or_missing_fields\"}"; return; }
	for(const char *k:keys) if(!IsString(MapGet(in,k))) { http.Response(400,"Invalid Request"); http.ContentType("application/json")<<"{\"status\":\"invalid\",\"reason\":\"invalid_field_types\"}"; return; }
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); UserSession session=SystemAuthService::GetCurrentSession(http); auto catalog=app.PinCatalog();
	ValueMap result=ReviewDiscussionService::AppendHumanRecord(*catalog,app,session,MapGetString(in,"workspace_id"),MapGetString(in,"package_id"),MapGetString(in,"revision_id"),MapGetString(in,"thread_id"),MapGetString(in,"body"),MapGetString(in,"record_type"),MapGetString(in,"operation_id"));
	http.ContentType("application/json; charset=utf-8")<<AsJSON(result,true);
}

SKYLARK(ApiCandidateWorkflow, "api/candidates/workflow")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get(); auto catalog = app.PinCatalog();
	ValueMap response = CandidateWorkflow::Project(catalog->data, app, (String)http["workspace"], (String)http["candidate"]);
	http.ContentType("application/json; charset=utf-8") << AsJSON(response, true);
}

SKYLARK(ApiCandidateCreate, "api/candidates/proposal:POST_RAW")
{
	String body = http.GetRequestContent();
	if(body.GetCount() > 65536) { http.Response(413, "Request Too Large"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"request_too_large\"}"; return; }
	Value request = ParseJSON(body);
	if(!request.Is<ValueMap>()) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_json\"}"; return; }
	ValueMap input = request;
	static const char *const allowed[] = { "workspace_id", "operation_id", "form" };
	if(input.GetCount() != 3) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"unknown_fields\"}"; return; }
	for(int i = 0; i < input.GetCount(); i++) { bool ok = false; for(const char *k : allowed) ok |= input.GetKey(i) == k; if(!ok) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"unknown_fields\"}"; return; } }
	if(!IsString(MapGet(input, "workspace_id")) || !IsString(MapGet(input, "operation_id")) || !MapGet(input, "form").Is<ValueMap>()) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_field_types\"}"; return; }
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	UserSession session = SystemAuthService::GetCurrentSession(http);
	if(!SystemAuthService::AuthorizeWriteForSession(app, session, MapGetString(input, "workspace_id"))) {
		http.Response(403, "Forbidden"); http.ContentType("application/json; charset=utf-8") << "{\"status\":\"denied\",\"reason\":\"write_authorization_denied\"}"; return;
	}
	auto catalog = app.PinCatalog();
	ValueMap result = CandidateWorkflow::CreateProposal(catalog->data, app, MapGet(input, "form"), MapGetString(input, "workspace_id"), catalog->data.GetSourceId(), MapGetString(input, "operation_id"), session);
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ApiCandidateRevise, "api/candidates/proposal/revise:POST_RAW")
{
	String body = http.GetRequestContent();
	if(body.GetCount() > 65536) { http.Response(413, "Request Too Large"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"request_too_large\"}"; return; }
	Value request = ParseJSON(body);
	static const char *const allowed[] = { "workspace_id", "candidate_id", "proposal_id", "expected_head_revision", "expected_revision_id", "operation_id", "form" };
	if(!request.Is<ValueMap>()) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_json\"}"; return; }
	ValueMap input = request; bool exact = input.GetCount() == (int)__countof(allowed);
	for(int i = 0; exact && i < input.GetCount(); i++) { bool found = false; for(const char *k : allowed) found |= input.GetKey(i) == k; exact &= found; }
	if(!exact) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"unknown_or_missing_fields\"}"; return; }
	for(const char *k : { "workspace_id", "candidate_id", "proposal_id", "expected_head_revision", "expected_revision_id", "operation_id" }) if(!IsString(MapGet(input, k))) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_field_types\"}"; return; }
	if(!MapGet(input, "form").Is<ValueMap>()) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_field_types\"}"; return; }
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get(); UserSession session = SystemAuthService::GetCurrentSession(http);
	if(!SystemAuthService::AuthorizeWriteForSession(app, session, MapGetString(input, "workspace_id"))) {
		http.Response(403, "Forbidden"); http.ContentType("application/json; charset=utf-8") << "{\"status\":\"denied\",\"reason\":\"write_authorization_denied\"}"; return;
	}
	auto catalog = app.PinCatalog();
	ValueMap result = CandidateWorkflow::ReviseProposal(catalog->data, app, MapGetString(input, "workspace_id"), MapGetString(input, "candidate_id"), MapGetString(input, "proposal_id"), MapGetString(input, "expected_head_revision"), MapGetString(input, "expected_revision_id"), MapGet(input, "form"), catalog->data.GetSourceId(), MapGetString(input, "operation_id"), session);
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ApiCandidateAdvance, "api/candidates/stage:POST_RAW")
{
	String body = http.GetRequestContent();
	if(body.GetCount() > 32768) { http.Response(413, "Request Too Large"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"request_too_large\"}"; return; }
	Value request = ParseJSON(body);
	static const char *const allowed[] = { "workspace_id", "candidate_id", "proposal_revision_id", "expected_candidate_revision", "expected_head_revision", "expected_head_event_id", "next_stage", "reason", "evidence_ids", "operation_id", "review_outcome" };
	if(!request.Is<ValueMap>()) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_json\"}"; return; }
	ValueMap input = request; if(input.GetCount() != (int)__countof(allowed)) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"unknown_or_missing_fields\"}"; return; }
	for(int i = 0; i < input.GetCount(); i++) { bool ok = false; for(const char *k : allowed) ok |= input.GetKey(i) == k; if(!ok) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"unknown_fields\"}"; return; } }
	for(const char *k : { "workspace_id", "candidate_id", "proposal_revision_id", "expected_candidate_revision", "expected_head_revision", "expected_head_event_id", "next_stage", "reason", "operation_id", "review_outcome" }) if(!IsString(MapGet(input, k))) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_field_types\"}"; return; }
	if(!MapGet(input, "evidence_ids").Is<ValueArray>()) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"evidence_ids_must_be_array\"}"; return; }
	if(MapGetString(input, "workspace_id").GetCount() > 128 || MapGetString(input, "candidate_id").GetCount() > 256 || MapGetString(input, "proposal_revision_id").IsEmpty() || MapGetString(input, "proposal_revision_id").GetCount() > 256 || MapGetString(input, "expected_candidate_revision").GetCount() > 32 || MapGetString(input, "expected_head_revision").GetCount() > 32 || MapGetString(input, "expected_head_event_id").GetCount() > 256 || MapGetString(input, "next_stage").GetCount() > 64 || MapGetString(input, "reason").GetCount() > 1024 || MapGetString(input, "operation_id").IsEmpty() || MapGetString(input, "operation_id").GetCount() > 128 || MapGetString(input, "review_outcome").GetCount() > 32 || ValueArray(MapGet(input, "evidence_ids")).GetCount() > 64) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"field_limit_exceeded\"}"; return; }
	for(const Value& id : ValueArray(MapGet(input, "evidence_ids"))) if(!IsString(id) || ((String)id).IsEmpty() || ((String)id).GetCount() > 256) { http.Response(400, "Invalid Request"); http.ContentType("application/json") << "{\"status\":\"invalid\",\"reason\":\"invalid_evidence_id\"}"; return; }
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get(); UserSession session = SystemAuthService::GetCurrentSession(http);
	if(!SystemAuthService::AuthorizeWriteForSession(app, session, MapGetString(input, "workspace_id"))) {
		http.Response(403, "Forbidden"); http.ContentType("application/json; charset=utf-8") << "{\"status\":\"denied\",\"reason\":\"write_authorization_denied\"}"; return;
	}
	auto catalog = app.PinCatalog();
	ValueMap result = CandidateWorkflow::AdvanceStage(catalog->data, app, MapGetString(input, "workspace_id"), MapGetString(input, "candidate_id"), MapGetString(input, "proposal_revision_id"), MapGetString(input, "expected_candidate_revision"), MapGetString(input, "expected_head_revision"), MapGetString(input, "expected_head_event_id"), MapGetString(input, "next_stage"), MapGetString(input, "reason"), MapGet(input, "evidence_ids"), catalog->data.GetSourceId(), MapGetString(input, "operation_id"), session, MapGetString(input, "review_outcome"));
	http.ContentType("application/json; charset=utf-8") << AsJSON(result, true);
}

SKYLARK(ApiEffectivePolicy, "api/policy/effective:POST_RAW")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	Value request = ParseJSON(http.GetRequestContent());
	if(!request.Is<ValueMap>()) {
		http.Response(400, "Invalid Request");
		http.ContentType("application/json") << "{\"error\":\"invalid_policy_request\"}";
		return;
	}
	ValueMap map = request;
	static const char *const allowed[] = { "workspace_id", "source_ids", "provider_id", "action" };
	bool fields_ok = map.GetCount() == 4;
	for(int i = 0; fields_ok && i < map.GetCount(); i++) {
		String key = map.GetKey(i);
		bool known = false;
		for(const char *field : allowed) known |= key == field;
		fields_ok &= known;
	}
	if(!fields_ok || !map["workspace_id"].Is<String>() || !map["provider_id"].Is<String>() ||
	   !map["action"].Is<String>() || !map["source_ids"].Is<ValueArray>()) {
		http.Response(400, "Invalid Request");
		http.ContentType("application/json") << "{\"error\":\"invalid_policy_request\"}";
		return;
	}
	PolicyInput input;
	String workspace_id = map["workspace_id"];
	input.workspace.workspace_id = workspace_id;
	input.workspace.object_id = workspace_id;
	input.provider.workspace_id = workspace_id;
	input.provider.object_id = map["provider_id"];
	for(const Value& value : ValueArray(map["source_ids"])) {
		if(!value.Is<String>()) {
			http.Response(400, "Invalid Request");
			http.ContentType("application/json") << "{\"error\":\"invalid_policy_request\"}";
			return;
		}
		PolicyIdentity source;
		source.workspace_id = workspace_id;
		source.object_id = value;
		input.sources.Add(source);
	}
	String action = map["action"];
	if(action == "model") input.action = POLICY_ACTION_MODEL;
	else if(action == "publication") input.action = POLICY_ACTION_PUBLICATION;
	else {
		http.Response(400, "Invalid Request");
		http.ContentType("application/json") << "{\"error\":\"invalid_policy_request\"}";
		return;
	}
	http.ContentType("application/json") << AsJSON(app.ResolveEffectivePolicy(input).ToRedactedValue(), true);
}

static ValueMap RedactedJobStatus(const DataDocument& doc)
{
	ValueMap result; result.Add("id",doc.id); result.Add("workspace_id",doc.workspace_id);
	result.Add("state",MapGetString(doc.data,"state")); result.Add("revision",doc.revision);
	result.Add("created_at",MapGetString(doc.data,"created_at")); result.Add("updated_at",MapGetString(doc.data,"updated_at"));
	result.Add("attempt",MapGet(doc.data,"attempt")); result.Add("failure_class",MapGetString(doc.data,"failure_class"));
	return result;
}

SKYLARK(ApiJobs, "api/jobs")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); String ws=http["workspace"];
	UserSession session=SystemAuthService::GetCurrentSession(http);
	if(ws.IsEmpty()||!SystemAuthService::AuthorizeWriteForSession(app,session,ws)) { http.Response(403,"Forbidden"); http.ContentType("application/json")<<"{\"status\":\"denied\"}"; return; }
	CatalogReadHandle catalog=app.PinCatalog(); ValueArray jobs;
	if(catalog&&catalog->IsComplete()) for(const DataDocument& d:catalog->data.GetDocuments()) if(d.kind=="durable-job"&&d.workspace_id==ws&&d.valid&&d.validated) jobs.Add(RedactedJobStatus(d));
	ValueMap response; response.Add("jobs",jobs); http.ContentType("application/json; charset=utf-8")<<AsJSON(response,true);
}

SKYLARK(ApiJobDetail, "api/jobs/*")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); String id=http[0], ws=http["workspace"];
	UserSession session=SystemAuthService::GetCurrentSession(http);
	if(ws.IsEmpty()||!SystemAuthService::AuthorizeWriteForSession(app,session,ws)) { http.Response(403,"Forbidden"); http.ContentType("application/json")<<"{\"status\":\"denied\"}"; return; }
	CatalogReadHandle catalog=app.PinCatalog(); const DataDocument *d=catalog&&catalog->IsComplete()?catalog->data.FindDocument(ws+":"+id):NULL;
	if(!d||d->kind!="durable-job"||!d->valid||!d->validated) { http.Response(404,"Not Found"); http.ContentType("application/json")<<"{\"status\":\"not_found\"}"; return; }
	http.ContentType("application/json; charset=utf-8")<<AsJSON(RedactedJobStatus(*d),true);
}

SKYLARK(ApiJobSubmitDisabled, "api/jobs/submit:POST_RAW")
{
	ManagerSkylarkApp& app=ManagerSkylarkApp::Get(); String ws=http.GetHeader("x-workspace-id");
	UserSession session=SystemAuthService::GetCurrentSession(http);
	if(ws.IsEmpty()||!SystemAuthService::AuthorizeWriteForSession(app,session,ws)) { http.Response(403,"Forbidden"); http.ContentType("application/json")<<"{\"status\":\"denied\"}"; return; }
	CatalogReadHandle catalog=app.PinCatalog();
	if(!catalog||!catalog->IsComplete()||catalog->data.GetLoadStatus()!="loaded"||catalog->data.GetSourceId().IsEmpty()) { http.Response(403,"Forbidden"); http.ContentType("application/json")<<"{\"status\":\"denied\"}"; return; }
	http.Response(503,"Service Unavailable"); http.ContentType("application/json; charset=utf-8")<<"{\"status\":\"disabled\",\"reason\":\"executor_unavailable\"}";
}

// ---------------------------------------------------------------------------
// Task-1 Ideas: task idea documents served from the generic DataStore
// (scan root from private runtime configuration). Same projection as the /data/ideas page.
// ---------------------------------------------------------------------------
static void RemoveOwnedTree(const String& path)
{
	if(!DirectoryExists(path))
		return;
	FindFile ff(AppendFileName(path, "*"));
	while(ff) {
		String name = ff.GetName();
		if(name == "." || name == "..") {
			ff.Next();
			continue;
		}
		String child = ff.GetPath();
		if(ff.IsSymLink())
			FileDelete(child);
		else if(ff.IsDirectory())
			RemoveOwnedTree(child);
		else
			FileDelete(child);
		ff.Next();
	}
	DirectoryDelete(path.ToStd().c_str());
}

static bool CreatePrivateDirectory(const String& path)
{
#ifdef PLATFORM_POSIX
	return DirectoryCreate(path.ToStd().c_str(), 0700);
#else
	return DirectoryCreate(path.ToStd().c_str());
#endif
}

struct SelfTestWorkspace {
	String root;
	String fixtures;
	SelfTestWorkspace() {
		for(int i = 0; i < 32; i++) {
			String leaf = "mgrskylark-test-" + AsString(GetTickCount()) + "-" + AsString(Random64());
			String candidate = AppendFileName(GetTempDirectory(), leaf);
			if(CreatePrivateDirectory(candidate)) {
				root = candidate;
				break;
			}
		}
		if(root.IsEmpty())
			Panic("Could not create exclusive self-test workspace");
		fixtures = AppendFileName(root, "fixtures");
		if(!CreatePrivateDirectory(fixtures) ||
		   !CreatePrivateDirectory(AppendFileName(root, "mgr")) ||
		   !CreatePrivateDirectory(AppendFileName(root, "data")) ||
		   !CreatePrivateDirectory(AppendFileName(root, "v2"))) {
			RemoveOwnedTree(root);
			root.Clear();
			Panic("Could not initialize self-test workspace");
		}
		for(int i = 0; i < 16; i++) {
			String shard = AppendFileName(fixtures, Format("%02d", i));
			if(!CreatePrivateDirectory(shard)) {
				RemoveOwnedTree(root);
				root.Clear();
				Panic("Could not initialize self-test fixture shards");
			}
		}
	}
	String FixtureRoot(const String& name) const {
		if(name.IsEmpty()) {
			Cerr() << "FAILED empty self-test fixture name\n";
			Panic("Invalid self-test fixture name");
		}
		for(int i = 0; i < name.GetCount(); i++) {
			char c = name[i];
			if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			     (c >= '0' && c <= '9') || c == '-' || c == '_')) {
				Cerr() << "FAILED invalid self-test fixture name=" << name << "\n";
				Panic("Invalid self-test fixture name");
			}
		}
		unsigned hash = (unsigned)GetHashValue(name);
		String shard = AppendFileName(fixtures, Format("%02d", (int)(hash % 16)));
		if(!DirectoryExists(shard) && !CreatePrivateDirectory(shard) && !DirectoryExists(shard)) {
			Cerr() << "FAILED self-test fixture shard create (errno=" << errno
			       << ", workspace=" << NormalizePath(root)
			       << ", fixtures=" << DirectoryExists(fixtures)
			       << ", shard=" << GetFileName(shard) << ")\n";
			Panic("Could not create self-test fixture shard");
		}
		return AppendFileName(shard, name);
	}
	~SelfTestWorkspace() { if(!root.IsEmpty()) RemoveOwnedTree(root); }
};

// Per-fixture cleanup; the workspace guard removes the parent after the app
// and server have been destroyed.
struct CurrentDirectoryGuard {
	String previous;
	CurrentDirectoryGuard() : previous(GetCurrentDirectory()) {}
	bool Enter(const String& path) { return ChangeCurrentDirectory(path.ToStd().c_str()); }
	void Restore() { if(!previous.IsEmpty()) ChangeCurrentDirectory(previous.ToStd().c_str()); previous.Clear(); }
	~CurrentDirectoryGuard() { Restore(); }
};

static int FindAvailableTestPort(int requested)
{
	for(int port = requested; port < 65536 && port < requested + 100; port++) {
		IpAddrInfo loopback;
		if(!loopback.Execute("127.0.0.1", port, IpAddrInfo::FAMILY_IPV4))
			continue;
		TcpSocket probe;
		if(probe.Listen(loopback, port, 1, false, false)) {
			probe.Close();
			return port;
		}
	}
	return 0;
}

struct SelfTestServerGuard {
	ManagerSkylarkApp *app = NULL;
	Thread *thread = NULL;
	~SelfTestServerGuard() {
		if(thread && thread->IsOpen()) {
			app->Quit();
			thread->Wait();
		}
	}
};

SKYLARK(ApiTaskIdeas, "api/tasks/ideas")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectUnavailableCatalogSource(http, *catalog, true)) return;
	const DataStore& store = catalog->data;

	ValueArray ideas;
	Vector<const DataDocument*> docs = app.QueryCatalogDocuments(*catalog);
	for(int i = 0; i < docs.GetCount(); i++) {
		const DataDocument& d = *docs[i];
		if(d.kind == "task-proposal" || d.kind == "research-candidate" || d.kind == "task") {
			CatalogRecordRead read = ReadCatalogRecord(*catalog, d.qualified_id);
			if(read.status != "ok") { http.Response(409, "Ambiguous Catalog Record"); http.ContentType("application/json") << AsJSON(read.response, true); return; }
			ideas.Add(MapGet(read.response, "record"));
		}
	}
	ValueMap root;
	root.Add("ideas", ideas);
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->data_status);
	root.Add("source_errors", store.GetSourceErrorCount());
	http.ContentType("application/json") << AsJSON(root, true);
}

// ---------------------------------------------------------------------------
// Scientific Paper Mining: paper documents served from the generic DataStore
// (scan root from private runtime configuration). Same projection as the /data/papers page.
// ---------------------------------------------------------------------------
SKYLARK(ApiPaperMining, "api/papers/mining")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectUnavailableCatalogSource(http, *catalog, true)) return;
	const DataStore& store = catalog->data;

	ValueArray papers;
	Vector<const DataDocument*> docs = app.QueryCatalogDocuments(*catalog, "paper");
	Vector<const DataDocument*> analyses = app.QueryCatalogDocuments(*catalog, "paper-analysis");
	for(const DataDocument *document : analyses) docs.Add(document);
	for(int i = 0; i < docs.GetCount(); i++) {
		const DataDocument& d = *docs[i];
		if(d.kind == "paper" || d.kind == "paper-analysis") {
			CatalogRecordRead read = ReadCatalogRecord(*catalog, d.qualified_id);
			if(read.status != "ok") { http.Response(409, "Ambiguous Catalog Record"); http.ContentType("application/json") << AsJSON(read.response, true); return; }
			papers.Add(MapGet(read.response, "record"));
		}
	}
	ValueMap root;
	root.Add("papers", papers);
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->data_status);
	root.Add("source_errors", store.GetSourceErrorCount());
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiProjects, "api/projects")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	ValueArray va;
	const Vector<Project>& projs = local.Projects();
	for(int i = 0; i < projs.GetCount(); i++) {
		const Project& p = projs[i];
		ValueMap jm;
		jm.Add("slug", p.slug);
		jm.Add("title", p.title);
		jm.Add("status", p.status);
		jm.Add("description", p.description);

		Vector<const Phase*> phases = local.PhasesFor(p.slug);
		Vector<const Task*>  tasks  = local.TasksFor(p.slug);
		jm.Add("phases_count", phases.GetCount());
		jm.Add("tasks_count", tasks.GetCount());

		int done = 0;
		for(int t = 0; t < tasks.GetCount(); t++)
			if(tasks[t]->status == "done") done++;
		jm.Add("tasks_done", done);

		va.Add(jm);
	}

	ValueMap root;
	root.Add("projects", va);
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiProjectDetail, "api/project/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	String slug = http[0];

	const Project* p = local.FindProject(slug);
	if(!p) {
		ValueMap err;
		err.Add("error", "Project not found");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	ValueMap root;
	root.Add("slug", p->slug);
	root.Add("title", p->title);
	root.Add("status", p->status);
	root.Add("description", p->description);

	ValueArray phase_arr;
	Vector<const Phase*> phases = local.PhasesFor(slug);
	for(int i = 0; i < phases.GetCount(); i++) {
		const Phase* ph = phases[i];
		ValueMap pm;
		pm.Add("seq", ph->seq);
		pm.Add("title", ph->title);
		pm.Add("status", ph->status);
		pm.Add("description", ph->description);

		ValueArray task_arr;
		Vector<const Task*> tasks = local.TasksForPhase(slug, ph->seq);
		for(int t = 0; t < tasks.GetCount(); t++) {
			const Task* tk = tasks[t];
			ValueMap tm;
			tm.Add("seq", tk->seq);
			tm.Add("title", tk->title);
			tm.Add("status", tk->status);
			tm.Add("instructions", tk->instructions);
			tm.Add("result", tk->result);
			tm.Add("link_next_task", tk->link_next_task);
			task_arr.Add(tm);
		}
		pm.Add("tasks", task_arr);
		phase_arr.Add(pm);
	}
	root.Add("phases", phase_arr);

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiNextTask, "api/tasks/next/*")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();
	String slug = http[0];

	const Project* p = local.FindProject(slug);
	if(!p) {
		ValueMap err;
		err.Add("error", "Project not found");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	Vector<const Phase*> phases = local.PhasesFor(p->slug);
	const Task* next_task = NULL;
	int phase_seq = 0;

	for(int i = 0; i < phases.GetCount(); i++) {
		Vector<const Task*> tasks = local.TasksForPhase(p->slug, phases[i]->seq);
		for(int t = 0; t < tasks.GetCount(); t++) {
			if(tasks[t]->status == "open") {
				next_task = tasks[t];
				phase_seq = phases[i]->seq;
				break;
			}
		}
		if(next_task) break;
	}

	ValueMap root;
	if(next_task) {
		root.Add("found", true);
		root.Add("project", p->slug);
		root.Add("phase_seq", phase_seq);
		root.Add("seq", next_task->seq);
		root.Add("title", next_task->title);
		root.Add("instructions", next_task->instructions);
		root.Add("status", next_task->status);
		root.Add("link_next_task", next_task->link_next_task);
	}
	else {
		root.Add("found", false);
		root.Add("message", "No open tasks in project");
	}

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiClaimTask, "api/tasks/claim:POST_RAW")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	String req = http.GetRequestContent();
	Value v = ParseJSON(req);

	String proj = v["project"];
	String phase = v["phase"];
	String task = v["task"];

	if(proj.IsEmpty() || phase.IsEmpty() || task.IsEmpty()) {
		ValueMap err;
		err.Add("error", "Missing project, phase, or task parameters");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	int rc = CmdSetTask(local, {proj, phase, task, "status", "in-progress"});
	ValueMap res;
	res.Add("success", rc == 0);
	res.Add("status", rc == 0 ? "in-progress" : "failed");
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiDoneTask, "api/tasks/done:POST_RAW")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	ManagerStore& local = app.GetLocalStore();

	String req = http.GetRequestContent();
	Value v = ParseJSON(req);

	String proj = v["project"];
	String phase = v["phase"];
	String task = v["task"];
	String result_txt = v["result"];

	if(proj.IsEmpty() || phase.IsEmpty() || task.IsEmpty()) {
		ValueMap err;
		err.Add("error", "Missing project, phase, or task parameters");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	int rc = CmdSetTask(local, {proj, phase, task, "status", "done"});
	if(rc == 0 && !result_txt.IsEmpty()) {
		CmdSetTask(local, {proj, phase, task, "result", result_txt});
	}

	ValueMap res;
	res.Add("success", rc == 0);
	res.Add("status", rc == 0 ? "done" : "failed");
	res.Add("result", result_txt);
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiGoals, "api/goals")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectUnavailableCatalogSource(http, *catalog, false)) return;
	const V2Store& v2 = catalog->v2;

	ValueArray va;
	const Vector<V2Goal>& goals = v2.GetGoals();
	for(int i = 0; i < goals.GetCount(); i++) {
		CatalogRecordRead read = ReadCatalogRecord(*catalog, goals[i].qualified_id);
		if(read.status != "ok") { http.Response(409, "Ambiguous Catalog Record"); http.ContentType("application/json") << AsJSON(read.response, true); return; }
		ValueMap gm = MapGet(read.response, "record");
		gm.Add("qualified_id", goals[i].qualified_id);
		gm.Add("source_revision", goals[i].source_revision);
		gm.Add("progress", goals[i].GetProgressPercent());
		gm.Add("total_tasks", goals[i].GetTotalTasks());
		gm.Add("done_tasks", goals[i].GetDoneTasks());
		va.Add(gm);
	}

	ValueMap root;
	root.Add("goals", va);
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->v2_status);
	root.Add("source_errors", v2.GetSourceErrorCount());
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiIssues, "api/issues")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectUnavailableCatalogSource(http, *catalog, false)) return;
	const V2Store& v2 = catalog->v2;

	ValueArray va;
	const Vector<V2Issue>& issues = v2.GetIssues();
	for(int i = 0; i < issues.GetCount(); i++) {
		CatalogRecordRead read = ReadCatalogRecord(*catalog, issues[i].qualified_id);
		if(read.status != "ok") { http.Response(409, "Ambiguous Catalog Record"); http.ContentType("application/json") << AsJSON(read.response, true); return; }
		ValueMap im = MapGet(read.response, "record");
		im.Add("qualified_id", issues[i].qualified_id);
		im.Add("source_revision", issues[i].source_revision);
		va.Add(im);
	}

	ValueMap root;
	root.Add("issues", va);
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->v2_status);
	root.Add("source_errors", v2.GetSourceErrorCount());
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiReleases, "api/releases")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	auto catalog = app.PinCatalog();
	if(RejectUnavailableCatalogSource(http, *catalog, false)) return;
	const V2Store& v2 = catalog->v2;

	ValueArray va;
	const Vector<V2Release>& rels = v2.GetReleases();
	for(int i = 0; i < rels.GetCount(); i++) {
		CatalogRecordRead read = ReadCatalogRecord(*catalog, rels[i].qualified_id);
		if(read.status != "ok") { http.Response(409, "Ambiguous Catalog Record"); http.ContentType("application/json") << AsJSON(read.response, true); return; }
		ValueMap rm = MapGet(read.response, "record");
		rm.Add("qualified_id", rels[i].qualified_id);
		rm.Add("source_revision", rels[i].source_revision);
		rm.Add("goals_count", rels[i].goals.GetCount());
		rm.Add("issues_count", rels[i].issues.GetCount());
		va.Add(rm);
	}

	ValueMap root;
	root.Add("releases", va);
	root.Add("catalog_generation", (int64)catalog->generation);
	root.Add("source_revision", catalog->source_revision);
	root.Add("source_status", catalog->v2_status);
	root.Add("source_errors", v2.GetSourceErrorCount());
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiDataRefresh, "api/data/refresh:POST_RAW")
{
	ManagerSkylarkApp& app = ManagerSkylarkApp::Get();
	bool complete = app.RefreshCatalog();
	auto catalog = app.PinLatestCatalog();
	ValueMap result;
	result.Add("success", complete);
	result.Add("catalog_generation", (int64)catalog->generation);
	result.Add("source_revision", catalog->source_revision);
	result.Add("v2_status", catalog->v2_status);
	result.Add("data_status", catalog->data_status);
	ValueArray errors;
	for(const String& diagnostic : catalog->diagnostics) errors.Add(diagnostic);
	result.Add("diagnostics", errors);
	http.ContentType("application/json");
	if(!complete) http.Response(503, "Source Unavailable");
	http << AsJSON(result, true);
}

SKYLARK(ApiSweAgentRun, "api/sweagent/run:POST_RAW")
{
	ValueMap root;
	root.Add("status", "disabled");
	root.Add("available", false);
	root.Add("error", "external_model_dispatch_disabled");
	http.ContentType("application/json");
	http.Response(503, "Service Unavailable");
	http << AsJSON(root, true);
}

SKYLARK(ApiProjectSyncGitHub, "api/project/*/sync-github")
{
	ValueMap response;
	response.Add("status", "disabled");
	response.Add("available", false);
	response.Add("error", "external_publishing_disabled");
	http.ContentType("application/json");
	http.Response(503, "Service Unavailable");
	http << AsJSON(response, true);
}

SKYLARK(ManagerFrameworkSpec, "api/framework/spec")
{
	NavigationModel nav = ManagerSkylarkApp::BuildNavigation();

	ValueMap root;
	root.Add("application", "ManagerSkylark");
	root.Add("version", "2.0.0");
	root.Add("framework", "WebFramework");
	root.Add("theme", "vivid-dark");
	root.Add("density", "comfortable");

	ValueArray cats;
	for(int i = 0; i < nav.GetCount(); i++) {
		const NavCategory& c = nav[i];
		ValueMap cat_m;
		cat_m.Add("id", c.id);
		cat_m.Add("title", c.title);
		cat_m.Add("description", c.description);

		ValueArray sections;
		for(int s = 0; s < c.sections.GetCount(); s++) {
			const NavSection& sec = c.sections[s];
			ValueMap sec_m;
			sec_m.Add("title", sec.title);
			ValueArray items;
			for(int it = 0; it < sec.items.GetCount(); it++) {
				const NavItem& item = sec.items[it];
				ValueMap item_m;
				item_m.Add("id", item.id);
				item_m.Add("title", item.title);
				item_m.Add("url", item.url);
				item_m.Add("description", item.description);
				items.Add(item_m);
			}
			sec_m.Add("items", items);
			sections.Add(sec_m);
		}
		cat_m.Add("sections", sections);
		cats.Add(cat_m);
	}
	root.Add("navigation", cats);

	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(QuitHandler, "quit")
{
	ManagerSkylarkApp::Get().Quit();
	http.ContentType("text/plain") << "Server shutting down\n";
}

// ---------------------------------------------------------------------------
// Paper evaluation is intentionally unavailable until the evidence-backed
// research workflow is implemented. Do not turn arbitrary request text into
// apparently evaluated tasks.
// ---------------------------------------------------------------------------
SKYLARK(ApiEvaluatePaper, "api/tasks/evaluate-paper:POST_RAW")
{
	ValueMap response;
	response.Add("success", false);
	response.Add("available", false);
	response.Add("error", "paper_evaluation_not_implemented");
	http.ContentType("application/json");
	http.Response(501, "Not Implemented");
	http << AsJSON(response, true);
}

// ---------------------------------------------------------------------------
// Schema inference is intentionally unavailable until a versioned schema
// inference and validation pipeline is implemented.
// ---------------------------------------------------------------------------
SKYLARK(ApiGenerateDataSchema, "api/data/schema/generate:POST_RAW")
{
	ValueMap response;
	response.Add("success", false);
	response.Add("available", false);
	response.Add("error", "schema_inference_not_implemented");
	http.ContentType("application/json");
	http.Response(501, "Not Implemented");
	http << AsJSON(response, true);
}

// ---------------------------------------------------------------------------
// Task 3: Generic Workspace Discovery
// Reports documents and diagrams already indexed from the configured source.
// ---------------------------------------------------------------------------
SKYLARK(ApiWorkspaceDiscover, "api/workspace/discover")
{
	if(!IsNull(http["path"])) {
		http.ContentType("application/json");
		http.Response(400, "Bad Request");
		http << "{\"error\": \"Path overrides are not accepted.\"}";
		return;
	}
	auto catalog = ManagerSkylarkApp::Get().PinCatalog();
	if(RejectUnavailableCatalogSource(http, *catalog, true)) return;
	const DataStore& store = catalog->data;
	if(catalog->data_status == "load_error") {
		http.ContentType("application/json");
		http.Response(503, "Service Unavailable");
		http << "{\"success\": false, \"error\": \"Source store is unavailable.\"}";
		return;
	}
	ValueArray files;
	for(int i = 0; i < store.GetDocuments().GetCount(); i++)
		files.Add(store.GetDocuments()[i].rel_path);
	ValueArray diagrams;
	for(int i = 0; i < store.GetDiagramSources().GetCount(); i++)
		diagrams.Add(store.GetDiagramSources()[i]);

	ValueMap response;
	response.Add("success", true);
	response.Add("catalog_generation", (int64)catalog->generation);
	response.Add("source_revision", catalog->source_revision);
	response.Add("source_status", catalog->data_status);
	response.Add("source_errors", store.GetSourceErrorCount());
	response.Add("source_id", store.GetSourceId());
	response.Add("read_only", store.IsReadOnly());
	response.Add("files", files);
	response.Add("diagrams", diagrams);
	response.Add("count", files.GetCount());
	response.Add("diagram_count", diagrams.GetCount());
	http.ContentType("application/json") << AsJSON(response, true);
}

// ---------------------------------------------------------------------------
// Automated Self-Test Verification Mode
// ---------------------------------------------------------------------------

bool ManagerSkylarkApp::RunSelfTests(int port)
{
	Cout() << "=== Running ManagerSkylark Automated Verification ===\n";
	if(!ScopedPolicyService::RunSyntheticTests()) {
		Cerr() << "FAILED: scoped policy synthetic security matrix\n";
		return false;
	}
	Cout() << "ok    scoped policy fail-closed, precedence, lineage and redaction fixtures\n";
	DataWriteAuthorization evidence_test_authorization;
	evidence_test_authorization.authenticated = true;
	evidence_test_authorization.action_scope = "record:write";
	evidence_test_authorization.principal = "synthetic-evidence-test";
	evidence_test_authorization.source_id = "synthetic-evidence-source";
	evidence_test_authorization.workspace_id = "test-workspace";
	DataWriteAuthorization evidence_wrong_workspace_authorization = evidence_test_authorization;
	evidence_wrong_workspace_authorization.workspace_id = "other-test-workspace";
	if(!EvidenceIngestion::RunSyntheticTests(evidence_test_authorization, evidence_wrong_workspace_authorization)) {
		Cerr() << "FAILED: evidence ingestion synthetic validation\n";
		return false;
	}
	if(!PaperSourceImport::RunSyntheticTests()) {
		Cerr() << "FAILED T73 synthetic paper source validation\n";
		return false;
	}
	if(!CurlPaperTransport::RunSyntheticTests() || !RunPaperTransportTests()) {
		Cerr() << "FAILED T81 synthetic HTTPS transport validation\n";
		return false;
	}
	Cout() << "ok    T81 fake-only URL/IP, curl argv, redirects, status/MIME, size, timeout and DNS rejection matrix\n";
	Cout() << "ok    T73 canonical URL/redirect and IP/local-host rejection; peer unobserved, default rules/transport absent\n";
	Cout() << "ok    typed evidence locators, pinned content, policy lineage and workspace authorization\n";
	if(!ReviewPackage::RunSyntheticTests()) { Cerr() << "FAILED T82 pure diff/path/artifact/hash matrix\n"; return false; }
	// T82 consumes only caller-supplied synthetic snapshots and an isolated store.
	{
		const String ws = "t82-workspace", source = "t82-source";
		SelfTestWorkspace t82_workspace;
		String root = t82_workspace.FixtureRoot("t82-review-package");
		if(!CreatePrivateDirectory(root)) { Cerr() << "FAILED T82 isolated root create\n"; return false; }
		String source_fixture = AppendFileName(root, "source-fixture.cpp");
		const String source_fixture_bytes = "synthetic source fixture must remain byte-identical\n";
		if(!SaveFile(source_fixture, source_fixture_bytes)) return false;
		DataStore store; if(!store.LoadWriteFixtureForTests(root, source)) { Cerr() << "FAILED T82 isolated store registration\n"; return false; }
		DataWriteAuthorization allowed; allowed.authenticated = true; allowed.action_scope = "record:write"; allowed.principal = "t82-synthetic"; allowed.source_id = source; allowed.workspace_id = ws;
		DataWriteAuthorization denied = allowed; denied.workspace_id = "other-t82-workspace";
		ReviewPackageInput input; input.workspace_id = ws; input.goal_id = "goal-a"; input.scope_id = "scope-a"; input.base_revision = "base-r1"; input.head_revision = "head-r1"; input.author = allowed.GetPrincipal(); input.author_state = "known-local-author"; input.capture_state = "captured"; input.selected_paths.Add("src/a.cpp");
		input.patch_bytes = "diff --git a/src/a.cpp b/src/a.cpp\nindex 1111111..2222222 100644\n--- a/src/a.cpp\n+++ b/src/a.cpp\n@@ -1 +1 @@\n-old\n+new\n";
		ReviewArtifact artifact; artifact.path = "assets/synthetic-blob.bin"; artifact.content_sha256 = String('a', 64); artifact.byte_count = 4096; artifact.hash_only = true; input.artifacts.Add(artifact);
		ReviewPackagePreview first = ReviewPackage::Capture(store, input, allowed, "t82-op-r1");
		if(first.status != "committed" || first.package_id.IsEmpty() || first.revision_id.IsEmpty() || first.patch_sha256 != DataStore::ContentDigest(input.patch_bytes)) { ReviewPackageInput plain = input; plain.artifacts.Clear(); ReviewPackagePreview plain_result = ReviewPackage::Capture(store, plain, allowed, "t82-op-plain"); Cerr() << "FAILED T82 first capture: " << first.status << " plain=" << plain_result.status << " schema=" << (store.FindSchemaCard("review-package-revision") != NULL) << "\n"; return false; }
		if(store.RecoverPendingBatches(allowed).status != "recovered") { Cerr() << "FAILED T82 journal recovery\n"; return false; }
		DataStore reloaded; if(!reloaded.LoadWriteFixtureForTests(root, source)) { Cerr() << "FAILED T82 reload after create\n"; return false; }
		ReviewPackagePreview retry = ReviewPackage::Capture(reloaded, input, allowed, "t82-op-r1");
		ReviewPackageInput changed = input; changed.patch_bytes = "diff --git a/src/a.cpp b/src/a.cpp\nindex 1111111..2222222 100644\n--- a/src/a.cpp\n+++ b/src/a.cpp\n@@ -1 +1 @@\n-old\n+changed\n";
		String changed_status = ReviewPackage::Capture(reloaded, changed, allowed, "t82-conflict").status;
		if(retry.status != "already_committed" || changed_status != "revision_conflict") { Cerr() << "FAILED T82 retry/conflict: " << retry.status << "/" << changed_status << "\n"; return false; }
		ReviewPackageInput next = input; next.previous_revision = first.revision_id; next.head_revision = "head-r2"; next.patch_bytes = "diff --git a/src/a.cpp b/src/a.cpp\nindex 1111111..3333333 100644\n--- a/src/a.cpp\n+++ b/src/a.cpp\n@@ -1 +1 @@\n-old\n+newer\n";
		ReviewPackagePreview second = ReviewPackage::Capture(reloaded, next, allowed, "t82-op-r2");
		if(second.status != "committed" || reloaded.RecoverPendingBatches(allowed).status != "recovered" || !reloaded.LoadWriteFixtureForTests(root, source)) { Cerr() << "FAILED T82 appended revision recovery\n"; return false; }
		ReviewPackageInput exact_size = input; exact_size.previous_revision = second.revision_id; exact_size.head_revision = "head-exact-size";
		String exact_prefix = "diff --git a/src/a.cpp b/src/a.cpp\n--- a/src/a.cpp\n+++ b/src/a.cpp\n@@ -0,0 +1 @@\n+";
		String exact_suffix = "\n";
		exact_size.patch_bytes = exact_prefix + String('x', ReviewPackage::MAX_PATCH_BYTES - exact_prefix.GetCount() - exact_suffix.GetCount()) + exact_suffix;
		if(exact_size.patch_bytes.GetCount() != ReviewPackage::MAX_PATCH_BYTES) { Cerr() << "FAILED T82 exact-size fixture construction\n"; return false; }
		ReviewPackagePreview max_patch = ReviewPackage::Capture(reloaded, exact_size, allowed, "t82-max-patch");
		if(max_patch.status != "committed" || reloaded.RecoverPendingBatches(allowed).status != "recovered" || !reloaded.LoadWriteFixtureForTests(root, source)) { Cerr() << "FAILED T82 exact patch-size capture\n"; return false; }
		const DataDocument *max_record = reloaded.FindDocument(max_patch.revision_id);
		if(!max_record || MapGetString(max_record->data, "patch_sha256") != DataStore::ContentDigest(exact_size.patch_bytes) || MapGetString(max_record->data, "head_revision") != exact_size.head_revision) { Cerr() << "FAILED T82 exact patch-size persistence\n"; return false; }
		int before_oversize = reloaded.GetDocuments().GetCount();
		ReviewPackageInput over_size = exact_size; over_size.previous_revision = max_patch.revision_id; over_size.head_revision = "head-over-size"; over_size.patch_bytes.Cat('x');
		ReviewPackagePreview too_large = ReviewPackage::Capture(reloaded, over_size, allowed, "t82-over-patch");
		if(too_large.status != "invalid" || reloaded.GetDocuments().GetCount() != before_oversize) { Cerr() << "FAILED T82 over-limit patch wrote or was accepted\n"; return false; }
		String first_bytes_before = AsJSON(reloaded.FindDocument(first.revision_id)->data, true);
		ReviewPackageInput fork = input; fork.previous_revision = first.revision_id; fork.head_revision = "head-fork";
		ReviewPackageInput stale = input; stale.previous_revision = "missing-stale-parent"; stale.head_revision = "head-stale";
		ReviewPackageInput bad = input; bad.selected_paths.Add("src/untracked.cpp");
		ReviewPackageInput unsafe = input; unsafe.selected_paths.Clear(); unsafe.selected_paths.Add("../secret");
		ReviewPackageInput unselected = input; unselected.patch_bytes += "diff --git a/other.cpp b/other.cpp\n--- a/other.cpp\n+++ b/other.cpp\n";
		ReviewPackageInput unavailable = input; unavailable.base_available = false;
		ReviewPackageInput unknown_base = input; unknown_base.base_state = "unknown";
		ReviewPackageInput malformed = input; malformed.patch_bytes = "not a unified diff\n";
		ReviewPackageInput oversized = input; oversized.patch_bytes = String('x', ReviewPackage::MAX_PATCH_BYTES + 1);
		ReviewPackageInput bad_artifact = input; bad_artifact.artifacts[0].hash_only = false;
		ReviewPackagePreview denied_result = ReviewPackage::Capture(reloaded, bad, denied, "t82-denied");
		int before_rejections = reloaded.GetDocuments().GetCount();
		ReviewPackagePreview blocked_preview = ReviewPackage::Capture(reloaded, unavailable, allowed, "t82-base");
		String fork_status = ReviewPackage::Capture(reloaded, fork, allowed, "t82-fork").status;
		String stale_status = ReviewPackage::Capture(reloaded, stale, allowed, "t82-stale").status;
		if(second.status != "committed" || MapGetString(reloaded.FindDocument(second.revision_id)->data, "previous_revision") != first.revision_id ||
		   ReviewPackage::Capture(reloaded, bad, allowed, "t82-untracked").status != "invalid_patch" ||
		   ReviewPackage::Capture(reloaded, unsafe, allowed, "t82-unsafe").status != "invalid" ||
		   ReviewPackage::Capture(reloaded, unselected, allowed, "t82-unselected").status != "invalid_patch" ||
		   blocked_preview.status != "blocked_base_unavailable" || !blocked_preview.record.IsEmpty() || !blocked_preview.package_id.IsEmpty() ||
		   ReviewPackage::Capture(reloaded, unknown_base, allowed, "t82-base-unknown").status != "blocked_base_unavailable" ||
		   fork_status != "revision_conflict" || stale_status != "revision_conflict" ||
		   ReviewPackage::Capture(reloaded, malformed, allowed, "t82-malformed").status != "invalid_patch" ||
		   ReviewPackage::Capture(reloaded, oversized, allowed, "t82-oversized").status != "invalid" ||
		   ReviewPackage::Capture(reloaded, bad_artifact, allowed, "t82-artifact-type").status != "invalid" || denied_result.status != "denied" || LoadFile(source_fixture) != source_fixture_bytes || reloaded.GetDocuments().GetCount() != before_rejections) { Cerr() << "FAILED T82 revision/matrix: " << second.status << " parent=" << MapGetString(reloaded.FindDocument(second.revision_id)->data, "previous_revision") << " fork=" << fork_status << " stale=" << stale_status << " denied=" << denied_result.status << " source=" << (LoadFile(source_fixture) == source_fixture_bytes) << "\n"; return false; }
		String serialized = AsJSON(reloaded.FindDocument(first.revision_id)->data, true);
		if(serialized != first_bytes_before || serialized.Find(DataStore::ContentDigest(input.patch_bytes)) < 0 || serialized.Find("patch_bytes") < 0 || artifact.hash_only != true) return false;
		Cout() << "ok    T82 hunk-validated diffs, CRLF, new/deleted files, hash-only binary, exact-size persistence, over-limit no-write and revision lineage\n";
	}
	// T83 keeps the shared headless projection identical to the HTML renderer and
	// verifies that hostile discussion text is never interpreted as markup.
	{
		SelfTestWorkspace t83_workspace; String root=t83_workspace.FixtureRoot("t83-review-redaction");
		if(!CreatePrivateDirectory(root)) { Cerr()<<"FAILED T83 isolated review root\n"; return false; }
		ValueMap patch_record; patch_record.Add("id","t83-secret-revision-id"); patch_record.Add("kind","review-package-revision"); patch_record.Add("workspace_id","t83-workspace"); patch_record.Add("patch_bytes","T83_PATCH_SECRET_SENTINEL");
		ValueMap comment_record; comment_record.Add("id","t83-secret-comment-id"); comment_record.Add("kind","review-comment"); comment_record.Add("workspace_id","t83-workspace"); comment_record.Add("body","T83_COMMENT_SECRET_SENTINEL");
		if(!SaveFile(AppendFileName(root,"patch.json"),AsJSON(patch_record,true))||!SaveFile(AppendFileName(root,"comment.json"),AsJSON(comment_record,true))) return false;
		One<DataStore> data; data.Create(); One<V2Store> v2; v2.Create();
		if(!data->LoadFixtureForTests(root)) { Cerr()<<"FAILED T83 isolated review store load\n"; return false; }
		One<DataStore> data_owner=pick(data); One<V2Store> v2_owner=pick(v2);
		CatalogGeneration generation(1,std::move(v2_owner),std::move(data_owner),true,true);
		for(const String& id:{String("t83-secret-revision-id"),String("t83-secret-comment-id")}) {
			CatalogRecordRead generic=ReadCatalogRecord(generation,"t83-workspace:"+id,true);
			String response=AsJSON(generic.response,true);
			if(generic.status!="missing"||response.Find(id)>=0||response.Find("T83_PATCH_SECRET_SENTINEL")>=0||response.Find("T83_COMMENT_SECRET_SENTINEL")>=0) { Cerr()<<"FAILED T83 generic isolated record redaction\n"; return false; }
		}
		ValueMap comment; comment.Add("author","synthetic-reviewer"); comment.Add("record_type","comment"); comment.Add("body","<img src=x onerror=alert(1)>"); comment.Add("stale",true);
		ValueArray comments; comments.Add(comment);
		ValueMap package; package.Add("id","review-r-t83"); package.Add("package_id","review-p-t83"); package.Add("base_revision","base-t83"); package.Add("head_revision","head-t83"); package.Add("patch_sha256",DataStore::ContentDigest("synthetic patch sentinel"));
		ValueMap detail; detail.Add("view_state","ready"); detail.Add("package",package); detail.Add("patch_text","synthetic patch sentinel"); detail.Add("comments",comments);
		String rendered=ReviewPackageViewService::Render(detail,true), serialized=AsJSON(detail,true);
		if(rendered.Find("<img src=x")>=0||rendered.Find("&lt;img src=x")<0||rendered.Find("stale revision")<0||serialized.Find("review-r-t83")<0||
		   !ReviewDiscussionService::IsPrivateKind("review-package-revision")||!ReviewDiscussionService::IsPrivateKind("review-thread")||!ReviewDiscussionService::IsPrivateKind("review-comment")||ReviewDiscussionService::IsPrivateKind("paper")) {
			Cerr()<<"FAILED T83 shared review rendering/redaction contract\n"; return false;
		}
		Cout()<<"ok    T83 isolated patch/comment generic lookup redaction, shared view escaping, stale label, and private-kind classification\n";
	}
	// T84 exercises provenance and typed navigation rendering without persistent data.
	{
		ValueMap package; package.Add("id","t84-revision"); package.Add("package_id","t84-package"); package.Add("workspace_id","t84-workspace"); package.Add("author","<img src=x>"); package.Add("author_state","known"); package.Add("capture_state","captured"); package.Add("goal_id","t84-goal"); package.Add("author_kind","not-package-field");
		ValueMap goal; goal.Add("id","t84-goal"); goal.Add("title","Visible goal"); goal.Add("url","/goal/t84-repo/t84-goal?workspace=t84-workspace&package=t84-package&revision=t84-revision");
		ValueMap topology; topology.Add("id","t84-topology"); topology.Add("title","Visible topology"); topology.Add("url","/data/diagram/scope/t84-scope?workspace=t84-workspace&package=t84-package&revision=t84-revision");
		ValueMap evidence; evidence.Add("id","t84-evidence"); evidence.Add("title","Evidence item"); evidence.Add("snapshot_id","t84-snapshot"); evidence.Add("snapshot_revision","snapshot-r2"); evidence.Add("snapshot_source_revision","snapshot-source-r2"); evidence.Add("evidence_revision","2"); evidence.Add("evidence_source_revision","item-source-r2"); evidence.Add("upstream_revision","source-r2"); ValueMap locator; locator.Add("type","paper-section"); locator.Add("line",7); evidence.Add("locator",locator); evidence.Add("excerpt","<script>evidence</script>"); evidence.Add("anchor","review-evidence-t84-evidence");
		ValueArray goals,topologies,evidence_items; goals.Add(goal); topologies.Add(topology); evidence_items.Add(evidence);
		package.Add("goal_links",goals); package.Add("topology_links",topologies); package.Add("evidence_items",evidence_items);
		ValueMap check; check.Add("id","t84-check"); check.Add("kind","assessment"); check.Add("title","Visible check"); check.Add("status","passed"); check.Add("url","/data/doc/t84-workspace%3At84-check?workspace=t84-workspace&package=t84-package&revision=t84-revision"); ValueArray checks; checks.Add(check);
		ValueArray comments;
		for(const String& kind:{String("human"),String("agent"),String("external")}) { ValueMap c; c.Add("author_kind",kind); c.Add("author","<b>author</b>"); c.Add("record_type",kind=="external"?String("external-evidence"):kind=="agent"?String("agent-note"):String("comment")); c.Add("body","<script>body</script>"); if(kind=="external") c.Add("evidence_items",evidence_items); comments.Add(c); }
		ValueMap view; view.Add("view_state","ready"); view.Add("package",package); view.Add("comments",comments); view.Add("check_summaries",checks); view.Add("patch_text","synthetic");
		String html=ReviewPackageViewService::Render(view,true),json=AsJSON(view,true);
		if(html.Find("Author: &lt;img src=x&gt;")<0||html.Find("author state: known")<0||html.Find("capture state: captured")<0||html.Find("Human comment")<0||html.Find("Agent note")<0||html.Find("External evidence")<0||html.Find("/goal/t84-repo/t84-goal")<0||html.Find("/data/diagram/scope/t84-scope")<0||html.Find("Visible check")<0||html.Find("href=\"#review-evidence-t84-evidence\"")<0||html.Find("id=\"review-evidence-t84-evidence\"")<0||html.Find("snapshot-source-r2")<0||html.Find("item-source-r2")<0||html.Find("&lt;script&gt;evidence")<0||html.Find("<script>evidence")>=0||html.Find("<script>body")>=0||json.Find("author_kind")<0||json.Find("external-evidence")<0||json.Find("/data/doc/t84-workspace%3At84-check?workspace=t84-workspace")<0) { Cerr()<<"FAILED T84 renderer provenance/link/escaping contract: "<<html<<"\n"; return false; }
		ValueMap redacted; redacted.Add("unavailable",true); ValueArray unavailable; unavailable.Add(redacted); package.Set("evidence_items",unavailable); package.Set("topology_links",unavailable); package.Set("goal_links",unavailable); view.Set("package",package); view.Set("comments",ValueArray()); view.Set("check_summaries",unavailable);
		String safe=ReviewPackageViewService::Render(view,true);
		if(safe.Find("Related record unavailable")<0||safe.Find("review-evidence-t84-evidence")>=0||safe.Find("href=\"/data/diagram/t84-workspace")>=0||safe.Find("href=\"/goal/t84-workspace")>=0||safe.Find("/data/doc/")>=0) { Cerr()<<"FAILED T84 unavailable reference redaction\n"; return false; }
		Cout()<<"ok    T84 provenance labels, contextual typed links, evidence locator rendering, escaping and generic unavailable marker\n";
	}
	// T15 synthetic in-memory adapter coverage; no source or private store is read.
	{
		DataWriteAuthorization allowed, denied;
		allowed.authenticated = true; allowed.action_scope = "record:write";
		allowed.principal = "synthetic-t15"; allowed.source_id = "synthetic-t15-source";
		allowed.workspace_id = "synthetic-t15-workspace";
		denied = allowed; denied.workspace_id = "other-synthetic-workspace";
		if(!HistoricalAssessmentMigration::RunSyntheticTests(allowed, denied)) {
			Cerr() << "FAILED: T15 synthetic historical assessment mapping\n";
			return false;
		}
		Cout() << "ok    T15 synthetic historical assessment mapping, provenance unknowns, conflicts and authorization redaction\n";
	}
	// T61 is an in-memory deterministic projection fixture; no DataStore or
	// ambient source is consulted.
	if(!AssessmentService::RunSyntheticTests()) {
		Cerr() << "FAILED: T61 synthetic deterministic assessment projection\n";
		return false;
	}
	if(!CandidateWorkflow::RunSyntheticTests()) {
		Cerr() << "FAILED: T65 validated proposal and gated workflow checks\n";
		return false;
	}
	if(!JobService::RunSyntheticTests()) {
		Cerr() << "FAILED: T70 synthetic durable job schema/bounds checks\n";
		return false;
	}
	Cout() << "ok    T70 versioned durable job/event/run schemas, bounded request shape and stable request digest\n";
	Cout() << "ok    T61 deterministic assessment projection, generic rubric and bounded deduplication\n";
	SelfTestWorkspace workspace;
	String stable_fixture = workspace.FixtureRoot("helper-stable-check");
	String stable_fixture_again = workspace.FixtureRoot("helper-stable-check");
	String different_fixture = workspace.FixtureRoot("helper-different-check");
	if(stable_fixture != stable_fixture_again || stable_fixture == different_fixture ||
	   stable_fixture.Find(workspace.fixtures) != 0 ||
	   stable_fixture.GetCount() <= workspace.fixtures.GetCount()) {
		Cerr() << "FAILED sharded fixture root mapping and containment\n";
		return false;
	}
	CurrentDirectoryGuard cwd;
	if(!cwd.Enter(workspace.root))
		return false;
	// T73 writer integration uses only synthetic policy, license evidence and paper bytes.
	{
		String root=workspace.FixtureRoot("t73-paper-import");
		if(!CreatePrivateDirectory(root)) return false;
		const String ws="t73-workspace", source_id="t73-source";
		DataWriteAuthorization auth; auth.authenticated=true; auth.action_scope="record:write"; auth.principal="t73-synthetic"; auth.source_id=source_id; auth.workspace_id=ws;
		DataStore store; if(!store.LoadWriteFixtureForTests(root,source_id)) return false;
		ValueMap license_snapshot; license_snapshot.Add("id","t73-license-snapshot"); license_snapshot.Add("kind","evidence-snapshot"); license_snapshot.Add("workspace_id",ws); license_snapshot.Add("source_kind","paper"); license_snapshot.Add("source_identity","t73-license-source"); license_snapshot.Add("upstream_revision","license-r1"); license_snapshot.Add("previous_revision",""); license_snapshot.Add("content_sha256",DataStore::ContentDigest("synthetic license terms")); license_snapshot.Add("captured_at","2026-01-01T00:00:00Z"); license_snapshot.Add("source_at","2026-01-01T00:00:00Z"); license_snapshot.Add("collector","synthetic"); ValueMap license_locator; license_locator.Add("type","paper-section"); license_locator.Add("id","t73-license-source"); license_locator.Add("page",1); license_locator.Add("section","terms"); license_snapshot.Add("locator",license_locator); license_snapshot.Add("resolution","resolved"); license_snapshot.Add("content","synthetic license terms"); license_snapshot.Add("classification","public"); license_snapshot.Add("visibility","visible"); license_snapshot.Add("retracted",false); license_snapshot.Add("predecessor","none"); license_snapshot.Add("contributors",ValueArray());
		ValueMap license_snapshot_env; license_snapshot_env.Add("schema_id","skylark/evidence-snapshot"); license_snapshot_env.Add("schema_version","2"); license_snapshot_env.Add("workspace_id",ws); license_snapshot_env.Add("stable_id","t73-license-snapshot"); license_snapshot_env.Add("record_kind","evidence-snapshot"); license_snapshot_env.Add("revision","1"); license_snapshot_env.Add("visibility","private"); license_snapshot_env.Add("provenance_ref","synthetic-t73"); license_snapshot.Add("envelope",license_snapshot_env);
		ValueMap license_item; license_item.Add("id","t73-license-evidence"); license_item.Add("kind","evidence-item"); license_item.Add("workspace_id",ws); license_item.Add("snapshot","t73-license-snapshot"); license_item.Add("source_identity","t73-license-source"); license_item.Add("upstream_revision","license-r1"); license_item.Add("content_sha256",DataStore::ContentDigest("synthetic license terms")); license_item.Add("locator",license_locator); license_item.Add("resolution","resolved"); license_item.Add("excerpt","synthetic license terms"); license_item.Add("classification","public"); license_item.Add("visibility","visible"); license_item.Add("contributors",ValueArray());
		ValueMap license_item_env=license_snapshot_env; license_item_env.Set("schema_id","skylark/evidence-item"); license_item_env.Set("stable_id","t73-license-evidence"); license_item_env.Set("record_kind","evidence-item"); license_item.Add("envelope",license_item_env);
		DataBatchChange ls,li; ls.action=li.action=DATA_BATCH_CREATE; ls.qualified_id=ws+":t73-license-snapshot"; ls.proposed=license_snapshot; li.qualified_id=ws+":t73-license-evidence"; li.proposed=license_item; Vector<DataBatchChange> seed; seed.Add(ls); seed.Add(li);
		if(store.PublishBatch(auth,"t73-license-seed",seed).status!="committed" || store.RecoverPendingBatches(auth).status!="recovered") return false;
		TrustedPolicyRegistry policy; policy.configured=true; PolicyWorkspaceFact wf; wf.identity.workspace_id=ws; wf.identity.object_id="workspace"; policy.workspaces.Add(wf); PolicySourceFact sf; sf.workspace=wf.identity; sf.identity.workspace_id=ws; sf.identity.object_id="t73-paper"; sf.classification=POLICY_PUBLIC; sf.visible=true; policy.sources.Add(sf);
		PublicPaperSourceRule rule; rule.rule_id="t73-rule"; rule.hostname="papers.example.invalid"; rule.path_prefix="/approved/"; rule.allowed_license_ids.Add("license-synthetic"); rule.max_bytes=1024; rule.max_sections=4; rule.internal_use_mode="internal-only"; Vector<PublicPaperSourceRule> rules; rules.Add(rule);
		PaperFetchSnapshot value; value.workspace_id=ws; value.source_identity="t73-paper"; value.external_version="v1"; value.source_url="https://papers.example.invalid/approved/paper"; value.final_url=value.source_url; value.license_id="license-synthetic"; value.license_evidence_ref="t73-license-evidence"; value.source_bytes="synthetic paper bytes"; value.source_sha256=DataStore::ContentDigest(value.source_bytes); value.captured_at="2026-01-01T00:00:00Z"; value.extractor="synthetic-extractor"; value.extractor_version="v1"; value.config_revision="c1"; value.mime_type="application/pdf"; PaperFetchSection section; section.section="abstract"; section.page=1; section.text="synthetic extracted section"; section.locator="abstract:p1"; value.sections.Add(section);
		PaperSourceImportResult imported=PaperSourceImport::Import(store,rules,policy,value,auth);
		if(imported.status!="committed" || imported.record_ids.GetCount()!=3 || store.RecoverPendingBatches(auth).status!="recovered") { Cerr()<<"FAILED T73 authorized import/recovery: "<<imported.status<<"\n"; return false; }
		DataStore reloaded; if(!reloaded.LoadWriteFixtureForTests(root,source_id)) return false;
		PaperSourceImportResult repeated=PaperSourceImport::Import(reloaded,rules,policy,value,auth);
		PaperFetchSnapshot changed=value; changed.source_bytes="different synthetic bytes"; changed.source_sha256=DataStore::ContentDigest(changed.source_bytes);
		PaperSourceImportResult conflict=PaperSourceImport::Import(reloaded,rules,policy,changed,auth);
		DataWriteAuthorization denied=auth; denied.workspace_id="other-t73-workspace";
		PaperSourceImportResult denied_import=PaperSourceImport::Import(reloaded,rules,policy,value,denied);
		bool transport_disabled=false, peer_unobserved=false;
		for(const DataDocument& d:reloaded.GetDocuments()) if(d.kind=="paper-source-snapshot") { transport_disabled=MapGet(d.data,"transport_installed")==false&&MapGet(d.data,"request_performed")==false; peer_unobserved=MapGetString(d.data,"peer_address_state")=="not_observed_transport_disabled"; }
		if(repeated.status!="already_committed" || conflict.status!="revision_conflict" || denied_import.status!="denied" || reloaded.GetDocuments().GetCount()!=5 || !transport_disabled || !peer_unobserved) { Cerr()<<"FAILED T73 repeat/conflict/scope: "<<repeated.status<<"/"<<conflict.status<<"/"<<denied_import.status<<" count="<<reloaded.GetDocuments().GetCount()<<"\n"; return false; }
		PolicySourceFact transported_source=sf; transported_source.identity.object_id="t73-transport"; policy.sources.Add(transported_source);
		PublicPaperSourceRule transport_rule=rule; transport_rule.hostname="papers.synthetic-example.org"; Vector<PublicPaperSourceRule> transport_rules; transport_rules.Add(transport_rule);
		String transport_url="https://papers.synthetic-example.org/approved/paper";
		SyntheticPaperHttpRunner fake_process; fake_process.Add("HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\nContent-Length: 21\r\n\r\nsynthetic paper bytes");
		SyntheticPaperDnsResolver fake_dns; fake_dns.addresses.Add("8.8.8.8");
		CurlPaperTransport fake_transport(transport_rule,"/usr/bin/curl",fake_process,fake_dns); PaperTransportResponse fetched; String fetch_error;
		if(!fake_transport.Fetch(transport_url,fetched,fetch_error)) { Cerr()<<"FAILED T73 fake verified fetch\n"; return false; }
		PaperFetchSnapshot verified=value; verified.source_identity="t73-transport"; verified.source_url=transport_url; verified.source_bytes=fetched.body; verified.source_sha256=DataStore::ContentDigest(verified.source_bytes); verified.final_url=fetched.final_url; verified.redirect_chain=clone(fetched.redirects); verified.transport_attestation=fetched.attestation;
		PaperSourceImportResult verified_import=PaperSourceImport::Import(reloaded,transport_rules,policy,verified,auth);
		if(verified_import.status=="committed") reloaded.RecoverPendingBatches(auth);
		DataStore verified_store; if(!verified_store.LoadWriteFixtureForTests(root,source_id)) return false;
		bool persisted_verified=false, leaked_peer=false;
	for(const DataDocument& d:verified_store.GetDocuments()) if(d.kind=="paper-source-snapshot"&&MapGetString(d.data,"source_identity")=="t73-transport") { persisted_verified=MapGet(d.data,"transport_installed")==true&&MapGet(d.data,"request_performed")==true&&MapGetString(d.data,"peer_address_state")=="public_dns_pinned_tls_verified"&&MapGetString(d.data,"transport_verification_state")=="tls_peer_and_hostname_verified"; leaked_peer=AsJSON(d.data,true).Find("8.8.8.8")>=0; }
		PaperFetchSnapshot forged=verified; forged.peer_address_claim="8.8.8.8";
		PaperFetchSnapshot altered=verified; altered.source_bytes+="tampered"; altered.source_sha256=DataStore::ContentDigest(altered.source_bytes);
		if(verified_import.status!="committed"||!persisted_verified||leaked_peer||PaperSourceImport::Import(verified_store,transport_rules,policy,forged,auth).status!="invalid"||PaperSourceImport::Import(verified_store,transport_rules,policy,altered,auth).status!="invalid") { Cerr()<<"FAILED T73 verified transport attestation handoff: "<<verified_import.status<<" persisted="<<persisted_verified<<" peer="<<leaked_peer<<"\n"; return false; }
		Cout()<<"ok    T73 authorized synthetic import, same-workspace license reference, atomic batch recovery, exact retry, changed-byte conflict and workspace denial\n";
		Cout()<<"ok    T73 fake verified transport attestation persistence, body binding and peer-claim rejection\n";
	}
	// T76 uses only the records emitted above by the T73 synthetic adapter.
	{
		ManagerSkylarkApp *prior_t76_instance=s_instance;
		ManagerSkylarkApp t76_app(workspace.root);
		struct FakePaperAnalyzer : PaperCandidateGenerator {
			int calls=0; String revision="fake-r1", provider_revision="t76-provider-r1", title="Validate the pinned section locator", evidence_id; bool supported=true; bool forged=false, missing=false, counterevidence=false, malformed=false;
			String GetGeneratorId() const override { return "t76-fake"; }
			String GetGeneratorRevision() const override { return revision; }
			String GetProviderConfigRevision() const override { return provider_revision; }
			bool Generate(const ValueMap&, Vector<PaperResearchClaim>& out) override {
				calls++; PaperResearchClaim c; c.title=title; c.purpose="Use source sections to verify a bounded locator."; c.quality_contract="The locator resolves to the exact extracted section."; c.inputs="A pinned paper section identifier."; c.outputs="A validated locator result."; c.runtime_resources="Local deterministic execution."; c.assets="The imported paper section."; c.verification_plan="Compare the returned locator to the pinned evidence item."; c.uncertainty="The synthetic fixture does not measure runtime behavior."; c.supported=supported; if(!missing) c.evidence_ids.Add(malformed?String(257,'x'):forged?"forged-evidence-id":evidence_id); if(counterevidence) c.counterevidence_ids.Add(malformed?String(257,'x'):evidence_id); out.Add(c); return true;
			}
		} analyzer;
		String t76_root=workspace.FixtureRoot("t73-paper-import");
		DataStore t76_store; if(!t76_store.LoadWriteFixtureForTests(t76_root,"t73-source")) { Cerr()<<"FAILED T76 initial T73 fixture reload\n"; return false; }
		const String ws="t73-workspace", source_identity="t73-paper", provider_id="t76-synthetic-provider";
		const DataDocument *paper=NULL; ValueArray selected_evidence;
		for(const DataDocument& d:t76_store.GetDocuments()) {
			if(d.kind=="paper-source-snapshot" && d.workspace_id==ws && MapGetString(d.data,"source_identity")==source_identity) paper=&d;
			if(d.kind=="evidence-item" && d.workspace_id==ws && MapGetString(d.data,"source_identity")==source_identity) selected_evidence.Add(d.id);
		}
		if(!paper || selected_evidence.IsEmpty()) { Cerr()<<"FAILED T76 T73 fixture source/evidence unavailable\n"; return false; }
		analyzer.evidence_id=selected_evidence[0];
		String paper_id=paper->id, paper_revision=MapGetString(paper->data,"upstream_revision"), extraction=MapGetString(paper->data,"extraction_sha256");
		TrustedPolicyRegistry registry; registry.configured=true; PolicyWorkspaceFact workspace_fact; workspace_fact.identity.workspace_id=ws; workspace_fact.identity.object_id=ws; registry.workspaces.Add(workspace_fact);
		for(const String& identity:{source_identity,String("t73-license-source"),String("t73-paper-disallowed")}) { PolicySourceFact fact; fact.workspace=workspace_fact.identity; fact.identity.workspace_id=ws; fact.identity.object_id=identity; fact.classification=POLICY_PUBLIC; fact.visible=true; registry.sources.Add(fact); }
		PolicyProviderFact provider; provider.workspace=workspace_fact.identity; provider.identity.workspace_id=ws; provider.identity.object_id=provider_id; provider.training_policy="eligible"; provider.enabled=true; provider.config_revision="t76-provider-r1"; provider.secret_reference="secretref:t76-synthetic"; registry.providers.Add(provider);
		PolicySettings enabled; enabled.configured=true; enabled.remote_enabled=true;
		if(!t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,registry)) { Cerr()<<"FAILED T76 synthetic policy installation\n"; s_instance=prior_t76_instance; return false; }
		int no_write_count=t76_store.GetDocuments().GetCount();
		ValueMap unconfigured=PaperResearch::Run(t76_store,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,NULL);
		ValueMap plan=PaperResearch::Run(t76_store,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		ValueArray supported_drafts=MapGet(MapGet(plan,"envelope"),"candidate_drafts");
		ValueArray draft_evidence= supported_drafts.IsEmpty()?ValueArray():ValueArray(MapGet((ValueMap)supported_drafts[0],"evidence_links"));
		if(MapGetString(unconfigured,"status")!="analysis_unconfigured" || analyzer.calls!=1 || t76_store.GetDocuments().GetCount()!=no_write_count || MapGetString(plan,"status")!="candidate_drafts_ready" || MapGet(plan,"envelope").IsNull() || supported_drafts.GetCount()!=1 || draft_evidence.GetCount()!=1 || (String)draft_evidence[0]!=(String)selected_evidence[0]) { Cerr()<<"FAILED T76 supported cited draft or null-generator no-write gate status="<<MapGetString(plan,"status")<<" reason="<<MapGetString(plan,"reason")<<" analyzer="<<analyzer.calls<<" drafts="<<supported_drafts.GetCount()<<" links="<<draft_evidence.GetCount()<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.provider_revision="forged-provider-r1"; int calls_before_forged_revision=analyzer.calls;
		ValueMap forged_revision=PaperResearch::Run(t76_store,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		String forged_revision_json=AsJSON(forged_revision,true);
		if(MapGetString(forged_revision,"status")!="blocked" || analyzer.calls!=calls_before_forged_revision || forged_revision_json.Find("secretref:t76-synthetic")>=0 || forged_revision_json.Find("t76-provider-r1")>=0 || forged_revision_json.Find(provider_id)>=0 || forged_revision_json.Find((String)selected_evidence[0])>=0) { Cerr()<<"FAILED T78 forged provider revision denied without leakage or analyzer call\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.provider_revision="t76-provider-r1";
		Vector<String> t78_sources; t78_sources.Add(source_identity);
		if(!JobService::IsProviderConfigCurrent(t76_app,ws,provider_id,t78_sources,"t76-provider-r1")) { Cerr()<<"FAILED T78 exact trusted provider revision rejected\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		TrustedPolicyRegistry changed_provider_registry; changed_provider_registry.configured=true; for(const PolicyWorkspaceFact& x:registry.workspaces) changed_provider_registry.workspaces.Add(x); for(const PolicySourceFact& x:registry.sources) changed_provider_registry.sources.Add(x); for(const PolicyProviderFact& x:registry.providers) changed_provider_registry.providers.Add(x); changed_provider_registry.providers[0].config_revision="t76-provider-r2";
		if(!t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,changed_provider_registry) || JobService::IsProviderConfigCurrent(t76_app,ws,provider_id,t78_sources,"t76-provider-r1")) { Cerr()<<"FAILED T78 changed provider profile revision remained current\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		if(!t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,registry)) { Cerr()<<"FAILED T78 provider fixture restoration\n"; s_instance=prior_t76_instance; return false; }
		DataWriteAuthorization t76_auth; t76_auth.authenticated=true; t76_auth.action_scope="record:write"; t76_auth.principal="t76-synthetic"; t76_auth.source_id="t73-source"; t76_auth.workspace_id=ws;
		DataWriteAuthorization t76_wrong=t76_auth; t76_wrong.workspace_id="other-t76-workspace";
		DataWriteAuthorization t76_wrong_source=t76_auth; t76_wrong_source.source_id="unregistered-t76-source";
		String plan_id=MapGetString(plan,"id"), op; for(int n=0;n<64;n++) op.Cat('a'); String overlong_operation=op; overlong_operation.Cat('b');
		Vector<String> sensitive_payloads; sensitive_payloads.Add(plan_id); sensitive_payloads.Add(paper_id); sensitive_payloads.Add((String)selected_evidence[0]); sensitive_payloads.Add(op); sensitive_payloads.Add(overlong_operation); sensitive_payloads.Add(analyzer.title); sensitive_payloads.Add(MapGetString(paper->data,"source_sha256")); sensitive_payloads.Add(MapGetString(paper->data,"source_url"));
		for(const DataDocument& d:t76_store.GetDocuments()) if(d.id==(String)selected_evidence[0]) { sensitive_payloads.Add(MapGetString(d.data,"excerpt")); sensitive_payloads.Add(AsJSON(MapGet(d.data,"locator"),true)); }
		auto RedactedFailure=[&](const ValueMap& result) { if(result.GetCount()>2 || result.Find("status")<0) return false; for(int i=0;i<result.GetCount();i++) if((String)result.GetKey(i)!="status" && (String)result.GetKey(i)!="reason") return false; String text=AsJSON(result,true); for(const String& sensitive:sensitive_payloads) if(!sensitive.IsEmpty() && text.Find(sensitive)>=0) return false; return true; };
		ValueMap denied=PaperResearch::PersistRun(plan,t76_store,t76_wrong,op);
		ValueMap source_denied=PaperResearch::PersistRun(plan,t76_store,t76_wrong_source,op);
		int before_rejects=t76_store.GetDocuments().GetCount();
		ValueMap tampered=plan; tampered.Set("status","tampered-status");
		ValueMap tampered_status=PaperResearch::PersistRun(tampered,t76_store,t76_auth,op);
		ValueMap tampered_nested=plan; ValueMap tampered_env=MapGet(tampered_nested,"envelope"); ValueArray tampered_drafts=MapGet(tampered_env,"candidate_drafts"); ValueMap altered_draft=tampered_drafts[0]; altered_draft.Set("title","tampered claim must not publish"); ValueArray altered_drafts; altered_drafts.Add(altered_draft); tampered_env.Set("candidate_drafts",altered_drafts); tampered_nested.Set("envelope",tampered_env);
		ValueMap tampered_claim=PaperResearch::PersistRun(tampered_nested,t76_store,t76_auth,op);
		ValueMap findings_removed=plan; int findings_key=findings_removed.Find("findings"); if(findings_key>=0) findings_removed.Remove(findings_key);
		ValueMap removed_findings=PaperResearch::PersistRun(findings_removed,t76_store,t76_auth,op);
		ValueMap drafts_removed=plan; ValueMap drafts_envelope=MapGet(drafts_removed,"envelope"); drafts_envelope.Set("candidate_drafts",ValueArray()); drafts_removed.Set("envelope",drafts_envelope);
		ValueMap emptied_drafts=PaperResearch::PersistRun(drafts_removed,t76_store,t76_auth,op);
		ValueMap long_operation=PaperResearch::PersistRun(plan,t76_store,t76_auth,overlong_operation);
		ValueMap persisted=PaperResearch::PersistRun(plan,t76_store,t76_auth,op);
		if(MapGetString(denied,"status")=="committed" || MapGetString(source_denied,"status")=="committed" || MapGetString(tampered_status,"status")!="denied" || MapGetString(tampered_claim,"status")!="denied" || MapGetString(removed_findings,"status")!="denied" || MapGetString(emptied_drafts,"status")!="denied" || MapGetString(long_operation,"status")!="denied" || t76_store.GetDocuments().GetCount()!=before_rejects || !RedactedFailure(denied) || !RedactedFailure(source_denied) || !RedactedFailure(tampered_status) || !RedactedFailure(tampered_claim) || !RedactedFailure(removed_findings) || !RedactedFailure(emptied_drafts) || !RedactedFailure(long_operation) || MapGetString(persisted,"status")!="committed" || t76_store.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 T55 redaction/integrity/op-id boundary/create: committed="<<MapGetString(persisted,"status")<<" reason="<<MapGetString(persisted,"reason")<<" tampered="<<MapGetString(tampered_status,"status")<<"/"<<MapGetString(tampered_claim,"status")<<" findings="<<MapGetString(removed_findings,"status")<<" drafts="<<MapGetString(emptied_drafts,"status")<<" long="<<MapGetString(long_operation,"status")<<" docs="<<t76_store.GetDocuments().GetCount()<<" before="<<before_rejects<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore reloaded_t76; if(!reloaded_t76.LoadWriteFixtureForTests(t76_root,"t73-source")) { Cerr()<<"FAILED T76 reload of committed T55 run fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		int persisted_count=reloaded_t76.GetDocuments().GetCount();
		const DataDocument *reloaded_run=reloaded_t76.FindDocument(ws+":"+plan_id);
		if(!reloaded_run || !reloaded_run->valid || !reloaded_run->validated || MapGetString(reloaded_run->data,"kind")!="paper-research-run" || ValueArray(MapGet(MapGet(reloaded_run->data,"envelope"),"candidate_drafts")).GetCount()!=1) { Cerr()<<"FAILED T76 reload did not preserve valid run and supported draft\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		ValueMap retry=PaperResearch::PersistRun(plan,reloaded_t76,t76_auth,op);
		analyzer.title="Changed claim bytes under the same deterministic identity";
		ValueMap changed_plan=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		ValueMap changed_claim=PaperResearch::PersistRun(changed_plan,reloaded_t76,t76_auth,op);
		if(MapGetString(retry,"status")!="already_committed" || reloaded_t76.GetDocuments().GetCount()!=persisted_count || MapGetString(changed_claim,"status")!="conflict" || !RedactedFailure(changed_claim)) { Cerr()<<"FAILED T76 idempotency/changed-claims conflict\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.revision="fake-no-support-r2"; analyzer.title="Unsupported assertion is never proposal content"; analyzer.supported=false;
		ValueMap no_support=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		ValueMap no_support_saved=PaperResearch::PersistRun(no_support,reloaded_t76,t76_auth,"t76-no-support-op");
		ValueArray no_support_findings=MapGet(no_support,"findings");
		if(MapGetString(no_support,"status")!="no_supported_candidate" || !ValueArray(MapGet(MapGet(no_support,"envelope"),"candidate_drafts")).IsEmpty() || no_support_findings.IsEmpty() || MapGetString((ValueMap)no_support_findings[0],"assertion")!="unsupported" || MapGetString((ValueMap)no_support_findings[0],"reason").IsEmpty() || MapGetString(no_support_saved,"status")!="committed" || reloaded_t76.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 persisted no-supported outcome\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore no_support_reload; if(!no_support_reload.LoadWriteFixtureForTests(t76_root,"t73-source")) { Cerr()<<"FAILED T76 reload no-supported fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; } const DataDocument *no_support_record=no_support_reload.FindDocument(ws+":"+MapGetString(no_support,"id"));
		if(!no_support_record || !no_support_record->valid || MapGetString(no_support_record->data,"status")!="no_supported_candidate") { Cerr()<<"FAILED T76 no-supported run not reloadable\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.revision="fake-forged-r3"; analyzer.supported=true; analyzer.forged=true;
		ValueMap forged=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		if(MapGetString(forged,"status")!="no_supported_candidate" || !ValueArray(MapGet(MapGet(forged,"envelope"),"candidate_drafts")).IsEmpty()) { Cerr()<<"FAILED T76 forged citation candidate suppression\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.revision="fake-missing-r4"; analyzer.forged=false; analyzer.missing=true; analyzer.counterevidence=false; analyzer.malformed=false;
		ValueMap missing=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		ValueMap missing_saved=PaperResearch::PersistRun(missing,reloaded_t76,t76_auth,"t76-missing-citation");
		if(MapGetString(missing,"status")!="no_supported_candidate" || !ValueArray(MapGet(MapGet(missing,"envelope"),"candidate_drafts")).IsEmpty() || MapGetString(missing_saved,"status")!="committed" || reloaded_t76.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 missing citation finding persistence\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.revision="fake-counter-r5"; analyzer.missing=false; analyzer.counterevidence=true; analyzer.malformed=false;
		ValueMap counter=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		if(MapGetString(counter,"status")!="no_supported_candidate" || !ValueArray(MapGet(MapGet(counter,"envelope"),"candidate_drafts")).IsEmpty()) { Cerr()<<"FAILED T76 counterevidence candidate suppression\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.revision="fake-malformed-r6"; analyzer.counterevidence=true; analyzer.malformed=true;
		ValueMap malformed=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		if(MapGetString(malformed,"status")!="no_supported_candidate" || !ValueArray(MapGet(MapGet(malformed,"envelope"),"candidate_drafts")).IsEmpty()) { Cerr()<<"FAILED T76 malformed citation/counterevidence suppression\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		analyzer.revision="fake-protected-r6"; analyzer.counterevidence=false;
		TrustedPolicyRegistry protected_registry; protected_registry.configured=true; for(const PolicyWorkspaceFact& x:registry.workspaces) protected_registry.workspaces.Add(x); for(const PolicySourceFact& x:registry.sources) protected_registry.sources.Add(x); for(const PolicyProviderFact& x:registry.providers) protected_registry.providers.Add(x); protected_registry.sources[0].classification=POLICY_PROTECTED;
		if(!t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,protected_registry)) { Cerr()<<"FAILED T76 protected source policy fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		int calls_before_protected=analyzer.calls; ValueMap protected_result=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		TrustedPolicyRegistry hidden_registry; hidden_registry.configured=true; for(const PolicyWorkspaceFact& x:registry.workspaces) hidden_registry.workspaces.Add(x); for(const PolicySourceFact& x:registry.sources) hidden_registry.sources.Add(x); for(const PolicyProviderFact& x:registry.providers) hidden_registry.providers.Add(x); hidden_registry.sources[0].visible=false;
		bool hidden_installed=t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,hidden_registry);
		ValueMap hidden_result=hidden_installed?PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer):ValueMap();
		if(MapGetString(protected_result,"status")!="blocked" || MapGetString(hidden_result,"status")!="blocked" || !RedactedFailure(protected_result) || !RedactedFailure(hidden_result) || analyzer.calls!=calls_before_protected || !t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,registry)) { Cerr()<<"FAILED T76 protected/hidden source policy denial or redaction\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		PaperFetchSnapshot changed_config; changed_config.workspace_id=ws; changed_config.source_identity=source_identity; changed_config.external_version="v1"; changed_config.source_url="https://papers.example.invalid/approved/paper"; changed_config.final_url=changed_config.source_url; changed_config.license_id="license-synthetic"; changed_config.license_evidence_ref="t73-license-evidence"; changed_config.source_bytes="synthetic paper bytes"; changed_config.source_sha256=DataStore::ContentDigest(changed_config.source_bytes); changed_config.captured_at="2026-01-01T00:00:00Z"; changed_config.extractor="synthetic-extractor"; changed_config.extractor_version="v1"; changed_config.config_revision="c2"; changed_config.mime_type="application/pdf"; PaperFetchSection changed_section; changed_section.section="abstract"; changed_section.page=1; changed_section.text="synthetic extracted section"; changed_section.locator="abstract:p1"; changed_config.sections.Add(changed_section);
		PublicPaperSourceRule source_rule; source_rule.rule_id="t73-rule"; source_rule.hostname="papers.example.invalid"; source_rule.path_prefix="/approved/"; source_rule.allowed_license_ids.Add("license-synthetic"); source_rule.max_bytes=1024; source_rule.max_sections=4; source_rule.internal_use_mode="internal-only"; Vector<PublicPaperSourceRule> source_rules; source_rules.Add(source_rule);
		String config_root=workspace.FixtureRoot("t76-changed-extractor-config"); if(!CreatePrivateDirectory(config_root)) { Cerr()<<"FAILED T76 create config fixture root\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore config_seed; if(!config_seed.LoadWriteFixtureForTests(config_root,"t73-source")) { Cerr()<<"FAILED T76 load config fixture writer\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		Vector<DataBatchChange> license_seed; for(const DataDocument& d:reloaded_t76.GetDocuments()) if(d.id=="t73-license-snapshot"||d.id=="t73-license-evidence") { DataBatchChange c; c.action=DATA_BATCH_CREATE; c.qualified_id=ws+":"+d.id; c.proposed=d.data; license_seed.Add(c); }
		String license_seed_status=license_seed.GetCount()==2?config_seed.PublishBatch(t76_auth,"t76-license-seed",license_seed).status:"missing-license-records";
		if(license_seed_status!="committed"||config_seed.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 seed license evidence: "<<license_seed_status<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		PaperSourceImportResult changed_import=PaperSourceImport::Import(config_seed,source_rules,registry,changed_config,t76_auth);
		if(changed_import.status!="committed" || config_seed.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 changed extractor config import: "<<changed_import.status<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore config_store; if(!config_store.LoadWriteFixtureForTests(config_root,"t73-source")) { Cerr()<<"FAILED T76 load imported config fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		String config_paper_id,config_revision,config_extraction; ValueArray config_evidence;
		for(const DataDocument& d:config_store.GetDocuments()) { if(d.kind=="paper-source-snapshot"&&MapGetString(d.data,"source_identity")==source_identity) { config_paper_id=d.id; config_revision=MapGetString(d.data,"upstream_revision"); config_extraction=MapGetString(d.data,"extraction_sha256"); } if(d.kind=="evidence-item"&&MapGetString(d.data,"source_identity")==source_identity) config_evidence.Add(d.id); }
		analyzer.forged=false; analyzer.supported=true; analyzer.missing=false; analyzer.counterevidence=false; analyzer.malformed=false; analyzer.revision="fake-r4"; if(!config_evidence.IsEmpty()) analyzer.evidence_id=config_evidence[0];
		ValueMap new_config_plan=PaperResearch::Run(config_store,t76_app,ws,config_paper_id,config_revision,config_extraction,config_evidence,provider_id,&analyzer);
		if(MapGetString(new_config_plan,"status")!="candidate_drafts_ready" || MapGetString(new_config_plan,"id")==plan_id) { Cerr()<<"FAILED T76 extraction config did not create a distinct run identity: "<<MapGetString(new_config_plan,"status")<<"/"<<MapGetString(new_config_plan,"reason")<<" evidence="<<config_evidence.GetCount()<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		PublicPaperSourceRule disallowed_rule=source_rule; disallowed_rule.rule_id="t73-rule-disallowed"; disallowed_rule.internal_use_mode="external-use"; Vector<PublicPaperSourceRule> disallowed_rules; disallowed_rules.Add(disallowed_rule); PaperFetchSnapshot disallowed_config=changed_config; disallowed_config.source_identity="t73-paper-disallowed"; disallowed_config.config_revision="c3"; PaperSourceImportResult disallowed_import=PaperSourceImport::Import(config_store,disallowed_rules,registry,disallowed_config,t76_auth);
		if(disallowed_import.status!="committed" || config_store.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 disallowed mode fixture import\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore disallowed_store; if(!disallowed_store.LoadWriteFixtureForTests(config_root,"t73-source")) { Cerr()<<"FAILED T76 load disallowed-mode fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		String disallowed_id,disallowed_revision,disallowed_hash; ValueArray disallowed_evidence; for(const DataDocument& d:disallowed_store.GetDocuments()) { if(d.kind=="paper-source-snapshot"&&MapGetString(d.data,"source_identity")=="t73-paper-disallowed") { disallowed_id=d.id; disallowed_revision=MapGetString(d.data,"upstream_revision"); disallowed_hash=MapGetString(d.data,"extraction_sha256"); } if(d.kind=="evidence-item"&&MapGetString(d.data,"source_identity")=="t73-paper-disallowed") disallowed_evidence.Add(d.id); }
		ValueMap disallowed_plan=PaperResearch::Run(disallowed_store,t76_app,ws,disallowed_id,disallowed_revision,disallowed_hash,disallowed_evidence,provider_id,&analyzer);
		ValueMap source_mismatch=PaperResearch::Run(disallowed_store,t76_app,ws,config_paper_id,config_revision,config_extraction,disallowed_evidence,provider_id,&analyzer);
		if(MapGetString(disallowed_plan,"status")!="blocked" || MapGetString(source_mismatch,"status")!="blocked" || !RedactedFailure(disallowed_plan) || !RedactedFailure(source_mismatch)) { Cerr()<<"FAILED T76 non-internal mode/source mismatch denial or redaction\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		const DataDocument *selected_item=NULL; for(const DataDocument& d:disallowed_store.GetDocuments()) if(d.kind=="evidence-item"&&d.id==(String)config_evidence[0]) selected_item=&d;
		const DataDocument *selected_parent=selected_item?disallowed_store.FindDocument(ws+":"+MapGetString(selected_item->data,"snapshot")):NULL;
		if(!selected_parent) { Cerr()<<"FAILED T76 retraction fixture source snapshot missing\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		ValueMap retracted_parent=selected_parent->data; retracted_parent.Set("retracted",true); DataBatchChange retract; retract.action=DATA_BATCH_UPDATE; retract.qualified_id=selected_parent->qualified_id; retract.expected_revision=selected_parent->revision; retract.expected_source_revision=selected_parent->source_revision; retract.proposed=retracted_parent; Vector<DataBatchChange> retract_batch; retract_batch.Add(retract);
		if(disallowed_store.PublishBatch(t76_auth,"t76-retract-evidence",retract_batch).status!="committed" || disallowed_store.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 retracted evidence T55 fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore retracted_store; if(!retracted_store.LoadWriteFixtureForTests(config_root,"t73-source")) { Cerr()<<"FAILED T76 reload retracted evidence fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		ValueMap retracted_result=PaperResearch::Run(retracted_store,t76_app,ws,config_paper_id,config_revision,config_extraction,config_evidence,provider_id,&analyzer);
		if(MapGetString(retracted_result,"status")!="blocked" || !RedactedFailure(retracted_result)) { Cerr()<<"FAILED T76 retracted evidence denial or redaction\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore read_only_store; read_only_store.LoadFixtureForTests(t76_root); ValueMap readonly_denial=PaperResearch::PersistRun(plan,read_only_store,t76_auth,"t76-readonly-denial");
		if(MapGetString(readonly_denial,"status")=="committed") { Cerr()<<"FAILED T76 read-only T55 denial\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		ValueMap stale=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,"changed-version",extraction,selected_evidence,provider_id,&analyzer);
		ValueMap stale_hash=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,String(64,'0'),selected_evidence,provider_id,&analyzer);
		ValueMap unrelated=PaperResearch::Run(reloaded_t76,t76_app,ws,"missing-paper",paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		PaperFetchSnapshot unknown_license=changed_config; unknown_license.license_id="unknown-license"; if(PaperSourceImport::Import(config_store,source_rules,registry,unknown_license,t76_auth).status=="committed") { Cerr()<<"FAILED T76 unknown license accepted by T73 import\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		if(MapGetString(stale,"status")!="blocked" || MapGetString(stale_hash,"status")!="blocked" || MapGetString(unrelated,"status")!="blocked" || !RedactedFailure(stale) || !RedactedFailure(stale_hash) || !RedactedFailure(unrelated)) { Cerr()<<"FAILED T76 stale/unrelated source denial\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		const DataDocument *duplicate_base_item=NULL; for(const DataDocument& d:reloaded_t76.GetDocuments()) if(d.kind=="evidence-item"&&d.id==(String)selected_evidence[0]) duplicate_base_item=&d;
		const DataDocument *duplicate_base=duplicate_base_item?reloaded_t76.FindDocument(ws+":"+MapGetString(duplicate_base_item->data,"snapshot")):NULL;
		if(!duplicate_base) { Cerr()<<"FAILED T76 duplicate evidence snapshot base missing\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		String duplicate_snapshot_id="t76-duplicate-evidence-snapshot", duplicate_content_hash=DataStore::ContentDigest("distinct duplicate snapshot bytes");
		ValueMap duplicate_snapshot=duplicate_base->data; duplicate_snapshot.Set("id",duplicate_snapshot_id); duplicate_snapshot.Set("content","distinct duplicate snapshot bytes"); duplicate_snapshot.Set("content_sha256",duplicate_content_hash); ValueMap duplicate_envelope=MapGet(duplicate_snapshot,"envelope"); duplicate_envelope.Set("stable_id",duplicate_snapshot_id); duplicate_snapshot.Set("envelope",duplicate_envelope);
		DataBatchChange duplicate_change; duplicate_change.action=DATA_BATCH_CREATE; duplicate_change.qualified_id=ws+":"+duplicate_snapshot_id; duplicate_change.proposed=duplicate_snapshot; Vector<DataBatchChange> duplicate_batch; duplicate_batch.Add(duplicate_change);
		if(reloaded_t76.PublishBatch(t76_auth,"t76-duplicate-snapshot",duplicate_batch).status!="committed" || reloaded_t76.RecoverPendingBatches(t76_auth).status!="recovered") { Cerr()<<"FAILED T76 T55 duplicate evidence snapshot fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		DataStore duplicate_store; if(!duplicate_store.LoadWriteFixtureForTests(t76_root,"t73-source")) { Cerr()<<"FAILED T76 reload duplicate snapshot fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		int calls_before_ambiguous=analyzer.calls; ValueMap ambiguous=PaperResearch::Run(duplicate_store,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		String ambiguous_json=AsJSON(ambiguous,true);
		if(MapGetString(ambiguous,"status")!="blocked" || !RedactedFailure(ambiguous) || ambiguous_json.Find(duplicate_snapshot_id)>=0 || ambiguous_json.Find(duplicate_content_hash)>=0 || analyzer.calls!=calls_before_ambiguous) { Cerr()<<"FAILED T76 duplicate snapshot ambiguity did not fail closed/redacted before generator\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		// T79 exercises the T76 plan through the T77 handoff using only this fixture.
		DataSourceRegistration prior_registration=t76_app.test_registration; bool prior_registration_provider=t76_app.test_registration_provider; String prior_fixture_root=t76_app.test_fixture_root;
		t76_app.test_fixture_root=t76_root; t76_app.test_registration_provider=true; t76_app.test_registration.id="t73-source"; t76_app.test_registration.root=NormalizePath(t76_root); t76_app.test_registration.enabled=true; t76_app.test_registration.read_only=false; t76_app.test_registration.write_enabled=true;
		bool registration_ready=t76_app.RefreshCatalog(); UserSession handoff_session=registration_ready?SystemAuthService::CreateTestSessionForTests():UserSession();
		auto t79_counts=[&](const DataStore& data,int& proposals,int& handoffs) { proposals=0; handoffs=0; for(const DataDocument& d:data.GetDocuments()) if(d.workspace_id==ws) { if(d.kind=="candidate-proposal") proposals++; if(d.kind=="paper-candidate-handoff") handoffs++; } };
		ValueMap first_handoff=registration_ready?PaperResearch::Handoff(reloaded_t76,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap();
		DataStore handoff_store; bool first_reload=handoff_store.LoadWriteFixtureForTests(t76_root,"t73-source"); int after_first_handoff=first_reload?handoff_store.GetDocuments().GetCount():0;
		ValueMap exact_handoff_retry=first_reload&&MapGetString(first_handoff,"status")=="complete"?PaperResearch::Handoff(handoff_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap();
		auto generic_denial=[&](const ValueMap& value,const String& sensitive) { String body=AsJSON(value,true); return MapGetString(value,"status")=="denied"&&RedactedFailure(value)&&body.Find(plan_id)<0&&body.Find(paper_id)<0&&body.Find((String)selected_evidence[0])<0&&body.Find(sensitive)<0; };
		DataStore auth_check_store; bool auth_check_loaded=auth_check_store.LoadWriteFixtureForTests(t76_root,"t73-source"); int auth_check_candidates=0,auth_check_handoffs=0; if(auth_check_loaded) t79_counts(auth_check_store,auth_check_candidates,auth_check_handoffs);
		ValueMap wrong_source_handoff=auth_check_loaded?PaperResearch::Handoff(auth_check_store,t76_app,ws,"t79-wrong-source",plan_id,handoff_session):ValueMap();
		ValueMap wrong_session_handoff=auth_check_loaded?PaperResearch::Handoff(auth_check_store,t76_app,ws,"t73-source",plan_id,UserSession()):ValueMap();
		ValueMap wrong_workspace_supported_handoff=auth_check_loaded?PaperResearch::Handoff(auth_check_store,t76_app,"t79-other-workspace","t73-source",plan_id,handoff_session):ValueMap();
		DataStore auth_check_after; bool auth_check_after_loaded=auth_check_after.LoadWriteFixtureForTests(t76_root,"t73-source"); int auth_check_candidates_after=0,auth_check_handoffs_after=0; if(auth_check_after_loaded) t79_counts(auth_check_after,auth_check_candidates_after,auth_check_handoffs_after);
		if(!generic_denial(wrong_source_handoff,"t79-wrong-source")||!generic_denial(wrong_session_handoff,"t73-license-evidence")||!generic_denial(wrong_workspace_supported_handoff,"t79-other-workspace")||!auth_check_after_loaded||auth_check_candidates_after!=auth_check_candidates||auth_check_handoffs_after!=auth_check_handoffs) { Cerr()<<"FAILED T79 wrong-workspace/source/session handoff denial, redaction, or write-count invariance\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		analyzer.title="Validate the pinned section locator"; analyzer.revision="fake-r7"; analyzer.supported=true; analyzer.forged=false; analyzer.missing=false; analyzer.counterevidence=false; analyzer.malformed=false; analyzer.evidence_id=selected_evidence[0];
		ValueMap duplicate_run=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		ValueMap duplicate_run_saved=PaperResearch::PersistRun(duplicate_run,reloaded_t76,t76_auth,"t79-duplicate-run"); String duplicate_run_recovery=reloaded_t76.RecoverPendingBatches(t76_auth).status;
		DataStore duplicate_handoff_store; bool duplicate_reload=duplicate_handoff_store.LoadWriteFixtureForTests(t76_root,"t73-source");
		String duplicate_store_recovery=duplicate_reload?duplicate_handoff_store.RecoverPendingBatches(t76_auth).status:"not-loaded";
		if(duplicate_store_recovery=="recovered") { duplicate_reload=duplicate_handoff_store.LoadWriteFixtureForTests(t76_root,"t73-source"); }
		ValueMap before_duplicate_maps,before_duplicate_paths; if(duplicate_reload) for(const DataDocument& d:duplicate_handoff_store.GetDocuments()) if(d.workspace_id==ws&&(d.kind=="paper-research-run"||d.kind=="candidate-proposal")) { before_duplicate_maps.Add(d.qualified_id,LoadFile(d.path)); before_duplicate_paths.Add(d.qualified_id,d.path); }
		ValueMap duplicate_handoff=duplicate_reload?PaperResearch::Handoff(duplicate_handoff_store,t76_app,ws,"t73-source",MapGetString(duplicate_run,"id"),handoff_session):ValueMap();
		String duplicate_recovery=duplicate_handoff_store.RecoverPendingBatches(t76_auth).status;
		DataStore after_duplicate_handoff; bool after_duplicate_reload=after_duplicate_handoff.LoadWriteFixtureForTests(t76_root,"t73-source");
		const DataDocument *duplicate_handoff_record=after_duplicate_reload?after_duplicate_handoff.FindDocument(ws+":paper-handoff-"+DataStore::ContentDigest(ws+"\n"+MapGetString(duplicate_run,"id")+"\n1").Left(32)):NULL;
		ValueArray duplicate_rows,duplicate_candidates; if(duplicate_handoff_record) { duplicate_rows=MapGet(duplicate_handoff_record->data,"duplicates"); duplicate_candidates=MapGet(duplicate_handoff_record->data,"supported_candidate_ids"); }
		int proposals_after_t79=0,handoffs_after_t79=0; if(after_duplicate_reload) for(const DataDocument& d:after_duplicate_handoff.GetDocuments()) if(d.workspace_id==ws) { if(d.kind=="candidate-proposal") proposals_after_t79++; if(d.kind=="paper-candidate-handoff") handoffs_after_t79++; }
		bool immutable_run_proposal_bytes=duplicate_reload&&after_duplicate_reload; String changed_immutable_id,changed_path,changed_before_hash,changed_after_hash,changed_fields; if(after_duplicate_reload) for(int i=0;i<before_duplicate_maps.GetCount();i++) { String qid=before_duplicate_maps.GetKey(i),before_bytes=before_duplicate_maps.Get(qid); const DataDocument *after=NULL; for(const DataDocument& d:after_duplicate_handoff.GetDocuments()) if(d.qualified_id==qid) { after=&d; break; } String after_bytes=after?LoadFile(after->path):String(); if(!after || after_bytes!=before_bytes) { immutable_run_proposal_bytes=false; changed_immutable_id=qid; changed_path=AsString(before_duplicate_paths.Get(qid))+" -> "+(after?after->path:"<missing>"); changed_before_hash=DataStore::ContentDigest(before_bytes); changed_after_hash=after?DataStore::ContentDigest(after_bytes):"<missing>"; Value before_value=ParseJSON(before_bytes), after_value=ParseJSON(after_bytes); if(before_value.Is<ValueMap>()&&after_value.Is<ValueMap>()) { ValueMap bm=before_value,am=after_value; for(int k=0;k<bm.GetCount();k++) { String key=bm.GetKey(k); if(am.Find(key)<0) changed_fields<<"-"<<key<<"="<<AsJSON(bm[key],true)<<";"; else if(AsJSON(bm[key],true)!=AsJSON(am[key],true)) changed_fields<<"~"<<key<<"="<<AsJSON(bm[key],true)<<" => "<<AsJSON(am[key],true)<<";"; } for(int k=0;k<am.GetCount();k++) { String key=am.GetKey(k); if(bm.Find(key)<0) changed_fields<<"+"<<key<<"="<<AsJSON(am[key],true)<<";"; } } break; } }
		ValueArray first_candidates; if(first_reload) { const DataDocument *first_receipt=NULL; for(const DataDocument& d:handoff_store.GetDocuments()) if(d.kind=="paper-candidate-handoff"&&MapGetString(d.data,"run_id")==plan_id) first_receipt=&d; if(first_receipt) first_candidates=MapGet(first_receipt->data,"supported_candidate_ids"); }
		String exact_duplicate_id=duplicate_rows.GetCount()==1?MapGetString((ValueMap)duplicate_rows[0],"existing_candidate_id"):String();
		if(MapGetString(first_handoff,"status")!="complete" || MapGetString(exact_handoff_retry,"status")!="already_committed" || (after_duplicate_reload&&after_duplicate_handoff.GetDocuments().GetCount()!=after_first_handoff+2) || MapGetString(duplicate_run_saved,"status")!="committed" || (duplicate_recovery!="recovered"&&duplicate_recovery!="clean") || !duplicate_handoff_record || duplicate_rows.GetCount()!=1 || !duplicate_candidates.IsEmpty() || proposals_after_t79!=1 || handoffs_after_t79!=2 || first_candidates.GetCount()!=1 || exact_duplicate_id!=(String)first_candidates[0]) { Cerr()<<"FAILED T79 authorized T76/T77 handoff/retry/duplicate: first="<<MapGetString(first_handoff,"status")<<" retry="<<MapGetString(exact_handoff_retry,"status")<<" run="<<MapGetString(duplicate_run_saved,"status")<<" duplicate="<<MapGetString(duplicate_handoff,"status")<<" reload="<<after_duplicate_reload<<" docs="<<(after_duplicate_reload?after_duplicate_handoff.GetDocuments().GetCount():-1)<<" expected="<<after_first_handoff+2<<" proposals="<<proposals_after_t79<<" handoffs="<<handoffs_after_t79<<" duplicate_rows="<<duplicate_rows.GetCount()<<" duplicate_id="<<exact_duplicate_id<<" expected_id="<<(first_candidates.IsEmpty()?String():String(first_candidates[0]))<<" recovery="<<duplicate_recovery<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		if(MapGetString(duplicate_handoff,"status")!="complete") { Cerr()<<"FAILED T79 same-fingerprint new run duplicate report: "<<MapGetString(duplicate_handoff,"status")<<"/"<<MapGetString(duplicate_handoff,"reason")<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		auto clone_t79_fixture=[&](const String& name,String& root) { root=workspace.FixtureRoot(name); if(!CreatePrivateDirectory(root)) return false; DataStore seed; if(!seed.LoadWriteFixtureForTests(root,"t73-source")) return false; Vector<DataBatchChange> changes; for(const DataDocument& d:after_duplicate_handoff.GetDocuments()) { DataBatchChange c; c.action=DATA_BATCH_CREATE; c.qualified_id=d.qualified_id; c.proposed=d.data; changes.Add(c); } if(changes.IsEmpty()||seed.PublishBatch(t76_auth,"t79-seed-"+name,changes).status!="committed") return false; String recovery=seed.RecoverPendingBatches(t76_auth).status; return recovery=="recovered"||recovery=="clean"; };
		auto install_t79_registration=[&](const String& root) { t76_app.test_fixture_root=root; t76_app.test_registration_provider=true; t76_app.test_registration.id="t73-source"; t76_app.test_registration.root=NormalizePath(root); t76_app.test_registration.enabled=true; t76_app.test_registration.read_only=false; t76_app.test_registration.write_enabled=true; return t76_app.RefreshCatalog(); };
		// Unsupported T76 output remains explicit and never produces a proposal.
		analyzer.supported=false; analyzer.revision="fake-t79-no-support";
		ValueMap unsupported_run=PaperResearch::Run(reloaded_t76,t76_app,ws,paper_id,paper_revision,extraction,selected_evidence,provider_id,&analyzer);
		ValueMap unsupported_saved=PaperResearch::PersistRun(unsupported_run,reloaded_t76,t76_auth,"t79-unsupported-run"); reloaded_t76.RecoverPendingBatches(t76_auth);
		DataStore unsupported_store; bool unsupported_loaded=unsupported_store.LoadWriteFixtureForTests(t76_root,"t73-source"); int unsupported_before=unsupported_loaded?unsupported_store.GetDocuments().GetCount():0, unsupported_proposals_before=0, unsupported_handoffs_before=0; if(unsupported_loaded) t79_counts(unsupported_store,unsupported_proposals_before,unsupported_handoffs_before);
		ValueMap unsupported_handoff=unsupported_loaded?PaperResearch::Handoff(unsupported_store,t76_app,ws,"t73-source",MapGetString(unsupported_run,"id"),handoff_session):ValueMap(); String unsupported_json=AsJSON(unsupported_handoff,true);
		DataStore unsupported_after; bool unsupported_after_loaded=unsupported_after.LoadWriteFixtureForTests(t76_root,"t73-source"); int unsupported_proposals_after=0,unsupported_handoffs_after=0; if(unsupported_after_loaded) t79_counts(unsupported_after,unsupported_proposals_after,unsupported_handoffs_after);
		const DataDocument *unsupported_receipt=NULL; if(unsupported_after_loaded) for(const DataDocument& d:unsupported_after.GetDocuments()) if(d.kind=="paper-candidate-handoff"&&MapGetString(d.data,"run_id")==MapGetString(unsupported_run,"id")) unsupported_receipt=&d;
		if(MapGetString(unsupported_run,"status")!="no_supported_candidate"||MapGetString(unsupported_saved,"status")!="committed"||MapGetString(unsupported_handoff,"status")!="no-supported"||unsupported_json.Find(MapGetString(unsupported_run,"id"))>=0||unsupported_json.Find(analyzer.title)>=0||unsupported_json.Find((String)selected_evidence[0])>=0||!unsupported_receipt||!ValueArray(MapGet(unsupported_receipt->data,"supported_candidate_ids")).IsEmpty()||unsupported_proposals_after!=unsupported_proposals_before||unsupported_handoffs_after!=unsupported_handoffs_before+1||!unsupported_after_loaded||unsupported_after.GetDocuments().GetCount()!=unsupported_before+1) { Cerr()<<"FAILED T79 unsupported run handoff result/no-proposal/redaction: run="<<MapGetString(unsupported_run,"status")<<" saved="<<MapGetString(unsupported_saved,"status")<<" handoff="<<MapGetString(unsupported_handoff,"status")<<" proposals="<<unsupported_proposals_before<<"/"<<unsupported_proposals_after<<" handoffs="<<unsupported_handoffs_before<<"/"<<unsupported_handoffs_after<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		// The live session is scoped to the requested workspace; wrong-workspace denial precedes run lookup.
		int wrong_workspace_count=unsupported_after.GetDocuments().GetCount(); ValueMap wrong_workspace_handoff=PaperResearch::Handoff(unsupported_after,t76_app,"t79-other-workspace","t73-source",MapGetString(unsupported_run,"id"),handoff_session); DataStore wrong_workspace_after; bool wrong_workspace_reloaded=wrong_workspace_after.LoadWriteFixtureForTests(t76_root,"t73-source");
		if(MapGetString(wrong_workspace_handoff,"status")!="denied"||!RedactedFailure(wrong_workspace_handoff)||wrong_workspace_reloaded&&wrong_workspace_after.GetDocuments().GetCount()!=wrong_workspace_count) { Cerr()<<"FAILED T79 wrong-workspace handoff did not deny before lookup/write\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		analyzer.supported=true; analyzer.revision="fake-r7"; analyzer.title="Validate the pinned section locator";
		String missing_ref_root, locator_root;
		String evidence_changed_root, hidden_source_root;
		if(!clone_t79_fixture("t79-missing-license-ref",missing_ref_root) || !clone_t79_fixture("t79-changed-license-locator",locator_root) || !clone_t79_fixture("t79-changed-extraction-evidence",evidence_changed_root) || !clone_t79_fixture("t79-policy-hidden-source",hidden_source_root)) { Cerr()<<"FAILED T79 clone isolated persisted-run fixtures\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		bool changed_evidence_registration=install_t79_registration(evidence_changed_root); DataStore evidence_seed; bool evidence_seeded=evidence_seed.LoadWriteFixtureForTests(evidence_changed_root,"t73-source"); const DataDocument *evidence_item=evidence_seeded?evidence_seed.FindDocument(ws+":"+(String)selected_evidence[0]):NULL;
		DataBatchResult evidence_change_result; if(evidence_item) { ValueMap changed=evidence_item->data; changed.Set("excerpt","synthetic changed extraction excerpt"); ValueMap loc=MapGet(changed,"locator"); loc.Set("section","changed-extraction-section"); changed.Set("locator",loc); changed.Set("content_sha256",DataStore::ContentDigest("synthetic changed extraction bytes")); DataBatchChange c; c.action=DATA_BATCH_UPDATE; c.qualified_id=evidence_item->qualified_id; c.expected_revision=evidence_item->revision; c.expected_source_revision=evidence_item->source_revision; c.proposed=changed; Vector<DataBatchChange> changes; changes.Add(c); evidence_change_result=evidence_seed.PublishBatch(t76_auth,"t79-change-extraction-evidence",changes); evidence_seed.RecoverPendingBatches(t76_auth); }
		DataStore evidence_changed_store; bool evidence_changed_loaded=evidence_item&&evidence_change_result.status=="committed"&&evidence_changed_store.LoadWriteFixtureForTests(evidence_changed_root,"t73-source"); int evidence_changed_before=evidence_changed_loaded?evidence_changed_store.GetDocuments().GetCount():0;
		ValueMap changed_evidence_handoff=changed_evidence_registration&&evidence_changed_loaded?PaperResearch::Handoff(evidence_changed_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap(); String changed_evidence_json=AsJSON(changed_evidence_handoff,true);
		if(MapGetString(changed_evidence_handoff,"status")!="blocked"||!RedactedFailure(changed_evidence_handoff)||changed_evidence_json.Find(paper_id)>=0||changed_evidence_json.Find((String)selected_evidence[0])>=0||changed_evidence_json.Find("synthetic changed extraction excerpt")>=0||evidence_changed_store.GetDocuments().GetCount()!=evidence_changed_before) { Cerr()<<"FAILED T79 changed extraction evidence pin blocked/redacted before writes\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		bool hidden_source_registration=install_t79_registration(hidden_source_root); DataStore hidden_source_store; bool hidden_source_loaded=hidden_source_store.LoadWriteFixtureForTests(hidden_source_root,"t73-source"); int hidden_source_before=hidden_source_loaded?hidden_source_store.GetDocuments().GetCount():0;
		TrustedPolicyRegistry hidden_paper_registry; hidden_paper_registry.configured=registry.configured; for(const PolicySourceFact& fact:registry.sources) hidden_paper_registry.sources.Add(fact); for(const PolicyAncestryFact& fact:registry.ancestry) hidden_paper_registry.ancestry.Add(fact); for(const PolicyProviderFact& fact:registry.providers) hidden_paper_registry.providers.Add(fact); for(const PolicyWorkspaceFact& fact:registry.workspaces) hidden_paper_registry.workspaces.Add(fact); for(const PolicyIdentity& fact:registry.protected_ancestry) hidden_paper_registry.protected_ancestry.Add(fact); if(hidden_paper_registry.sources.GetCount()) hidden_paper_registry.sources[0].visible=false; bool hidden_paper_policy=hidden_source_registration&&t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,hidden_paper_registry);
		ValueMap hidden_paper_handoff=hidden_paper_policy&&hidden_source_loaded?PaperResearch::Handoff(hidden_source_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap(); String hidden_paper_json=AsJSON(hidden_paper_handoff,true);
		if(MapGetString(hidden_paper_handoff,"status")!="blocked"||!RedactedFailure(hidden_paper_handoff)||hidden_paper_json.Find(paper_id)>=0||hidden_paper_json.Find((String)selected_evidence[0])>=0||hidden_source_store.GetDocuments().GetCount()!=hidden_source_before) { Cerr()<<"FAILED T79 policy-hidden paper contributor blocked/redacted before writes\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		if(!t76_app.InstallSyntheticPolicyContextForTests(enabled,enabled,enabled,registry)) { Cerr()<<"FAILED T79 restore visible synthetic source policy\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		bool missing_ref_registration=install_t79_registration(missing_ref_root); DataStore missing_ref_seed; bool missing_ref_seeded=missing_ref_seed.LoadWriteFixtureForTests(missing_ref_root,"t73-source"); const DataDocument *missing_ref_paper=missing_ref_seeded?missing_ref_seed.FindDocument(ws+":"+paper_id):NULL;
		bool missing_ref_mutated=false; if(missing_ref_paper) { ValueMap missing_ref_data=missing_ref_paper->data; missing_ref_data.Set("license_evidence_ref","t79-missing-license-evidence"); missing_ref_mutated=SaveFile(missing_ref_paper->path,AsJSON(missing_ref_data,true)); }
		DataStore missing_ref_store; bool missing_ref_loaded=missing_ref_mutated&&missing_ref_store.LoadWriteFixtureForTests(missing_ref_root,"t73-source"); int missing_ref_before=missing_ref_loaded?missing_ref_store.GetDocuments().GetCount():0, missing_ref_proposals_before=0, missing_ref_handoffs_before=0; if(missing_ref_loaded) t79_counts(missing_ref_store,missing_ref_proposals_before,missing_ref_handoffs_before);
		ValueMap missing_ref_handoff=missing_ref_registration&&missing_ref_loaded?PaperResearch::Handoff(missing_ref_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap(); String missing_ref_json=AsJSON(missing_ref_handoff,true); int missing_ref_proposals_after=0,missing_ref_handoffs_after=0; if(missing_ref_loaded) t79_counts(missing_ref_store,missing_ref_proposals_after,missing_ref_handoffs_after);
		if(MapGetString(missing_ref_handoff,"status")!="blocked" || !RedactedFailure(missing_ref_handoff) || missing_ref_json.Find(paper_id)>=0 || missing_ref_json.Find("t79-missing-license-evidence")>=0 || missing_ref_store.GetDocuments().GetCount()!=missing_ref_before || missing_ref_proposals_after!=missing_ref_proposals_before || missing_ref_handoffs_after!=missing_ref_handoffs_before) { Cerr()<<"FAILED T79 missing paper license reference blocked/redacted before candidate/handoff writes\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		bool locator_registration=install_t79_registration(locator_root); DataStore locator_seed; bool locator_seeded=locator_seed.LoadWriteFixtureForTests(locator_root,"t73-source"); const DataDocument *locator_item=locator_seeded?locator_seed.FindDocument(ws+":t73-license-evidence"):NULL;
		DataBatchResult locator_write; if(locator_item) { ValueMap changed_locator_item=locator_item->data; ValueMap changed_locator=MapGet(changed_locator_item,"locator"); changed_locator.Set("page",2); changed_locator.Set("section","revised-terms"); changed_locator_item.Set("locator",changed_locator); DataBatchChange change; change.action=DATA_BATCH_UPDATE; change.qualified_id=locator_item->qualified_id; change.expected_revision=locator_item->revision; change.expected_source_revision=locator_item->source_revision; change.proposed=changed_locator_item; Vector<DataBatchChange> changes; changes.Add(change); locator_write=locator_seed.PublishBatch(t76_auth,"t79-change-license-locator",changes); locator_seed.RecoverPendingBatches(t76_auth); }
		DataStore locator_store; bool locator_loaded=locator_item&&locator_write.status=="committed"&&locator_store.LoadWriteFixtureForTests(locator_root,"t73-source"); int locator_before=locator_loaded?locator_store.GetDocuments().GetCount():0, locator_proposals_before=0, locator_handoffs_before=0; if(locator_loaded) t79_counts(locator_store,locator_proposals_before,locator_handoffs_before);
		ValueMap locator_handoff=locator_registration&&locator_loaded?PaperResearch::Handoff(locator_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap(); String locator_json=AsJSON(locator_handoff,true); int locator_proposals_after=0,locator_handoffs_after=0; if(locator_loaded) t79_counts(locator_store,locator_proposals_after,locator_handoffs_after);
		if(MapGetString(locator_handoff,"status")!="blocked" || !RedactedFailure(locator_handoff) || locator_json.Find(paper_id)>=0 || locator_json.Find("t73-license-evidence")>=0 || locator_store.GetDocuments().GetCount()!=locator_before || locator_proposals_after!=locator_proposals_before || locator_handoffs_after!=locator_handoffs_before) { Cerr()<<"FAILED T79 changed license locator blocked/redacted before candidate/handoff writes: write="<<locator_write.status<<" handoff="<<MapGetString(locator_handoff,"status")<<"/"<<MapGetString(locator_handoff,"reason")<<" loaded="<<locator_loaded<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		auto reject_paper_pin_mutation=[&](const String& fixture,const String& field,const String& replacement) {
			String root; if(!clone_t79_fixture(fixture,root) || !install_t79_registration(root)) return false;
			DataStore seed; if(!seed.LoadWriteFixtureForTests(root,"t73-source")) return false;
			const DataDocument *paper_doc=seed.FindDocument(ws+":"+paper_id); if(!paper_doc) return false;
			ValueMap changed=paper_doc->data; changed.Set(field,replacement);
			DataBatchChange change; change.action=DATA_BATCH_UPDATE; change.qualified_id=paper_doc->qualified_id;
			change.expected_revision=paper_doc->revision; change.expected_source_revision=paper_doc->source_revision; change.proposed=changed;
			Vector<DataBatchChange> changes; changes.Add(change);
			if(seed.PublishBatch(t76_auth,"t80-mutate-"+fixture,changes).status!="committed" || seed.RecoverPendingBatches(t76_auth).status!="recovered") return false;
			DataStore mutated; if(!mutated.LoadWriteFixtureForTests(root,"t73-source")) return false;
			int before=mutated.GetDocuments().GetCount(), proposals_before=0, handoffs_before=0; t79_counts(mutated,proposals_before,handoffs_before);
			ValueMap bytes_before; for(const DataDocument& d:mutated.GetDocuments()) bytes_before.Add(d.qualified_id,LoadFile(d.path));
			ValueMap result=PaperResearch::Handoff(mutated,t76_app,ws,"t73-source",plan_id,handoff_session); String body=AsJSON(result,true);
			int proposals_after=0, handoffs_after=0; t79_counts(mutated,proposals_after,handoffs_after);
			bool bytes_unchanged=mutated.GetDocuments().GetCount()==bytes_before.GetCount();
			for(const DataDocument& d:mutated.GetDocuments()) if(bytes_before.Find(d.qualified_id)<0 || LoadFile(d.path)!=(String)bytes_before.Get(d.qualified_id)) bytes_unchanged=false;
			return MapGetString(result,"status")=="blocked" && RedactedFailure(result) && body.Find(paper_id)<0 &&
			       body.Find(replacement)<0 && mutated.GetDocuments().GetCount()==before && proposals_after==proposals_before && handoffs_after==handoffs_before && bytes_unchanged;
		};
		auto reject_license_evidence_mutation=[&](const String& fixture,const String& field,const String& replacement,bool remove_field) {
			String root; if(!clone_t79_fixture(fixture,root) || !install_t79_registration(root)) return false;
			DataStore seed; if(!seed.LoadWriteFixtureForTests(root,"t73-source")) return false;
			const DataDocument *license=seed.FindDocument(ws+":t73-license-evidence"); if(!license) return false;
			ValueMap changed=license->data;
			if(field=="locator") { ValueMap locator=MapGet(changed,"locator"); locator.Set("page",2); locator.Set("section","revised-terms"); changed.Set("locator",locator); }
			else if(remove_field) { int field_i=changed.Find(field); if(field_i>=0) changed.Remove(field_i); }
			else changed.Set(field,replacement);
			DataBatchChange change; change.action=DATA_BATCH_UPDATE; change.qualified_id=license->qualified_id; change.expected_revision=license->revision; change.expected_source_revision=license->source_revision; change.proposed=changed;
			Vector<DataBatchChange> changes; changes.Add(change);
			if(seed.PublishBatch(t76_auth,"t80-mutate-"+fixture,changes).status!="committed" || seed.RecoverPendingBatches(t76_auth).status!="recovered") return false;
			DataStore mutated; if(!mutated.LoadWriteFixtureForTests(root,"t73-source")) return false;
			int before=mutated.GetDocuments().GetCount(), proposals_before=0, handoffs_before=0; t79_counts(mutated,proposals_before,handoffs_before);
			ValueMap bytes_before; for(const DataDocument& d:mutated.GetDocuments()) bytes_before.Add(d.qualified_id,LoadFile(d.path));
			ValueMap result=PaperResearch::Handoff(mutated,t76_app,ws,"t73-source",plan_id,handoff_session); String body=AsJSON(result,true);
			int proposals_after=0, handoffs_after=0; t79_counts(mutated,proposals_after,handoffs_after);
			bool bytes_unchanged=mutated.GetDocuments().GetCount()==bytes_before.GetCount();
			for(const DataDocument& d:mutated.GetDocuments()) if(bytes_before.Find(d.qualified_id)<0 || LoadFile(d.path)!=(String)bytes_before.Get(d.qualified_id)) bytes_unchanged=false;
			return MapGetString(result,"status")=="blocked" && RedactedFailure(result) && body.Find(paper_id)<0 && body.Find("t73-license-evidence")<0 &&
			       mutated.GetDocuments().GetCount()==before && proposals_after==proposals_before && handoffs_after==handoffs_before && bytes_unchanged;
		};
		struct T80Mutation { const char *fixture, *field; String value; };
		const T80Mutation t80_mutations[] = {
			{"t80-source-url","source_url","https://t80-source.example.invalid/paper"},
			{"t80-final-url","final_url","https://t80-final.example.invalid/paper"},
			{"t80-license-id","license_id","license-mutated"},
			{"t80-rule-id","rule_id","rule-mutated"},
			{"t80-internal-use","internal_use_mode","external-use"},
			{"t80-config-revision","config_revision","c-t80"},
			{"t80-extractor-revision","extractor_version","extractor-t80"},
			{"t80-extraction-hash","extraction_sha256",String(64,'0')},
			{"t80-source-hash","source_sha256",String(64,'1')}
		};
		for(const T80Mutation& mutation:t80_mutations)
			if(!reject_paper_pin_mutation(mutation.fixture,mutation.field,mutation.value)) {
				Cerr()<<"FAILED T80 paper pin mutation blocked/redacted before writes: "<<mutation.field<<"\n";
				t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false;
			}
		struct T80LicenseMutation { const char *fixture, *field; String value; bool remove_field; };
		const T80LicenseMutation t80_license_mutations[] = {
			{"t80-hidden-license-evidence","visibility","restricted",false},
			{"t80-revised-license-content","excerpt","synthetic revised license evidence",false},
			{"t80-revised-license-revision","upstream_revision","license-revision-t80",false},
			{"t80-changed-license-locator","locator","",false},
			{"t80-changed-license-content-hash","content_sha256",String(64,'2'),false}
		};
		for(const T80LicenseMutation& mutation:t80_license_mutations)
			if(!reject_license_evidence_mutation(mutation.fixture,mutation.field,mutation.value,mutation.remove_field)) {
				Cerr()<<"FAILED T80 license evidence mutation blocked/redacted without writes: "<<mutation.fixture<<"\n";
				t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false;
			}
		String removed_license_root;
		bool removed_license_ready=clone_t79_fixture("t80-removed-license-item",removed_license_root) && install_t79_registration(removed_license_root);
		DataStore removed_license_seed; bool removed_license_loaded=removed_license_ready&&removed_license_seed.LoadWriteFixtureForTests(removed_license_root,"t73-source");
		const DataDocument *removed_license_doc=removed_license_loaded?removed_license_seed.FindDocument(ws+":t73-license-evidence"):NULL;
		bool removed_license_file=removed_license_doc && removed_license_doc->path.Find(removed_license_root)==0 && FileDelete(removed_license_doc->path);
		DataStore removed_license_store; bool removed_license_reloaded=removed_license_file&&removed_license_store.LoadWriteFixtureForTests(removed_license_root,"t73-source");
		int removed_license_before=removed_license_reloaded?removed_license_store.GetDocuments().GetCount():0, removed_license_proposals_before=0, removed_license_handoffs_before=0;
		if(removed_license_reloaded) t79_counts(removed_license_store,removed_license_proposals_before,removed_license_handoffs_before);
		ValueMap removed_license_bytes_before; if(removed_license_reloaded) for(const DataDocument& d:removed_license_store.GetDocuments()) removed_license_bytes_before.Add(d.qualified_id,LoadFile(d.path));
		ValueMap removed_license_handoff=removed_license_reloaded?PaperResearch::Handoff(removed_license_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap();
		String removed_license_json=AsJSON(removed_license_handoff,true); int removed_license_proposals_after=0, removed_license_handoffs_after=0;
		if(removed_license_reloaded) t79_counts(removed_license_store,removed_license_proposals_after,removed_license_handoffs_after);
		DataStore removed_license_after; bool removed_license_after_reloaded=removed_license_reloaded&&removed_license_after.LoadWriteFixtureForTests(removed_license_root,"t73-source");
		bool removed_license_records_unchanged=removed_license_after_reloaded&&removed_license_after.GetDocuments().GetCount()==removed_license_bytes_before.GetCount();
		if(removed_license_records_unchanged) for(const DataDocument& d:removed_license_after.GetDocuments()) {
			int before_i=removed_license_bytes_before.Find(d.qualified_id);
			if(before_i<0 || LoadFile(d.path)!=(String)removed_license_bytes_before.Get(d.qualified_id)) removed_license_records_unchanged=false;
		}
		for(int i=0;i<removed_license_bytes_before.GetCount();i++) {
			String id=removed_license_bytes_before.GetKey(i);
			if(removed_license_json.Find(id)>=0) removed_license_records_unchanged=false;
		}
		String removed_license_excerpt=removed_license_doc?MapGetString(removed_license_doc->data,"excerpt"):String();
		if(!removed_license_excerpt.IsEmpty()&&removed_license_json.Find(removed_license_excerpt)>=0) removed_license_records_unchanged=false;
		if(MapGetString(removed_license_handoff,"status")!="blocked" || !RedactedFailure(removed_license_handoff) || removed_license_json.Find(paper_id)>=0 || removed_license_json.Find("t73-license-evidence")>=0 || !removed_license_reloaded || !removed_license_after_reloaded || removed_license_store.GetDocuments().GetCount()!=removed_license_before || removed_license_proposals_after!=removed_license_proposals_before || removed_license_handoffs_after!=removed_license_handoffs_before || !removed_license_records_unchanged) {
			Cerr()<<"FAILED T80 removed license evidence blocked/redacted before writes: status="<<MapGetString(removed_license_handoff,"status")<<" redacted="<<RedactedFailure(removed_license_handoff)<<" reloaded="<<removed_license_reloaded<<" after="<<removed_license_after_reloaded<<" records="<<removed_license_before<<"/"<<removed_license_after.GetDocuments().GetCount()<<" proposals="<<removed_license_proposals_before<<"/"<<removed_license_proposals_after<<" handoffs="<<removed_license_handoffs_before<<"/"<<removed_license_handoffs_after<<" unchanged="<<removed_license_records_unchanged<<"\n";
			t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false;
		}
		if(!install_t79_registration(t76_root)) { Cerr()<<"FAILED T79 restore main synthetic registration after independent mutation tests\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		DataStore before_source_change; bool source_reload=before_source_change.LoadWriteFixtureForTests(t76_root,"t73-source");
		const DataDocument *paper_before_t79=source_reload?before_source_change.FindDocument(ws+":"+paper_id):NULL;
		if(!paper_before_t79) { Cerr()<<"FAILED T79 source fixture vanished\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		ValueMap changed_paper=paper_before_t79->data; changed_paper.Set("source_url","https://papers.example.invalid/changed/paper"); DataBatchChange source_change; source_change.action=DATA_BATCH_UPDATE; source_change.qualified_id=paper_before_t79->qualified_id; source_change.expected_revision=paper_before_t79->revision; source_change.expected_source_revision=paper_before_t79->source_revision; source_change.proposed=changed_paper; Vector<DataBatchChange> source_changes; source_changes.Add(source_change);
		DataBatchResult changed_source_result=before_source_change.PublishBatch(t76_auth,"t79-change-paper-source",source_changes); before_source_change.RecoverPendingBatches(t76_auth);
		DataStore changed_source_store; bool changed_source_reload=changed_source_store.LoadWriteFixtureForTests(t76_root,"t73-source"); int docs_before_stale_handoff=changed_source_store.GetDocuments().GetCount(); ValueMap stale_handoff=changed_source_result.status=="committed"&&changed_source_reload?PaperResearch::Handoff(changed_source_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap(); String stale_json=AsJSON(stale_handoff,true);
		if(MapGetString(stale_handoff,"status")!="blocked" || !RedactedFailure(stale_handoff) || stale_json.Find(paper_id)>=0 || stale_json.Find(MapGetString(paper_before_t79->data,"source_url"))>=0 || stale_json.Find(MapGetString(paper_before_t79->data,"license_evidence_ref"))>=0 || changed_source_store.GetDocuments().GetCount()!=docs_before_stale_handoff) { Cerr()<<"FAILED T79 changed paper URL blocked/redacted before proposal creation\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		DataStore license_test_store; bool license_test_loaded=license_test_store.LoadWriteFixtureForTests(t76_root,"t73-source"); const DataDocument *license_before=license_test_loaded?license_test_store.FindDocument(ws+":t73-license-evidence"):NULL;
		if(!license_before) { Cerr()<<"FAILED T79 license pin fixture missing\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		ValueMap hidden_license=license_before->data; hidden_license.Set("visibility","restricted"); DataBatchChange hide_license; hide_license.action=DATA_BATCH_UPDATE; hide_license.qualified_id=license_before->qualified_id; hide_license.expected_revision=license_before->revision; hide_license.expected_source_revision=license_before->source_revision; hide_license.proposed=hidden_license; Vector<DataBatchChange> hide_license_changes; hide_license_changes.Add(hide_license);
		String old_source_id=paper_id, old_license_id="t73-license-evidence", old_url=MapGetString(paper_before_t79->data,"source_url");
		DataBatchResult hidden_license_write=license_test_store.PublishBatch(t76_auth,"t79-hide-license",hide_license_changes); license_test_store.RecoverPendingBatches(t76_auth);
		DataStore hidden_license_store; bool hidden_license_loaded=hidden_license_store.LoadWriteFixtureForTests(t76_root,"t73-source"); int before_hidden_handoff=hidden_license_store.GetDocuments().GetCount(); ValueMap hidden_license_handoff=hidden_license_loaded&&hidden_license_write.status=="committed"?PaperResearch::Handoff(hidden_license_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap();
		String hidden_license_json=AsJSON(hidden_license_handoff,true);
	if(MapGetString(hidden_license_handoff,"status")!="blocked" || !RedactedFailure(hidden_license_handoff) || hidden_license_json.Find(old_source_id)>=0 || hidden_license_json.Find(old_license_id)>=0 || hidden_license_json.Find(old_url)>=0 || hidden_license_store.GetDocuments().GetCount()!=before_hidden_handoff) { Cerr()<<"FAILED T79 hidden license item blocked/redacted before proposal and handoff creation: write="<<hidden_license_write.status<<" handoff="<<MapGetString(hidden_license_handoff,"status")<<"/"<<MapGetString(hidden_license_handoff,"reason")<<" loaded="<<hidden_license_loaded<<" docs="<<hidden_license_store.GetDocuments().GetCount()<<" before="<<before_hidden_handoff<<" redact="<<RedactedFailure(hidden_license_handoff)<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		DataStore changed_license_seed; if(!changed_license_seed.LoadWriteFixtureForTests(t76_root,"t73-source")) { Cerr()<<"FAILED T79 reload hidden license seed\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		const DataDocument *hidden_license_doc=changed_license_seed.FindDocument(ws+":t73-license-evidence"); ValueMap changed_license=hidden_license_doc->data; changed_license.Set("visibility","visible"); changed_license.Set("excerpt","synthetic changed license evidence revision"); DataBatchChange revise_license; revise_license.action=DATA_BATCH_UPDATE; revise_license.qualified_id=hidden_license_doc->qualified_id; revise_license.expected_revision=hidden_license_doc->revision; revise_license.expected_source_revision=hidden_license_doc->source_revision; revise_license.proposed=changed_license; Vector<DataBatchChange> revise_license_changes; revise_license_changes.Add(revise_license);
		DataBatchResult revised_license_write=changed_license_seed.PublishBatch(t76_auth,"t79-revise-license",revise_license_changes); changed_license_seed.RecoverPendingBatches(t76_auth);
		DataStore revised_license_store; bool revised_license_loaded=revised_license_store.LoadWriteFixtureForTests(t76_root,"t73-source"); int before_revised_handoff=revised_license_store.GetDocuments().GetCount(); ValueMap revised_license_handoff=revised_license_loaded&&revised_license_write.status=="committed"?PaperResearch::Handoff(revised_license_store,t76_app,ws,"t73-source",plan_id,handoff_session):ValueMap(); String revised_license_json=AsJSON(revised_license_handoff,true);
		if(MapGetString(revised_license_handoff,"status")!="blocked" || !RedactedFailure(revised_license_handoff) || revised_license_json.Find(old_source_id)>=0 || revised_license_json.Find(old_license_id)>=0 || revised_license_json.Find(old_url)>=0 || revised_license_store.GetDocuments().GetCount()!=before_revised_handoff) { Cerr()<<"FAILED T79 changed license item revision blocked/redacted before proposal and handoff creation\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		if(!immutable_run_proposal_bytes) { String old_path=AsString(before_duplicate_paths.Get(changed_immutable_id)); const DataDocument *after=NULL; for(const DataDocument& d:after_duplicate_handoff.GetDocuments()) if(d.qualified_id==changed_immutable_id) { after=&d; break; } Cerr()<<"FAILED T79 pre-duplicate record bytes changed: id="<<changed_immutable_id<<" path="<<changed_path<<" before_exists="<<FileExists(old_path)<<" before_size="<<GetFileLength(old_path)<<" after_exists="<<(after?FileExists(after->path):false)<<" after_size="<<(after?GetFileLength(after->path):-1)<<" before_sha256="<<changed_before_hash<<" after_sha256="<<changed_after_hash<<" fields="<<changed_fields<<" run_recovery="<<duplicate_run_recovery<<" store_recovery="<<duplicate_store_recovery<<"\n"; t76_app.ResetSyntheticPolicyContextForTests(); t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; t76_app.RefreshCatalog(); s_instance=prior_t76_instance; return false; }
		t76_app.test_registration=prior_registration; t76_app.test_registration_provider=prior_registration_provider; t76_app.test_fixture_root=prior_fixture_root; bool registration_restored=t76_app.RefreshCatalog();
		if(!registration_restored) { Cerr()<<"FAILED T79 restore synthetic app registration/session fixture\n"; t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance; return false; }
		t76_app.ResetSyntheticPolicyContextForTests(); s_instance=prior_t76_instance;
		Cout()<<"ok    T76/T77/T79/T80 pinned provenance, handoff/retry/duplicate, URL/license/source metadata mutation matrix and removed evidence denial before writes; no network\n";
	}
	// T70 persistence tests use an isolated writable catalog registration and
	// canonical batches only; the fake executor is local to this fixture.
	{
		ManagerSkylarkApp *prior_instance=s_instance;
		ManagerSkylarkApp app(workspace.root);
		String root=workspace.FixtureRoot("t70-job-app");
		if(!CreatePrivateDirectory(root)) return false;
		const String ws="t70-workspace", source="t70-source", paper_id="t70-input-paper";
		DataWriteAuthorization auth; auth.authenticated=true; auth.action_scope="record:write"; auth.principal="t70-synthetic"; auth.source_id=source; auth.workspace_id=ws;
		DataStore writer; if(!writer.LoadWriteFixtureForTests(root,source)) return false;
		const String source_identity="t70-origin-source";
		ValueMap paper; paper.Add("id",paper_id); paper.Add("kind","evidence-snapshot"); paper.Add("workspace_id",ws); paper.Add("source_kind","paper"); paper.Add("source_identity",source_identity); paper.Add("upstream_revision","upstream-r1"); paper.Add("content_sha256",String('a',64)); paper.Add("captured_at","synthetic"); paper.Add("source_at","synthetic"); paper.Add("collector","synthetic-test");
		ValueMap locator; locator.Add("type","paper-section"); locator.Add("id","synthetic-paper"); locator.Add("section","abstract"); paper.Add("locator",locator); paper.Add("resolution","resolved"); paper.Add("content","synthetic input"); paper.Add("classification","public"); paper.Add("visibility","visible"); paper.Add("retracted",false); paper.Add("predecessor","none"); paper.Add("contributors",ValueArray()); paper.Add("source_lines",ValueArray());
		ValueMap paper_env; paper_env.Add("schema_id","skylark/evidence-snapshot"); paper_env.Add("schema_version","2"); paper_env.Add("workspace_id",ws); paper_env.Add("stable_id",paper_id); paper_env.Add("record_kind","evidence-snapshot"); paper_env.Add("revision","1"); paper_env.Add("visibility","private"); paper_env.Add("provenance_ref","synthetic"); paper.Add("envelope",paper_env);
		DataBatchChange pc; pc.action=DATA_BATCH_CREATE; pc.qualified_id=ws+":"+paper_id; pc.proposed=paper;
		Vector<DataBatchChange> seed; seed.Add(pc);
		DataBatchResult seed_result=writer.PublishBatch(auth,"t70-seed-paper",seed); String seed_recovery=writer.RecoverPendingBatches(auth).status;
		if(seed_result.status!="committed"||(seed_recovery!="recovered"&&seed_recovery!="clean")) { Cerr()<<"FAILED T70 seed input canonical batch "<<seed_result.status<<"/"<<seed_recovery<<"\n"; return false; }
		DataSourceRegistration prior_registration=app.test_registration; bool prior_provider=app.test_registration_provider;
		app.test_registration.id=source; app.test_registration.root=NormalizePath(root); app.test_registration.enabled=true;
		app.test_registration.read_only=false; app.test_registration.write_enabled=true; app.test_registration_provider=true;
		if(!app.RefreshCatalog()) { Cerr()<<"FAILED T70 writable isolated catalog load\n"; return false; }
		PolicySettings job_policy; job_policy.configured=true; job_policy.remote_enabled=true; job_policy.secret_reference="synthetic-secret-reference-marker";
		PolicySettings no_job_overrides; TrustedPolicyRegistry job_registry; job_registry.configured=true;
		PolicyWorkspaceFact job_workspace; job_workspace.identity.workspace_id=ws; job_workspace.identity.object_id=ws; job_registry.workspaces.Add(job_workspace);
		PolicyIdentity job_source_id; job_source_id.workspace_id=ws; job_source_id.object_id=source_identity; PolicySourceFact job_source_fact; job_source_fact.workspace=job_workspace.identity; job_source_fact.identity=job_source_id; job_source_fact.classification=POLICY_PUBLIC; job_source_fact.visible=true; job_registry.sources.Add(job_source_fact);
		const String derived_identity="t71-derived-contributor";
		PolicyIdentity derived_source_id; derived_source_id.workspace_id=ws; derived_source_id.object_id=derived_identity; PolicySourceFact derived_source_fact; derived_source_fact.workspace=job_workspace.identity; derived_source_fact.identity=derived_source_id; derived_source_fact.classification=POLICY_PUBLIC; derived_source_fact.visible=true; job_registry.sources.Add(derived_source_fact);
		PolicyIdentity listed_source_id; listed_source_id.workspace_id=ws; listed_source_id.object_id="t71-source-list-identity"; PolicySourceFact listed_source_fact; listed_source_fact.workspace=job_workspace.identity; listed_source_fact.identity=listed_source_id; listed_source_fact.classification=POLICY_PUBLIC; listed_source_fact.visible=true; job_registry.sources.Add(listed_source_fact);
		PolicyIdentity job_provider_id; job_provider_id.workspace_id=ws; job_provider_id.object_id="t70-provider"; PolicyProviderFact job_provider; job_provider.workspace=job_workspace.identity; job_provider.identity=job_provider_id; job_provider.training_policy="eligible"; job_provider.enabled=true; job_provider.config_revision="synthetic-config-r1"; job_provider.secret_reference="secretref:synthetic-t70"; job_registry.providers.Add(job_provider);
		if(!app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry)) { Cerr()<<"FAILED T71 install synthetic job policy\n"; return false; }
		auto view=[&]() { return app.PinLatestCatalog(); };
		auto paper_doc=[&]() -> const DataDocument * { CatalogReadHandle h=view(); return h?h->data.FindDocument(ws+":"+paper_id):NULL; };
		auto make_request=[&](const String& id,const String& rev,int64 budget=1000,int max_attempts=2,int output_limit=128) {
			ValueMap pin; pin.Add("workspace_id",ws); pin.Add("id",id); pin.Add("kind","evidence-snapshot"); pin.Add("revision",rev); pin.Add("source_id",source);
			ValueArray pins; pins.Add(pin); ValueMap request; request.Add("inputs",pins); request.Add("budget_ms",budget); request.Add("max_attempts",max_attempts); request.Add("max_output_bytes",output_limit); request.Add("provider_id","t70-provider"); request.Add("model_config_revision","synthetic-config-r1"); return request;
		};
		auto request_for_record=[&](const String& id,const String& kind) {
			const DataDocument *doc=view()->data.FindDocument(ws+":"+id); ValueMap pin; pin.Add("workspace_id",ws); pin.Add("id",id); pin.Add("kind",kind); pin.Add("revision",doc?doc->revision:String()); pin.Add("source_id",source);
			ValueArray inputs; inputs.Add(pin); ValueMap request=make_request(paper_id,paper_doc()->revision); request.Set("inputs",inputs); return request;
		};
		struct FakeExecutor : JobExecutor {
			ManagerSkylarkApp *app=nullptr; String ws, expected_id, paper_id; int calls=0; bool saw_running=false; bool cancel_running=false; bool deadline_was_future=false;
			DataWriteAuthorization auth; JobExecutionResult result; ValueMap seen_job; JobExecutionRequest seen_request;
			void (FakeExecutor::*callback)(const JobExecutionRequest&)=nullptr; String callback_status;
			void ChangePinnedInput(const JobExecutionRequest&) {
				CatalogReadHandle h=app->PinLatestCatalog(); const DataDocument *p=h?h->data.FindDocument(ws+":"+paper_id):NULL; if(!p) { callback_status="missing"; return; }
				ValueMap proposed=p->data; proposed.Set("content","changed during execution"); DataBatchChange update; update.action=DATA_BATCH_UPDATE; update.qualified_id=p->qualified_id; update.expected_revision=p->revision; update.expected_source_revision=p->source_revision; update.proposed=proposed;
				Vector<DataBatchChange> batch; batch.Add(update); callback_status=app->FinalizeBatch(auth,"t70-fake-input-change",batch).status;
			}
			JobExecutionResult Execute(const JobExecutionRequest& request) override {
				calls++; seen_job=request.job; seen_request=request; deadline_was_future=request.deadline_at>GetSysTime(); CatalogReadHandle h=app->PinLatestCatalog();
				const DataDocument *d=h?h->data.FindDocument(ws+":"+expected_id):NULL;
				saw_running=d&&MapGetString(d->data,"state")=="running";
				if(cancel_running&&d) { JobService::Result c=JobService::Cancel(*app,h->data,auth,ws,expected_id,d->revision,"t70-cancel"); if(c.status!="committed") result.status="failed"; }
				if(callback) (this->*callback)(request);
				return result;
			}
		} fake;
		fake.app=&app; fake.ws=ws; fake.auth=auth; fake.paper_id=paper_id;
		const DataDocument *pd=paper_doc(); if(!pd||pd->revision.IsEmpty()) return false;
		ValueMap req=make_request(paper_id,pd->revision);
		const String derived_id="t71-derived-item";
		ValueMap derived_item; derived_item.Add("id",derived_id); derived_item.Add("kind","evidence-item"); derived_item.Add("workspace_id",ws); derived_item.Add("snapshot",paper_id); derived_item.Add("snapshot_revision","1"); derived_item.Add("source_identity",derived_identity); derived_item.Add("upstream_revision","upstream-r1"); derived_item.Add("content_sha256",String('d',64)); derived_item.Add("locator",locator); derived_item.Add("resolution","resolved"); derived_item.Add("excerpt","synthetic derived evidence"); derived_item.Add("classification","public"); derived_item.Add("visibility","visible"); derived_item.Add("contributors",ValueArray()); derived_item.Add("source_lines",ValueArray());
		ValueMap derived_env; derived_env.Add("schema_id","skylark/evidence-item"); derived_env.Add("schema_version","2"); derived_env.Add("workspace_id",ws); derived_env.Add("stable_id",derived_id); derived_env.Add("record_kind","evidence-item"); derived_env.Add("revision","1"); derived_env.Add("visibility","private"); derived_env.Add("provenance_ref","synthetic"); derived_item.Add("envelope",derived_env);
		DataBatchChange derived_change; derived_change.action=DATA_BATCH_CREATE; derived_change.qualified_id=ws+":"+derived_id; derived_change.proposed=derived_item; Vector<DataBatchChange> derived_changes; derived_changes.Add(derived_change);
		ManagerSkylarkApp::BatchFinalizationResult derived_seed=app.FinalizeBatch(auth,"t71-seed-derived",derived_changes);
		if(!derived_seed.committed) { Cerr()<<"FAILED T71 derived source fixture seed status="<<derived_seed.status<<"\n"; return false; }
		const String missing_snapshot_revision_id="t71-paper-missing-snapshot-revision";
		ValueMap missing_snapshot_revision; missing_snapshot_revision.Add("id",missing_snapshot_revision_id); missing_snapshot_revision.Add("kind","paper"); missing_snapshot_revision.Add("workspace_id",ws); missing_snapshot_revision.Add("title","synthetic paper input"); missing_snapshot_revision.Add("source","synthetic"); missing_snapshot_revision.Add("status","draft"); missing_snapshot_revision.Add("source_identity","t71-unpinned-paper-source"); missing_snapshot_revision.Add("snapshot",paper_id);
		ValueMap missing_revision_env; missing_revision_env.Add("schema_id","skylark/paper"); missing_revision_env.Add("schema_version","1"); missing_revision_env.Add("workspace_id",ws); missing_revision_env.Add("stable_id",missing_snapshot_revision_id); missing_revision_env.Add("record_kind","paper"); missing_revision_env.Add("revision","1"); missing_revision_env.Add("visibility","private"); missing_revision_env.Add("provenance_ref","synthetic"); missing_snapshot_revision.Add("envelope",missing_revision_env);
		DataBatchChange missing_revision_change; missing_revision_change.action=DATA_BATCH_CREATE; missing_revision_change.qualified_id=ws+":"+missing_snapshot_revision_id; missing_revision_change.proposed=missing_snapshot_revision; Vector<DataBatchChange> missing_revision_changes; missing_revision_changes.Add(missing_revision_change);
		if(!app.FinalizeBatch(auth,"t71-seed-missing-snapshot-revision",missing_revision_changes).committed) { Cerr()<<"FAILED T71 missing snapshot revision fixture seed\n"; return false; }
		const String missing_source_revision_id="t71-paper-missing-source-snapshot-revision";
		ValueMap missing_source_revision=missing_snapshot_revision; missing_source_revision.Set("id",missing_source_revision_id); missing_source_revision.Add("source_snapshot_id",paper_id);
		ValueMap missing_source_env=MapGet(missing_source_revision,"envelope"); missing_source_env.Set("stable_id",missing_source_revision_id); missing_source_revision.Set("envelope",missing_source_env);
		DataBatchChange missing_source_change; missing_source_change.action=DATA_BATCH_CREATE; missing_source_change.qualified_id=ws+":"+missing_source_revision_id; missing_source_change.proposed=missing_source_revision; Vector<DataBatchChange> missing_source_changes; missing_source_changes.Add(missing_source_change);
		if(!app.FinalizeBatch(auth,"t71-seed-missing-source-snapshot-revision",missing_source_changes).committed) { Cerr()<<"FAILED T71 missing source snapshot revision fixture seed\n"; return false; }
		CatalogReadHandle derived_view=view(); const DataDocument *derived_doc=derived_view->data.FindDocument(ws+":"+derived_id); if(!derived_doc) return false;
		ValueMap derived_request=req; ValueMap derived_pin; derived_pin.Add("workspace_id",ws); derived_pin.Add("id",derived_id); derived_pin.Add("kind","evidence-item"); derived_pin.Add("revision",derived_doc->revision); derived_pin.Add("source_id",source); ValueArray derived_pins; derived_pins.Add(derived_pin); derived_request.Set("inputs",derived_pins);
		auto source_revision=[&](const String& id)->String { const DataDocument *d=view()->data.FindDocument(ws+":"+id); return d?MapGetString(d->data,"upstream_revision"):String(); };
		auto provenance_ref=[&](const String& identity,const String& revision) { ValueMap p; p.Add("workspace_id",ws); p.Add("identity",identity); p.Add("revision",revision); return p; };
		auto make_analysis_run=[&](const String& id,const ValueArray& input_revisions,const ValueArray& source_provenance) {
			ValueMap projection; projection.Add("status","configured"); projection.Add("workspace_id",ws); projection.Add("rubric_id","synthetic-rubric"); projection.Add("rubric_revision","1"); projection.Add("tool_config","synthetic-tool"); projection.Add("model_config","synthetic-model"); projection.Add("input_revisions",input_revisions); projection.Add("relationships",ValueArray()); projection.Add("overlap_groups",ValueArray()); projection.Add("source_provenance",source_provenance); projection.Add("evidence_keys",ValueArray()); projection.Add("unique_source_count",2); projection.Add("unique_source_revision_count",2); projection.Add("unique_evidence_count",1); projection.Add("uncertainty","synthetic");
			ValueArray built=AssessmentService::BuildRecords(projection); if(built.IsEmpty()) return ValueMap(); ValueMap run=built[0]; run.Set("id",id); ValueMap env=MapGet(run,"envelope"); env.Set("stable_id",id); run.Set("envelope",env); return run;
		};
		auto record_pin=[&](const String& pin_ws,const String& id,const String& revision) { ValueMap p; p.Add("workspace_id",pin_ws); p.Add("id",id); p.Add("revision",revision); return p; };
		ValueArray run_inputs; run_inputs.Add(record_pin(ws,derived_id,derived_doc->revision));
		ValueArray mixed_provenance; mixed_provenance.Add(provenance_ref(source_identity,source_revision(paper_id))); mixed_provenance.Add(provenance_ref(derived_identity,source_revision(derived_id)));
		Vector<DataBatchChange> lineage_seed;
		auto add_analysis=[&](const ValueMap& run) { DataBatchChange c; c.action=DATA_BATCH_CREATE; c.qualified_id=ws+":"+MapGetString(run,"id"); c.proposed=run; lineage_seed.Add(c); };
		ValueMap mixed_run=make_analysis_run("t71-analysis-mixed",run_inputs,mixed_provenance);
		ValueMap source_relation; source_relation.Add("workspace_id",ws); source_relation.Add("left","t71-source-list-identity"); source_relation.Add("left_kind","source"); source_relation.Add("right","synthetic-candidate"); source_relation.Add("right_kind","candidate"); source_relation.Add("type","related"); ValueArray source_relationships; source_relationships.Add(source_relation); mixed_run.Set("relationships",source_relationships); add_analysis(mixed_run);
		ValueMap id_only_run=make_analysis_run("t71-analysis-id-only",ValueArray(),ValueArray()); add_analysis(id_only_run);
		ValueArray missing_pin; missing_pin.Add(record_pin(ws,"t71-missing-input","1")); add_analysis(make_analysis_run("t71-analysis-missing",missing_pin,ValueArray()));
		ValueArray foreign_pin; foreign_pin.Add(record_pin("t71-foreign-workspace",derived_id,derived_doc->revision)); add_analysis(make_analysis_run("t71-analysis-cross-workspace",foreign_pin,ValueArray()));
		ValueArray revision_pin; revision_pin.Add(record_pin(ws,derived_id,"stale-revision")); add_analysis(make_analysis_run("t71-analysis-stale-revision",revision_pin,ValueArray()));
		ValueArray cycle_a_pin; cycle_a_pin.Add(record_pin(ws,"t71-analysis-cycle-b","1"));		ValueArray cycle_b_pin; cycle_b_pin.Add(record_pin(ws,"t71-analysis-cycle-a","1"));
		add_analysis(make_analysis_run("t71-analysis-cycle-a",cycle_a_pin,ValueArray())); add_analysis(make_analysis_run("t71-analysis-cycle-b",cycle_b_pin,ValueArray()));
		const String ambiguous_identity="t71-ambiguous-source";
		ValueMap duplicate_a=paper,duplicate_b=paper; duplicate_a.Set("id","t71-ambiguous-source-a"); duplicate_b.Set("id","t71-ambiguous-source-b"); duplicate_a.Set("source_identity",ambiguous_identity); duplicate_b.Set("source_identity",ambiguous_identity);
		ValueMap duplicate_a_env=MapGet(duplicate_a,"envelope"),duplicate_b_env=MapGet(duplicate_b,"envelope"); duplicate_a_env.Set("stable_id","t71-ambiguous-source-a"); duplicate_b_env.Set("stable_id","t71-ambiguous-source-b"); duplicate_a.Set("envelope",duplicate_a_env); duplicate_b.Set("envelope",duplicate_b_env);
		for(const ValueMap& duplicate:{duplicate_a,duplicate_b}) { DataBatchChange c; c.action=DATA_BATCH_CREATE; c.qualified_id=ws+":"+MapGetString(duplicate,"id"); c.proposed=duplicate; lineage_seed.Add(c); }
		ValueArray ambiguous_provenance; ambiguous_provenance.Add(provenance_ref(ambiguous_identity,"1")); add_analysis(make_analysis_run("t71-analysis-ambiguous",ValueArray(),ambiguous_provenance));
		ValueArray wrong_evidence; wrong_evidence.Add(MapGetString(mixed_run,"id"));
		ValueMap finding; finding.Add("workspace_id",ws); finding.Add("code","wrong_evidence_kind"); finding.Add("candidate_id","synthetic-candidate"); finding.Add("dimension_id","d1"); finding.Add("evidence_ids",wrong_evidence); finding.Add("supporting_evidence_ids",ValueArray()); finding.Add("counterevidence_ids",ValueArray()); finding.Add("source_ids",ValueArray());
		ValueMap finding_record; finding_record.Add("id","t71-finding-wrong-type"); finding_record.Add("kind","assessment-finding"); finding_record.Add("workspace_id",ws); finding_record.Add("revision","1"); finding_record.Add("provenance","computed"); finding_record.Add("analysis_run",MapGetString(mixed_run,"id")); finding_record.Add("finding",finding);
		ValueMap finding_env; finding_env.Add("schema_id","skylark/assessment-finding"); finding_env.Add("schema_version","1"); finding_env.Add("workspace_id",ws); finding_env.Add("stable_id","t71-finding-wrong-type"); finding_env.Add("record_kind","assessment-finding"); finding_env.Add("revision","1"); finding_env.Add("visibility","private"); finding_env.Add("provenance_ref","synthetic"); finding_record.Add("envelope",finding_env); DataBatchChange finding_change; finding_change.action=DATA_BATCH_CREATE; finding_change.qualified_id=ws+":t71-finding-wrong-type"; finding_change.proposed=finding_record; lineage_seed.Add(finding_change);
		ValueArray trusted_source_list; trusted_source_list.Add("t71-source-list-identity");
		ValueMap source_list_finding=finding_record; source_list_finding.Set("id","t71-finding-source-list"); source_list_finding.Set("analysis_run","t71-analysis-id-only"); source_list_finding.Set("finding",ValueMap()); ValueMap source_list_payload; source_list_payload.Add("workspace_id",ws); source_list_payload.Add("code","source_identity_list"); source_list_payload.Add("candidate_id","synthetic-candidate"); source_list_payload.Add("dimension_id","d1"); source_list_payload.Add("source_ids",trusted_source_list); source_list_finding.Set("finding",source_list_payload);
		ValueMap source_list_env=MapGet(source_list_finding,"envelope"); source_list_env.Set("stable_id","t71-finding-source-list"); source_list_finding.Set("envelope",source_list_env); DataBatchChange source_list_change; source_list_change.action=DATA_BATCH_CREATE; source_list_change.qualified_id=ws+":t71-finding-source-list"; source_list_change.proposed=source_list_finding; lineage_seed.Add(source_list_change);
		for(int i=0;i<lineage_seed.GetCount();i++) { Vector<DataBatchChange> one; one.Add(lineage_seed[i]); String seed_id=MapGetString(lineage_seed[i].proposed,"id"); ManagerSkylarkApp::BatchFinalizationResult lineage_written=app.FinalizeBatch(auth,"t71-seed-analysis-lineage-"+seed_id,one); if(!lineage_written.committed) { Cerr()<<"FAILED T71 analysis lineage fixture seed id="<<seed_id<<" status="<<lineage_written.status<<"\n"; return false; } }
		JobService::Result source_list_submit=JobService::Submit(app,view()->data,auth,ws,"t71-source-id-list",request_for_record("t71-finding-source-list","assessment-finding"),"t71-source-id-list");
		CatalogReadHandle source_list_view=view(); const DataDocument *source_list_job=source_list_view->data.FindDocument(ws+":"+source_list_submit.job_id);
		if(source_list_submit.status!="committed"||!source_list_job||AsJSON(MapGet(source_list_job->data,"contributors"),true).Find("t71-source-list-identity")<0||fake.calls!=0) { Cerr()<<"FAILED T71 trusted source ID list request status="<<source_list_submit.status<<" job="<<source_list_submit.job_id<<"\n"; return false; }
		JobService::Result mixed_run_submit=JobService::Submit(app,view()->data,auth,ws,"t71-analysis-eligible",request_for_record("t71-analysis-mixed","analysis-run"),"t71-analysis-eligible");
		CatalogReadHandle mixed_run_view=view(); const DataDocument *mixed_run_job=mixed_run_view->data.FindDocument(ws+":"+mixed_run_submit.job_id);
		if(mixed_run_submit.status!="committed"||!mixed_run_job||AsJSON(MapGet(mixed_run_job->data,"contributors"),true).Find(source_identity)<0||AsJSON(MapGet(mixed_run_job->data,"contributors"),true).Find(derived_identity)<0||fake.calls!=0) { Cerr()<<"FAILED T71 eligible analysis lineage status="<<mixed_run_submit.status<<"\n"; return false; }
		// T71 policy matrix: remote-disabled and unregistered providers fail before writes.
		{
		int before_policy_denial=view()->data.GetDocuments().GetCount();
		int fake_calls_before_unpinned=fake.calls;
		JobService::Result unpinned_submit=JobService::Submit(app,view()->data,auth,ws,"t71-missing-snapshot-revision",request_for_record(missing_snapshot_revision_id,"paper"),"t71-denied");
		if(unpinned_submit.status!="policy_denied"||view()->data.GetDocuments().GetCount()!=before_policy_denial||fake.calls!=fake_calls_before_unpinned) { Cerr()<<"FAILED T71 missing snapshot revision submit gate status="<<unpinned_submit.status<<"\n"; return false; }
		JobService::Result missing_source_submit=JobService::Submit(app,view()->data,auth,ws,"t71-missing-source-snapshot-revision",request_for_record(missing_source_revision_id,"paper"),"t71-denied");
		if(missing_source_submit.status!="policy_denied"||view()->data.GetDocuments().GetCount()!=before_policy_denial||fake.calls!=fake_calls_before_unpinned) { Cerr()<<"FAILED T71 missing source snapshot revision submit gate status="<<missing_source_submit.status<<"\n"; return false; }
		const DataDocument *unpinned_input=view()->data.FindDocument(ws+":"+missing_source_revision_id); if(!unpinned_input) { Cerr()<<"FAILED T71 missing snapshot revision input fixture lookup\n"; return false; }
		String unpinned_job_id="t71-manual-unpinned-job"; ValueMap unpinned_request=request_for_record(missing_source_revision_id,"paper");
		ValueMap unpinned_policy; unpinned_policy.Add("fingerprint",String('e',64)); unpinned_policy.Add("secret_state","referenced_redacted"); unpinned_policy.Add("decisions",ValueArray());
		ValueMap unpinned_job; unpinned_job.Add("id",unpinned_job_id); unpinned_job.Add("kind","durable-job"); unpinned_job.Add("workspace_id",ws); unpinned_job.Add("revision","1"); unpinned_job.Add("provenance","computed"); unpinned_job.Add("state","queued"); unpinned_job.Add("request_id","synthetic"); unpinned_job.Add("request",unpinned_request); unpinned_job.Add("operation_key","t71-manual-unpinned"); unpinned_job.Add("request_digest",DataStore::ContentDigest(AsJSON(unpinned_request,true))); unpinned_job.Add("inputs",MapGet(unpinned_request,"inputs")); unpinned_job.Add("contributors",ValueArray()); unpinned_job.Add("provider_id","t70-provider"); unpinned_job.Add("model_config_revision","synthetic-config-r1"); unpinned_job.Add("policy_snapshot",unpinned_policy); unpinned_job.Add("attempt",0); unpinned_job.Add("attempt_id",String()); unpinned_job.Add("created_at","synthetic-created"); unpinned_job.Add("updated_at","synthetic-updated"); unpinned_job.Add("started_at",String()); unpinned_job.Add("finished_at",String()); unpinned_job.Add("budget_ms",1000); unpinned_job.Add("max_attempts",1); unpinned_job.Add("max_output_bytes",128); unpinned_job.Add("output_ref",String()); unpinned_job.Add("output_hash",String()); unpinned_job.Add("failure_class",String()); unpinned_job.Add("retryable",false); unpinned_job.Add("idempotency_safe",false); unpinned_job.Add("event_ids",ValueArray()); unpinned_job.Add("run_ids",ValueArray());
		ValueMap unpinned_env; unpinned_env.Add("schema_id","skylark/durable-job"); unpinned_env.Add("schema_version","2"); unpinned_env.Add("workspace_id",ws); unpinned_env.Add("stable_id",unpinned_job_id); unpinned_env.Add("record_kind","durable-job"); unpinned_env.Add("revision","1"); unpinned_env.Add("visibility","private"); unpinned_env.Add("provenance_ref","computed"); unpinned_job.Add("envelope",unpinned_env);
		DataBatchChange unpinned_job_change; unpinned_job_change.action=DATA_BATCH_CREATE; unpinned_job_change.qualified_id=ws+":"+unpinned_job_id; unpinned_job_change.proposed=unpinned_job; Vector<DataBatchChange> unpinned_job_changes; unpinned_job_changes.Add(unpinned_job_change);
		ManagerSkylarkApp::BatchFinalizationResult unpinned_job_seed=app.FinalizeBatch(auth,"t71-seed-unpinned-job",unpinned_job_changes); if(!unpinned_job_seed.committed) { Cerr()<<"FAILED T71 unpinned job fixture seed status="<<unpinned_job_seed.status<<"\n"; return false; }
		CatalogReadHandle unpinned_run_view=view(); const DataDocument *unpinned_queued=unpinned_run_view->data.FindDocument(ws+":"+unpinned_job_id); if(!unpinned_queued) { Cerr()<<"FAILED T71 missing snapshot revision queued fixture lookup\n"; return false; }
		int unpinned_job_count=0; for(const DataDocument& d:unpinned_run_view->data.GetDocuments()) if(d.workspace_id==ws&&d.kind=="durable-job") unpinned_job_count++;
		JobService::Result unpinned_run=JobService::RunOne(app,unpinned_run_view->data,auth,ws,unpinned_job_id,unpinned_queued->revision,"t71-run-unpinned",fake);
		CatalogReadHandle unpinned_after=view(); const DataDocument *unpinned_after_job=unpinned_after->data.FindDocument(ws+":"+unpinned_job_id); int unpinned_jobs_after=0,unpinned_events_after=0,unpinned_runs_after=0;
		for(const DataDocument& d:unpinned_after->data.GetDocuments()) if(d.workspace_id==ws) { if(d.kind=="durable-job") unpinned_jobs_after++; if(MapGetString(d.data,"job_id")==unpinned_job_id&&d.kind=="job-event") unpinned_events_after++; if(MapGetString(d.data,"job_id")==unpinned_job_id&&d.kind=="job-run") unpinned_runs_after++; }
		if(unpinned_run.status!="policy_denied"||fake.calls!=fake_calls_before_unpinned||!unpinned_after_job||MapGetString(unpinned_after_job->data,"state")!="queued"||unpinned_job_count!=unpinned_jobs_after||unpinned_events_after||unpinned_runs_after) { Cerr()<<"FAILED T71 missing snapshot revision RunOne gate status="<<unpinned_run.status<<" calls="<<fake.calls<<"\n"; return false; }
		before_policy_denial=unpinned_after->data.GetDocuments().GetCount();
		Cout()<<"ok    T71 scalar snapshot references require explicit revisions before submit/run writes\n";
		auto deny_lineage=[&](const String& operation,const String& record_id,const String& kind)->bool {
			int before=view()->data.GetDocuments().GetCount();
			JobService::Result denied=JobService::Submit(app,view()->data,auth,ws,operation,request_for_record(record_id,kind),"t71-denied");
			return denied.status=="policy_denied"&&view()->data.GetDocuments().GetCount()==before&&fake.calls==0;
		};
		TrustedPolicyRegistry protected_run; protected_run.configured=true; protected_run.workspaces.Add(job_workspace); protected_run.sources.Add(job_source_fact); PolicySourceFact protected_run_source=derived_source_fact; protected_run_source.classification=POLICY_PROTECTED; protected_run.sources.Add(protected_run_source); protected_run.providers.Add(job_provider);
		if(!app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,protected_run)||!deny_lineage("t71-analysis-protected","t71-analysis-mixed","analysis-run")) return false;
		app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry);
		for(const String& id:{"t71-analysis-id-only","t71-analysis-missing","t71-analysis-cross-workspace","t71-analysis-stale-revision","t71-analysis-cycle-a","t71-analysis-ambiguous"})
			if(!deny_lineage("t71-lineage-deny-"+id,id,"analysis-run")) return false;
		if(!deny_lineage("t71-wrong-evidence-type","t71-finding-wrong-type","assessment-finding")) return false;
		TrustedPolicyRegistry protected_lineage; protected_lineage.configured=true; protected_lineage.workspaces.Add(job_workspace); protected_lineage.sources.Add(job_source_fact); PolicySourceFact protected_derived=derived_source_fact; protected_derived.classification=POLICY_PROTECTED; protected_lineage.sources.Add(protected_derived); protected_lineage.providers.Add(job_provider);
		String derived_denial=String();
		if(!app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,protected_lineage)) return false;
		derived_denial=JobService::Submit(app,view()->data,auth,ws,"t71-derived-protected",derived_request,"t71-denied").status;
		if(derived_denial!="policy_denied"||view()->data.GetDocuments().GetCount()!=before_policy_denial) return false;
		TrustedPolicyRegistry missing_lineage; missing_lineage.configured=true; missing_lineage.workspaces.Add(job_workspace); missing_lineage.sources.Add(job_source_fact); missing_lineage.providers.Add(job_provider);
		if(!app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,missing_lineage)||JobService::Submit(app,view()->data,auth,ws,"t71-derived-unregistered",derived_request,"t71-denied").status!="policy_denied"||view()->data.GetDocuments().GetCount()!=before_policy_denial) return false;
		app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry);
		ValueMap forged_revision=req; forged_revision.Set("model_config_revision","forged-config-r1");
		String forged_result=JobService::Submit(app,view()->data,auth,ws,"t71-forged-config",forged_revision,"t71-denied").status;
		if(forged_result!="policy_denied"||view()->data.GetDocuments().GetCount()!=before_policy_denial) { Cerr()<<"FAILED T71 forged provider config revision status="<<forged_result<<"\n"; return false; }
		PolicySettings disabled_job_policy=job_policy; disabled_job_policy.remote_enabled=false;
		app.InstallSyntheticPolicyContextForTests(disabled_job_policy,no_job_overrides,no_job_overrides,job_registry);
		String disabled_result=JobService::Submit(app,view()->data,auth,ws,"t71-remote-disabled",req,"t71-denied").status;
		if(disabled_result!="policy_denied"||view()->data.GetDocuments().GetCount()!=before_policy_denial) { Cerr()<<"FAILED T71 disabled policy submission status="<<disabled_result<<"\n"; return false; }
		app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry);
		ValueMap unknown_provider_request=req; unknown_provider_request.Set("provider_id","unregistered-provider");
		String unknown_result=JobService::Submit(app,view()->data,auth,ws,"t71-unknown-provider",unknown_provider_request,"t71-denied").status;
		if(unknown_result!="policy_denied"||view()->data.GetDocuments().GetCount()!=before_policy_denial) { Cerr()<<"FAILED T71 unknown policy submission status="<<unknown_result<<"\n"; return false; }
		auto policy_variant_denied=[&](PolicyClassification classification,bool visible,const String& training,bool provider_enabled)->bool {
			TrustedPolicyRegistry variant; variant.configured=true; variant.workspaces.Add(job_workspace);
			PolicySourceFact source_fact=job_source_fact; source_fact.classification=classification; source_fact.visible=visible; variant.sources.Add(source_fact);
			PolicyProviderFact provider_fact=job_provider; provider_fact.training_policy=training; provider_fact.enabled=provider_enabled; provider_fact.config_revision="synthetic-config-r1"; provider_fact.secret_reference="secretref:synthetic-t70"; variant.providers.Add(provider_fact);
			return app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,variant)&&
			       JobService::Submit(app,view()->data,auth,ws,"t71-variant-"+AsString((int)classification)+AsString(visible)+training+AsString(provider_enabled),req,"t71-denied").status=="policy_denied"&&
			       view()->data.GetDocuments().GetCount()==before_policy_denial;
		};
		if(!policy_variant_denied(POLICY_PROTECTED,true,"eligible",true)||!policy_variant_denied(POLICY_PUBLIC,false,"eligible",true)||!policy_variant_denied(POLICY_PUBLIC,true,"ineligible",true)||!policy_variant_denied(POLICY_PUBLIC,true,"eligible",false)) return false;
		app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry);
		ValueMap id_only_request=req; ValueArray id_only_pins=MapGet(id_only_request,"inputs"); ValueMap id_only_pin=id_only_pins[0]; id_only_pin.Set("id","t70-provider"); id_only_pins.Set(0,id_only_pin); id_only_request.Set("inputs",id_only_pins);
		if(JobService::Submit(app,view()->data,auth,ws,"t71-record-id-not-source",id_only_request,"t71-denied").status!="stale_input"||view()->data.GetDocuments().GetCount()!=before_policy_denial) return false;
		Cout()<<"ok    T71 forged/unknown provider revision, remote-disabled, and record-ID-only inputs deny without job writes\n";
		}
		CatalogReadHandle initial=view(); JobService::Result submitted=JobService::Submit(app,initial->data,auth,ws,"t70-submit",req,"t70-created");
		if(submitted.status!="committed") { Cerr()<<"FAILED T70 submit: "<<submitted.status<<"\n"; return false; }
		ValueMap wrong_typed=req; wrong_typed.Set("budget_ms",true);
		ValueMap double_typed=req; double_typed.Set("budget_ms",1.0);
		if(JobService::Submit(app,initial->data,auth,ws,"t70-bool-budget",wrong_typed,"t70-invalid").status!="denied"||
		   JobService::Submit(app,initial->data,auth,ws,"t70-double-budget",double_typed,"t70-invalid").status!="denied") return false;
		ValueArray refs=MapGet(req,"inputs"); ValueMap malformed_ref=refs[0]; malformed_ref.Add("unexpected","x"); ValueArray malformed_refs; malformed_refs.Add(malformed_ref);
		ValueMap malformed_req=req; malformed_req.Set("inputs",malformed_refs);
		if(JobService::Submit(app,initial->data,auth,ws,"t70-extra-input-key",malformed_req,"t70-invalid").status!="stale_input") return false;
		ValueMap wrong_kind_ref=refs[0]; wrong_kind_ref.Set("kind","job-run"); ValueArray wrong_kind_refs; wrong_kind_refs.Add(wrong_kind_ref);
		ValueMap wrong_kind_req=req; wrong_kind_req.Set("inputs",wrong_kind_refs);
		if(JobService::Submit(app,initial->data,auth,ws,"t70-wrong-input-kind",wrong_kind_req,"t70-invalid").status!="stale_input") return false;
		CatalogReadHandle after_submit=view(); const DataDocument *queued=after_submit->data.FindDocument(ws+":"+submitted.job_id);
		if(!queued||MapGetString(queued->data,"state")!="queued"||MapGetString(queued->data,"model_config_revision")!="synthetic-config-r1") return false;
		String queued_json=AsJSON(queued->data,true);
		if(queued_json.Find("synthetic-secret-reference-marker")>=0||MapGet((ValueMap)MapGet(queued->data,"policy_snapshot"),"fingerprint").IsNull()) return false;
		JournalRecoveryResult submit_recovery=after_submit->data.RecoverPendingBatches(auth);
		if(submit_recovery.status!="clean"&&submit_recovery.status!="recovered") { Cerr()<<"FAILED T70 submit recovery/reload "<<submit_recovery.status<<"\n"; return false; }
		after_submit=view(); queued=after_submit->data.FindDocument(ws+":"+submitted.job_id); if(!queued)return false;
		int before_exact_retry=after_submit->data.GetDocuments().GetCount();
		if(JobService::Submit(app,after_submit->data,auth,ws,"t70-submit",req,"t70-retry").status!="already_submitted"||after_submit->data.GetDocuments().GetCount()!=before_exact_retry) return false;
		ValueMap altered=req; altered.Set("budget_ms",999); if(JobService::Submit(app,after_submit->data,auth,ws,"t70-submit",altered,"t70-conflict").status!="idempotency_conflict") return false;
		DataWriteAuthorization wrong_workspace=auth; wrong_workspace.workspace_id="t70-other-workspace";
		DataWriteAuthorization wrong_source=auth; wrong_source.source_id="t70-other-source";
		if(JobService::Submit(app,after_submit->data,wrong_workspace,ws,"t70-denied-workspace",req,"t70-denied").status!="denied"||
		   JobService::Submit(app,after_submit->data,wrong_source,ws,"t70-denied-source",req,"t70-denied").status!="denied") return false;
		Cout()<<"ok    T70 enqueue/idempotency persistence\n";
		fake.expected_id=submitted.job_id; fake.result.status="completed"; fake.result.output="synthetic-result"; fake.result.elapsed_ms=10; fake.result.idempotency_safe=true;
		// T71 re-resolves policy after submit and before the fake executor call.
		job_registry.providers[0].config_revision="synthetic-config-r2";
		app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry);
		int before_changed_policy_calls=fake.calls;
		if(JobService::RunOne(app,after_submit->data,auth,ws,submitted.job_id,queued->revision,"t71-policy-changed",fake).status!="policy_denied"||fake.calls!=before_changed_policy_calls) return false;
		CatalogReadHandle still_queued_view=view(); const DataDocument *still_queued=still_queued_view->data.FindDocument(ws+":"+submitted.job_id);
		if(!still_queued||MapGetString(still_queued->data,"state")!="queued") return false;
		job_registry.providers[0].config_revision="synthetic-config-r1";
		app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry);
		CatalogReadHandle recheck_view=view(); const DataDocument *recheck_input=recheck_view->data.FindDocument(ws+":"+paper_id);
		JobService::Result recheck_submit=JobService::Submit(app,recheck_view->data,auth,ws,"t71-final-policy-recheck",make_request(paper_id,recheck_input->revision),"t71-recheck-submit");
		CatalogReadHandle recheck_queued_view=view(); const DataDocument *recheck_queued=recheck_queued_view->data.FindDocument(ws+":"+recheck_submit.job_id);
		if(recheck_submit.status!="committed"||!recheck_queued) return false;
		struct T71PolicyChangeContext { ManagerSkylarkApp *app; PolicySettings policy; PolicySettings overrides; TrustedPolicyRegistry *registry; };
		T71PolicyChangeContext change_context{&app,job_policy,no_job_overrides,&job_registry};
		auto change_policy_after_mark=[](void *opaque) {
			T71PolicyChangeContext *context=(T71PolicyChangeContext *)opaque;
			context->registry->sources[0].visible=false;
			context->app->InstallSyntheticPolicyContextForTests(context->policy,context->overrides,context->overrides,*context->registry);
		};
		fake.expected_id=recheck_submit.job_id; int calls_before_recheck=fake.calls;
		JobService::Result blocked_result=JobService::RunOneForTests(app,recheck_queued_view->data,auth,ws,recheck_submit.job_id,recheck_queued->revision,"t71-recheck-failed",fake,change_policy_after_mark,&change_context);
		CatalogReadHandle blocked_view=view(); const DataDocument *blocked_job=blocked_view->data.FindDocument(ws+":"+recheck_submit.job_id);
		int blocked_runs=0,blocked_events=0; bool blocked_output=false;
		for(const DataDocument& d:blocked_view->data.GetDocuments()) if(d.workspace_id==ws&&MapGetString(d.data,"job_id")==recheck_submit.job_id) {
			if(d.kind=="job-run") { blocked_runs++; blocked_output=!MapGetString(d.data,"output").IsEmpty()||!MapGetString(d.data,"output_hash").IsEmpty(); }
			if(d.kind=="job-event"&&MapGetString(d.data,"to_state")=="failed"&&MapGetString(d.data,"reason")=="policy_denied") blocked_events++;
		}
		job_registry.sources[0].visible=true;
		app.InstallSyntheticPolicyContextForTests(job_policy,no_job_overrides,no_job_overrides,job_registry);
		if(blocked_result.status!="policy_denied"||fake.calls!=calls_before_recheck||!blocked_job||MapGetString(blocked_job->data,"state")!="failed"||
		   MapGetString(blocked_job->data,"failure_class")!="policy_denied"||blocked_runs!=1||blocked_events!=1||blocked_output) {
			Cerr()<<"FAILED T71 post-mark policy denial was not terminal/atomic status="<<blocked_result.status<<" calls="<<fake.calls<<" runs="<<blocked_runs<<" events="<<blocked_events<<"\n"; return false;
		}
		Cout()<<"ok    T71 final policy recheck atomically fails the running job without executor invocation\n";
		fake.expected_id=submitted.job_id;
		JobService::Result completed=JobService::RunOne(app,after_submit->data,auth,ws,submitted.job_id,queued->revision,"t70-finished",fake);
		CatalogReadHandle after_run=view(); const DataDocument *done=after_run->data.FindDocument(ws+":"+submitted.job_id); int runs=0,events=0;
		for(const DataDocument& d:after_run->data.GetDocuments()) if(d.workspace_id==ws&&MapGetString(d.data,"job_id")==submitted.job_id) { if(d.kind=="job-run") runs++; if(d.kind=="job-event") events++; }
		bool result_evidence=false; for(const DataDocument& d:after_run->data.GetDocuments()) if(d.kind=="job-run"&&d.workspace_id==ws&&MapGetString(d.data,"job_id")==submitted.job_id&&MapGetString(d.data,"output")=="synthetic-result") result_evidence=true;
		if(completed.status!="committed"||!fake.saw_running||fake.calls!=1||!done||MapGetString(done->data,"state")!="completed"||runs!=1||events!=3||!result_evidence||
		   fake.seen_request.runtime_limit_ms!=1000||fake.seen_request.input_limit_bytes!=JobService::MAX_REQUEST_BYTES||fake.seen_request.output_limit_bytes!=128||!fake.deadline_was_future) { Cerr()<<"FAILED T70 completed run/event evidence/limits status="<<completed.status<<" saw="<<fake.saw_running<<" calls="<<fake.calls<<" state="<<(done?MapGetString(done->data,"state"):String("missing"))<<" runs="<<runs<<" events="<<events<<" evidence="<<result_evidence<<" deadline="<<fake.deadline_was_future<<"\n"; return false; }
		Cout()<<"ok    T70 executor sees persisted running and result is atomic\n";
		// Timeout and output overflow are terminal failures with persisted run evidence.
		auto enqueue=[&](const String& key,int64 budget,int attempts,int output) -> String { CatalogReadHandle h=view(); const DataDocument *p=h->data.FindDocument(ws+":"+paper_id); JobService::Result r=JobService::Submit(app,h->data,auth,ws,key,make_request(paper_id,p->revision,budget,attempts,output),"t70-enqueue"); return r.status=="committed"?r.job_id:String(); };
		String timeout_id=enqueue("t70-timeout",1,1,128); if(timeout_id.IsEmpty()) return false;
		CatalogReadHandle timeout_view=view(); const DataDocument *timeout_job=timeout_view->data.FindDocument(ws+":"+timeout_id); fake.expected_id=timeout_id; fake.result.elapsed_ms=2; fake.result.output="ok"; fake.result.retryable=true; fake.result.idempotency_safe=true;
		if(JobService::RunOne(app,timeout_view->data,auth,ws,timeout_id,timeout_job->revision,"t70-timeout-result",fake).status!="committed") return false;
		CatalogReadHandle after_timeout=view(); const DataDocument *timeout_done=after_timeout->data.FindDocument(ws+":"+timeout_id);
		if(!timeout_done||MapGetString(timeout_done->data,"state")!="failed"||MapGetString(timeout_done->data,"failure_class")!="timeout") return false;
		for(const DataDocument& d:after_timeout->data.GetDocuments()) if(d.kind=="job-run"&&MapGetString(d.data,"job_id")==timeout_id&&(!MapGetString(d.data,"output").IsEmpty()||MapGetString(d.data,"output_ref").GetCount())) return false;
		Cout()<<"ok    T70 timeout budget persisted failure\n";
		String large_id=enqueue("t70-output-limit",1000,1,2); if(large_id.IsEmpty()) return false;
		CatalogReadHandle large_view=view(); const DataDocument *large_job=large_view->data.FindDocument(ws+":"+large_id); fake.expected_id=large_id; fake.result.elapsed_ms=1; fake.result.output="oversized"; fake.result.retryable=false;
		if(JobService::RunOne(app,large_view->data,auth,ws,large_id,large_job->revision,"t70-large-result",fake).status!="committed") return false;
		CatalogReadHandle after_large=view(); const DataDocument *large_done=after_large->data.FindDocument(ws+":"+large_id);
		if(!large_done||MapGetString(large_done->data,"state")!="failed"||MapGetString(large_done->data,"failure_class")!="output_too_large") return false;
		for(const DataDocument& d:after_large->data.GetDocuments()) if(d.kind=="job-run"&&MapGetString(d.data,"job_id")==large_id&&(!MapGetString(d.data,"output").IsEmpty()||!MapGetString(d.data,"output_ref").IsEmpty()||!MapGetString(d.data,"output_hash").IsEmpty())) return false;
		Cout()<<"ok    T70 output byte bound persisted failure\n";
		// Retry requires both explicit safety flags and remaining attempt budget.
		String retry_id=enqueue("t70-safe-retry",1000,2,128); if(retry_id.IsEmpty()) { Cerr()<<"FAILED T70 enqueue retry fixture\n"; return false; }
		CatalogReadHandle retry_view=view(); const DataDocument *retry_job=retry_view->data.FindDocument(ws+":"+retry_id); fake.expected_id=retry_id; fake.result.status="failed"; fake.result.failure_class="synthetic_retryable"; fake.result.elapsed_ms=1; fake.result.retryable=true; fake.result.idempotency_safe=true;
		if(!retry_job) { Cerr()<<"FAILED T70 reload queued retry job\n"; return false; }
		JobService::Result first_failure=JobService::RunOne(app,retry_view->data,auth,ws,retry_id,retry_job->revision,"t70-fail",fake); if(first_failure.status!="committed") { Cerr()<<"FAILED T70 failed run "<<first_failure.status<<"\n"; return false; }
		CatalogReadHandle failed_view=view(); const DataDocument *failed=failed_view->data.FindDocument(ws+":"+retry_id);
		if(!failed) { Cerr()<<"FAILED T70 reload failed job\n"; return false; }
		JobService::Result retry_transition=failed?JobService::Retry(app,failed_view->data,auth,ws,retry_id,failed->revision,"t70-retry-queued"):JobService::Result(); if(!failed||retry_transition.status!="committed") { Cerr()<<"FAILED T70 safe retry transition "<<retry_transition.status<<"\n"; return false; }
		CatalogReadHandle requeued_view=view(); const DataDocument *requeued=requeued_view->data.FindDocument(ws+":"+retry_id); fake.result.status="completed"; fake.result.output="retry-result";
		JobService::Result second_run=requeued?JobService::RunOne(app,requeued_view->data,auth,ws,retry_id,requeued->revision,"t70-retry-done",fake):JobService::Result(); if(!requeued||second_run.status!="committed") { Cerr()<<"FAILED T70 retried execution "<<second_run.status<<"\n"; return false; }
		CatalogReadHandle retried_view=view(); const DataDocument *retried=retried_view->data.FindDocument(ws+":"+retry_id);
		Value attempt_value=retried?MapGet(retried->data,"attempt"):Value(); int retry_attempt=attempt_value.Is<int>()?(int)attempt_value:attempt_value.Is<int64>()?(int)(int64)attempt_value:attempt_value.Is<double>()?(int)(double)attempt_value:0;
		JobService::Result exhausted_retry=retried?JobService::Retry(app,retried_view->data,auth,ws,retry_id,retried->revision,"t70-retry-again"):JobService::Result();
		if(!retried||MapGetString(retried->data,"state")!="completed"||retry_attempt!=2||exhausted_retry.status!="retry_denied") { Cerr()<<"FAILED T70 attempt limit/state retry="<<retry_attempt<<"/"<<exhausted_retry.status<<"\n"; return false; }
		Cout()<<"ok    T70 explicitly safe failed attempt retry and attempt cap\n";
		// Queued cancellation is terminal; a cancellation racing a running fake becomes uncertain.
		String queued_cancel_id=enqueue("t70-cancel-queued",1000,1,128); if(queued_cancel_id.IsEmpty())return false;
		CatalogReadHandle qc=view(); const DataDocument *qcd=qc->data.FindDocument(ws+":"+queued_cancel_id);
		if(JobService::Cancel(app,qc->data,auth,ws,queued_cancel_id,qcd->revision,"t70-cancel-queued-time").status!="committed")return false;
		CatalogReadHandle qca=view(); const DataDocument *qcx=qca->data.FindDocument(ws+":"+queued_cancel_id); if(MapGetString(qcx->data,"state")!="cancelled")return false;
		String cancel_run_id=enqueue("t70-cancel-running",1000,1,128); if(cancel_run_id.IsEmpty())return false;
		CatalogReadHandle cr=view(); const DataDocument *crd=cr->data.FindDocument(ws+":"+cancel_run_id); fake.expected_id=cancel_run_id; fake.cancel_running=true; fake.result.status="completed"; fake.result.output="should-not-complete"; fake.result.elapsed_ms=1;
		int before_cancel_calls=fake.calls; JobService::RunOne(app,cr->data,auth,ws,cancel_run_id,crd->revision,"t70-cancel-run",fake); fake.cancel_running=false;
		CatalogReadHandle cra=view(); const DataDocument *crx=cra->data.FindDocument(ws+":"+cancel_run_id);
		if(fake.calls!=before_cancel_calls+1||!crx||MapGetString(crx->data,"state")!="uncertain")return false;
		Cout()<<"ok    T70 queued and running cancellation semantics\n";
		// A submitted pin made stale before execution is denied before running or execution.
		String stale_id=enqueue("t70-stale-input",1000,1,128); if(stale_id.IsEmpty())return false;
		CatalogReadHandle stale_view=view(); const DataDocument *stale_job=stale_view->data.FindDocument(ws+":"+stale_id); const DataDocument *fresh_paper=stale_view->data.FindDocument(ws+":"+paper_id); if(!stale_job||!fresh_paper)return false; ValueMap changed_paper=fresh_paper->data; changed_paper.Set("title","revised synthetic input");
		DataBatchChange update; update.action=DATA_BATCH_UPDATE; update.qualified_id=fresh_paper->qualified_id; update.expected_revision=fresh_paper->revision; update.expected_source_revision=fresh_paper->source_revision; update.proposed=changed_paper; Vector<DataBatchChange> update_batch; update_batch.Add(update);
		if(!app.FinalizeBatch(auth,"t70-stale-input-update",update_batch).committed)return false;
		CatalogReadHandle changed_view=view(); fake.expected_id=stale_id; int before_stale_calls=fake.calls;
		JobService::Result stale_run=JobService::RunOne(app,stale_view->data,auth,ws,stale_id,stale_job->revision,"t70-stale-run",fake);
		const DataDocument *stale_current=changed_view->data.FindDocument(ws+":"+stale_id);
		if(stale_run.status!="stale_input"||fake.calls!=before_stale_calls||!stale_current||MapGetString(stale_current->data,"state")!="queued")return false;
		Cout()<<"ok    T70 stale input blocks executor and completion\n";
		// The pinned source can change during execution; result becomes uncertain and is redacted.
		String in_flight_id=enqueue("t70-input-change-during-run",1000,1,128); if(in_flight_id.IsEmpty())return false;
		CatalogReadHandle in_flight_view=view(); const DataDocument *in_flight_job=in_flight_view->data.FindDocument(ws+":"+in_flight_id); if(!in_flight_job)return false;
		fake.expected_id=in_flight_id; fake.result.status="completed"; fake.result.output="must-not-be-persisted"; fake.result.elapsed_ms=1; fake.callback=&FakeExecutor::ChangePinnedInput;
		JobService::Result changed_during_run=JobService::RunOne(app,in_flight_view->data,auth,ws,in_flight_id,in_flight_job->revision,"t70-input-change-result",fake); fake.callback=nullptr;
		CatalogReadHandle after_in_flight=view(); const DataDocument *in_flight_done=after_in_flight->data.FindDocument(ws+":"+in_flight_id); bool raw_exposed=false;
		for(const DataDocument& d:after_in_flight->data.GetDocuments()) if(d.kind=="job-run"&&MapGetString(d.data,"job_id")==in_flight_id) raw_exposed=!MapGetString(d.data,"output").IsEmpty()||!MapGetString(d.data,"output_ref").IsEmpty()||!MapGetString(d.data,"output_hash").IsEmpty();
		if(changed_during_run.status!="committed"||fake.callback_status!="committed"||!in_flight_done||MapGetString(in_flight_done->data,"state")!="uncertain"||MapGetString(in_flight_done->data,"failure_class")!="stale_input"||raw_exposed) { Cerr()<<"FAILED T70 post-executor stale input redaction/state\n"; return false; }
		Cout()<<"ok    T70 post-executor input change persists uncertain with no output\n";
		// Crash simulation: T55 publishes only the running marker; recovery cannot dispatch it.
		String crash_id=enqueue("t70-crash-running",1000,2,128); if(crash_id.IsEmpty()) { Cerr()<<"FAILED T70 enqueue crash fixture\n"; return false; }
		CatalogReadHandle crash_queued=view(); const DataDocument *crash_old=crash_queued->data.FindDocument(ws+":"+crash_id); if(!crash_old) { Cerr()<<"FAILED T70 reload crash job\n"; return false; } ValueMap crash_job=crash_old->data; String attempt_id="t70-attempt-crash"; crash_job.Set("state","running"); crash_job.Set("attempt",1); crash_job.Set("attempt_id",attempt_id); crash_job.Set("started_at","t70-start"); crash_job.Set("updated_at","t70-start");
		ValueArray crash_events=MapGet(crash_job,"event_ids"); ValueMap running_event; running_event.Add("id","job-event-t70-crash-running"); running_event.Add("kind","job-event"); running_event.Add("workspace_id",ws); running_event.Add("revision","1"); running_event.Add("provenance","computed"); running_event.Add("job_id",crash_id); running_event.Add("sequence",crash_events.GetCount()); running_event.Add("from_state","queued"); running_event.Add("to_state","running"); running_event.Add("attempt",1); running_event.Add("attempt_id",attempt_id); running_event.Add("operation_key","t70-crash-running"); running_event.Add("timestamp","t70-start"); running_event.Add("reason","executor_started");
		ValueMap crash_event_env; crash_event_env.Add("schema_id","skylark/job-event"); crash_event_env.Add("schema_version","2"); crash_event_env.Add("workspace_id",ws); crash_event_env.Add("stable_id","job-event-t70-crash-running"); crash_event_env.Add("record_kind","job-event"); crash_event_env.Add("revision","1"); crash_event_env.Add("visibility","private"); crash_event_env.Add("provenance_ref","computed"); running_event.Add("envelope",crash_event_env);
		crash_events.Add("job-event-t70-crash-running"); crash_job.Set("event_ids",crash_events);
		Vector<DataBatchChange> crash_batch; DataBatchChange cu; cu.action=DATA_BATCH_UPDATE; cu.qualified_id=crash_old->qualified_id; cu.expected_revision=crash_old->revision; cu.expected_source_revision=crash_old->source_revision; cu.proposed=crash_job; crash_batch.Add(cu); DataBatchChange ce; ce.action=DATA_BATCH_CREATE; ce.qualified_id=ws+":job-event-t70-crash-running"; ce.proposed=running_event; crash_batch.Add(ce);
		ManagerSkylarkApp::BatchFinalizationResult crash_mark=app.FinalizeBatch(auth,"t70-crash-marker",crash_batch); if(!crash_mark.committed) { Cerr()<<"FAILED T70 running-only crash marker "<<crash_mark.status<<"\n"; return false; }
		CatalogReadHandle crash_running=view(); int before_recovery_calls=fake.calls;
		int recovery_count=JobService::RecoverRunning(app,crash_running->data,auth,ws,"t70-recovered"); if(recovery_count!=1) { Cerr()<<"FAILED T70 restart recovery count "<<recovery_count<<"\n"; return false; }
		CatalogReadHandle recovered_view=view(); const DataDocument *recovered=recovered_view->data.FindDocument(ws+":"+crash_id);
		JobService::Result replay=recovered?JobService::RunOne(app,recovered_view->data,auth,ws,crash_id,recovered->revision,"t70-no-replay",fake):JobService::Result(); if(!recovered||MapGetString(recovered->data,"state")!="uncertain"||fake.calls!=before_recovery_calls||replay.status!="invalid_transition") { Cerr()<<"FAILED T70 recovery/no replay state="<<(recovered?MapGetString(recovered->data,"state"):String("missing"))<<" calls="<<fake.calls<<" replay="<<replay.status<<"\n"; return false; }
		Cout()<<"ok    T70 running restart recovery is uncertain and not replayed\n";
		Cout()<<"ok    T70 isolated persisted submit/idempotency, running visibility, results/budgets/retry, cancellation, stale pins and crash recovery\n";
		app.test_registration_provider=prior_provider; app.test_registration=prior_registration;
		s_instance=prior_instance;
	}
	// T64 canonical records are persisted and reloaded only in an isolated fixture.
	{
		ValueMap input; input.Add("workspace_id","t64-workspace"); ValueArray assertions;
		auto assertion=[&](const String& stable,const String& kind) { ValueMap a; a.Add("stable_id",stable); a.Add("kind",kind); a.Add("provenance","synthetic-evidence"); a.Add("uncertainty","explicit synthetic fixture"); return a; };
		ValueMap observed=assertion("external-observation","observed-submission"); observed.Add("external_identity","external:observation-7"); assertions.Add(observed);
		ValueMap interpretation=assertion("interpretation-7","local-interpretation"); assertions.Add(interpretation);
		ValueMap candidate=assertion("candidate-7","local-candidate"); candidate.Add("candidate_kind","candidate"); assertions.Add(candidate);
		ValueMap proposal=assertion("proposal-7","proposal-revision"); proposal.Add("proposal_id","proposal-7"); proposal.Add("revision_id","r1"); proposal.Add("candidate_id","candidate-7"); assertions.Add(proposal);
		ValueMap decision=assertion("decision-7","selection-decision"); decision.Add("actor","synthetic-human"); decision.Add("decision","consider"); assertions.Add(decision);
		ValueMap denied_child=assertion("t64-private-child","local-interpretation"); denied_child.Set("uncertainty","t64-secret-content-marker");
		ValueMap hidden_link; hidden_link.Add("id","source-parent-link"); hidden_link.Add("type","evidence"); hidden_link.Add("workspace_id","t64-workspace"); hidden_link.Add("target_id","private-parent-marker"); hidden_link.Add("target_kind","evidence-snapshot"); hidden_link.Add("resolution","unresolved");
		ValueMap second_hidden_link=hidden_link; second_hidden_link.Set("id","second-source-parent-link"); second_hidden_link.Set("target_id","second-private-parent-marker");
		ValueArray hidden_links; hidden_links.Add(hidden_link); hidden_links.Add(second_hidden_link); denied_child.Add("relationships",hidden_links); assertions.Add(denied_child);
		input.Add("assertions",assertions); ValueArray built=CandidateLineage::BuildRecords(input);
		if(built.GetCount()!=6 || AsJSON(built,true)!=AsJSON(CandidateLineage::BuildRecords(input),true)) { Cerr()<<"FAILED T64 canonical stable BuildRecords\n"; return false; }
		ValueArray same_id_assertions; same_id_assertions.Add(observed); ValueMap other_workspace; other_workspace.Add("workspace_id","t64-other-workspace"); other_workspace.Add("assertions",same_id_assertions); ValueArray other_built=CandidateLineage::BuildRecords(other_workspace);
		if(other_built.GetCount()!=1 || MapGetString((ValueMap)built[0],"id")==MapGetString((ValueMap)other_built[0],"id")) { Cerr()<<"FAILED T64 workspace-qualified generated identities\n"; return false; }
		ValueMap wrong_origin=(ValueMap)built[1]; wrong_origin.Set("origin_kind","external-observation"); String record_error;
		ValueMap wrong_link_assertion=assertion("wrong-link","local-interpretation"); ValueMap wrong_link; wrong_link.Add("id","bad-link"); wrong_link.Add("type","observes"); wrong_link.Add("workspace_id","t64-workspace"); wrong_link.Add("target_id","candidate-7"); wrong_link.Add("target_kind","local-candidate"); wrong_link.Add("resolution","unresolved"); ValueArray bad_links; bad_links.Add(wrong_link); wrong_link_assertion.Add("relationships",bad_links); ValueMap bad_link_input; bad_link_input.Add("workspace_id","t64-workspace"); ValueArray bad_assertions; bad_assertions.Add(wrong_link_assertion); bad_link_input.Add("assertions",bad_assertions);
		if(CandidateLineage::ValidateRecord(wrong_origin,record_error) || !CandidateLineage::BuildRecords(bad_link_input).IsEmpty()) { Cerr()<<"FAILED T64 wrong-origin/target-kind validation\n"; return false; }
		String root=workspace.FixtureRoot("t64-lineage-store"); if(!CreatePrivateDirectory(root)) return false;
		for(const Value& v:built) {
			ValueMap r=v; String path=AppendFileName(root,"lineage/"+MapGetString(r,"kind")+"-"+MapGetString(r,"id")+".json");
			if(!RealizeDirectory(GetFileFolder(path)) || !SaveFile(path,AsJSON(r,true))) { Cerr()<<"FAILED T64 fixture record write\n"; return false; }
		}
		for(const Value& v:other_built) { ValueMap r=v; String path=AppendFileName(root,"other-workspace/"+MapGetString(r,"kind")+"-"+MapGetString(r,"id")+".json"); if(!RealizeDirectory(GetFileFolder(path))||!SaveFile(path,AsJSON(r,true))) return false; }
		auto hidden_snapshot=[&](const String& id) { ValueMap r; r.Add("id",id); r.Add("kind","evidence-snapshot"); r.Add("workspace_id","t64-workspace"); r.Add("schema_id","skylark/evidence-snapshot"); r.Add("schema_version","2"); r.Add("source_kind","paper"); r.Add("source_identity","t64-hidden-source"); r.Add("upstream_revision","r1"); r.Add("content_sha256",String('0',64)); r.Add("captured_at","synthetic"); r.Add("source_at","synthetic"); r.Add("collector","synthetic-test"); ValueMap locator; locator.Add("type","paper-section"); locator.Add("id","synthetic-section"); r.Add("locator",locator); r.Add("resolution","resolved"); r.Add("content","private synthetic parent bytes"); r.Add("classification","private"); r.Add("visibility","restricted"); r.Add("retracted",false); r.Add("predecessor","none"); r.Add("contributors",ValueArray()); r.Add("source_lines",ValueArray()); ValueMap env; env.Add("schema_id","skylark/evidence-snapshot"); env.Add("schema_version","2"); env.Add("workspace_id","t64-workspace"); env.Add("stable_id",id); env.Add("record_kind","evidence-snapshot"); env.Add("revision","1"); env.Add("visibility","private"); env.Add("provenance_ref","synthetic"); r.Add("envelope",env); return r; };
		for(const String& id:{"private-parent-marker","second-private-parent-marker"}) { ValueMap r=hidden_snapshot(id); String path=AppendFileName(root,"evidence/"+id+".json"); if(!RealizeDirectory(GetFileFolder(path))||!SaveFile(path,AsJSON(r,true))) return false; }
		DataStore store; if(!store.LoadFixtureForTests(root) || store.GetLoadStatus()!="loaded" || store.GetDocuments().GetCount()!=9) { Cerr()<<"FAILED T64 persisted schema load: "<<store.GetLoadStatus()<<" count="<<store.GetDocuments().GetCount()<<"\n"; return false; }
		for(const DataDocument& d:store.GetDocuments()) if(!d.valid || !d.validated) { Cerr()<<"FAILED T64 trusted schema rejected "<<d.kind<<": "<<d.invalid_reason<<"\n"; return false; }
		ValueMap bad=interpretation; bad.Add("submission_state","submitted"); ValueMap invalid; invalid.Add("id","bad"); invalid.Add("kind","local-interpretation"); invalid.Add("workspace_id","t64-workspace"); invalid.Add("schema_id","skylark/local-interpretation"); invalid.Add("schema_version","1"); invalid.Add("origin_kind","local-hypothesis"); invalid.Add("owner_kind","local-unknown-author"); invalid.Add("provenance","synthetic"); invalid.Add("uncertainty","unknown"); invalid.Add("submission_state","submitted"); invalid.Add("approval_state","approved"); invalid.Add("author_state","known"); invalid.Add("intent_state","unknown"); invalid.Add("relationships",ValueArray()); String error;
		ValueMap empty_projection; empty_projection.Add("workspace_id","t64-workspace"); empty_projection.Add("assertions",ValueArray());
		if(CandidateLineage::ValidateRecord(invalid,error) || CandidateLineage::BuildRecords(empty_projection).GetCount()!=0) { Cerr()<<"FAILED T64 hypothesis fail-closed validation\n"; return false; }
		Cout()<<"ok    T64 BuildRecords emits deterministic canonical records validated by isolated DataStore reload\n";
	}
	// T63 qualitative assertions are inert metadata and retain independent provenance.
	{
		ValueMap rubric;rubric.Add("id","t63-rubric");rubric.Add("workspace_id","t63-workspace");rubric.Add("revision","r1");rubric.Add("score_scale","integer-0-10");rubric.Add("total_rule","weighted-mean");ValueArray dims,cov,tiers;
		for(int i=0;i<6;i++){ValueMap d;d.Add("id",Format("axis-%d",i));d.Add("weight",1);d.Add("minimum_score",0);d.Add("maximum_score",10);d.Add("minimum_evidence_count",1);dims.Add(d);cov.Add(Format("axis-%d",i));}rubric.Add("dimensions",dims);rubric.Add("coverage_required",cov);ValueMap tier;tier.Add("id","t63-tier");tier.Add("minimum_numerator",0);tier.Add("minimum_denominator",1);tier.Add("maximum_numerator",10);tier.Add("maximum_denominator",1);tiers.Add(tier);rubric.Add("tiers",tiers);
		ValueMap inputs;inputs.Add("workspace_id","t63-workspace");ValueArray sources,evidence,scores,pins,relationships;ValueMap source;source.Add("workspace_id","t63-workspace");source.Add("identity","t63-source");source.Add("revision","r1");sources.Add(source);ValueMap source_pin;source_pin.Add("workspace_id","t63-workspace");source_pin.Add("id","t63-source");source_pin.Add("revision","r1");pins.Add(source_pin);
		for(int i=0;i<6;i++){String eid=Format("t63-evidence-%d",i);ValueMap e;e.Add("workspace_id","t63-workspace");e.Add("item_id",eid);e.Add("snapshot_id",Format("t63-snapshot-%d",i));e.Add("source_identity","t63-source");e.Add("source_revision","r1");e.Add("resolution","resolved");e.Add("visibility","visible");evidence.Add(e);ValueMap p;p.Add("workspace_id","t63-workspace");p.Add("id",Format("t63-snapshot-%d",i));p.Add("revision","r1");pins.Add(p);ValueMap a;a.Add("workspace_id","t63-workspace");a.Add("candidate_id","t63-candidate");a.Add("dimension_id",Format("axis-%d",i));a.Add("score","5");a.Add("provenance","computed");ValueArray refs;refs.Add(eid);a.Add("evidence_ids",refs);scores.Add(a);}
		inputs.Add("sources",sources);inputs.Add("evidence",evidence);inputs.Add("scores",scores);inputs.Add("input_revisions",pins);inputs.Add("relationships",relationships);ValueMap plain=AssessmentService::Project(rubric,inputs);
		ValueArray assertions=scores;const char *provenances[]={"authored","ai","historical"};const char *rationales[]={" authored exact \t", "AI rationale conflict", "historical rationale conflict"};const char *crowding[]={"crowding-authored","crowding-ai","crowding-historical"};const char *gaps[]={"gap-authored","gap-ai","gap-historical"};
		for(int i=0;i<3;i++){ValueMap a=(ValueMap)scores[0];a.Set("provenance",provenances[i]);a.Add("rationale",rationales[i]);a.Add("crowding_judgment",crowding[i]);a.Add("opportunity_gap",gaps[i]);assertions.Add(a);}ValueMap with_text=inputs;with_text.Set("scores",assertions);ValueMap rich=AssessmentService::Project(rubric,with_text);
		if(MapGetString(plain,"status")!="configured"||MapGetString(rich,"status")!="configured"||AsJSON(MapGet(plain,"candidate_assessments"),true)!=AsJSON(MapGet(rich,"candidate_assessments"),true)||MapGet(rich,"category_assessments").GetCount()!=9){Cerr()<<"FAILED T63 qualitative projection invariance/conflicts\n";return false;}
		for(int i=0;i<3;i++){bool found=false;ValueArray categories=MapGet(rich,"category_assessments");for(const Value& v:categories){ValueMap ca=v;if(MapGetString(ca,"provenance")!=provenances[i])continue;ValueMap assertion=MapGet(ca,"assessment");if(MapGetString(assertion,"rationale")==rationales[i]&&MapGetString(assertion,"crowding_judgment")==crowding[i]&&MapGetString(assertion,"opportunity_gap")==gaps[i])found=true;}if(!found){Cerr()<<"FAILED T63 verbatim provenance round-trip\n";return false;}}
		ValueMap absent=inputs;ValueMap absent_score=scores[0];ValueArray absent_scores=scores;absent_scores.Set(0,absent_score);absent.Set("scores",absent_scores);ValueMap absent_projection=AssessmentService::Project(rubric,absent);ValueMap absent_assertion=MapGet((ValueMap)MapGet(absent_projection,"category_assessments")[0],"assessment");if(MapGetString(absent_projection,"status")!="configured"||absent_assertion.Find("rationale")>=0){Cerr()<<"FAILED T63 absent optional field\n";return false;}
		auto rejected=[&](const String& key,const Value& value){ValueMap bad=inputs;ValueMap item=scores[0];item.Add(key,value);ValueArray list=scores;list.Set(0,item);bad.Set("scores",list);return MapGetString(AssessmentService::Project(rubric,bad),"status")=="unknown";};
		if(!rejected("rationale",5)||!rejected("unexpected",String("extra"))||!rejected("opportunity_gap",String('x',4097))){Cerr()<<"FAILED T63 qualitative validation bounds/closed keys\n";return false;}
		String utf8_pair="\xC3\xA9",utf8_limit;for(int i=0;i<2048;i++)utf8_limit+=utf8_pair;String utf8_over=utf8_limit+utf8_pair;if(utf8_limit.GetCount()!=4096||utf8_over.GetCount()!=4098){Cerr()<<"FAILED T63 UTF-8 byte fixture construction\n";return false;}
		ValueMap boundary_input=inputs;ValueMap boundary_assertion=scores[0];boundary_assertion.Set("provenance","authored");boundary_assertion.Add("rationale",utf8_limit);ValueArray boundary_scores=scores;boundary_scores.Add(boundary_assertion);boundary_input.Set("scores",boundary_scores);ValueMap boundary_projection=AssessmentService::Project(rubric,boundary_input);
		if(MapGetString(boundary_projection,"status")!="configured"||!rejected("rationale",utf8_over)){Cerr()<<"FAILED T63 UTF-8 service byte bound\n";return false;}
		ValueArray records=AssessmentService::BuildRecords(rich);if(AsJSON(records,true)!=AsJSON(AssessmentService::BuildRecords(rich),true)){Cerr()<<"FAILED T63 deterministic BuildRecords retry bytes\n";return false;}String root=workspace.FixtureRoot("t63-assessment-write");if(records.GetCount()<2||!CreatePrivateDirectory(root)){Cerr()<<"FAILED T63 BuildRecords setup\n";return false;}DataStore writer;if(!writer.LoadWriteFixtureForTests(root,"t63-source")){Cerr()<<"FAILED T63 fixture writer load\n";return false;}DataWriteAuthorization auth;auth.authenticated=true;auth.action_scope="record:write";auth.principal="t63-actor";auth.source_id="t63-source";auth.workspace_id="t63-workspace";Vector<DataBatchChange> changes;for(const Value& v:records){ValueMap record=v;DataBatchChange c;c.action=DATA_BATCH_CREATE;c.qualified_id="t63-workspace:"+MapGetString(record,"id");c.proposed=record;changes.Add(c);}DataBatchResult published=writer.PublishBatch(auth,"t63-qualitative-records",changes);if(published.status!="committed"||writer.RecoverPendingBatches(auth).status!="recovered"){Cerr()<<"FAILED T63 canonical schema BuildRecords write: "<<published.status<<"\n";return false;}
		ValueArray boundary_records=AssessmentService::BuildRecords(boundary_projection);Vector<DataBatchChange> boundary_changes;ValueMap boundary_category;for(const Value& v:boundary_records){ValueMap record=v;DataBatchChange c;c.action=DATA_BATCH_CREATE;c.qualified_id="t63-workspace:"+MapGetString(record,"id");c.proposed=record;boundary_changes.Add(c);if(MapGetString(record,"kind")=="category-assessment"&&MapGetString(MapGet(record,"assessment"),"rationale")==utf8_limit)boundary_category=record;}DataBatchResult boundary_write=writer.PublishBatch(auth,"t63-utf8-boundary",boundary_changes);if(boundary_write.status!="committed"||writer.RecoverPendingBatches(auth).status!="recovered"||boundary_category.IsEmpty()){Cerr()<<"FAILED T63 UTF-8 canonical schema byte bound: "<<boundary_write.status<<"\n";return false;}
		String bad_id="category-assessment-t63-utf8-over";boundary_category.Set("id",bad_id);ValueMap bad_envelope=MapGet(boundary_category,"envelope");bad_envelope.Set("stable_id",bad_id);boundary_category.Set("envelope",bad_envelope);ValueMap bad_assessment=MapGet(boundary_category,"assessment");bad_assessment.Set("rationale",utf8_over);boundary_category.Set("assessment",bad_assessment);DataBatchChange bad_change;bad_change.action=DATA_BATCH_CREATE;bad_change.qualified_id="t63-workspace:"+bad_id;bad_change.proposed=boundary_category;Vector<DataBatchChange> bad_changes;bad_changes.Add(bad_change);DataBatchResult over_schema=writer.PublishBatch(auth,"t63-utf8-over-limit",bad_changes);if(over_schema.status=="committed"||!over_schema.changed_ids.IsEmpty()){Cerr()<<"FAILED T63 canonical schema accepted over-limit UTF-8 bytes\n";return false;}
		Cout()<<"ok    T63 bounded qualitative assertions, UTF-8 byte-limit parity, provenance conflicts, computed invariance and canonical schema validation\n";
	}
	auto is_sha256 = [](const String& digest) {
		if(digest.GetCount() != 64) return false;
		for(int i = 0; i < digest.GetCount(); i++)
			if(!((digest[i] >= '0' && digest[i] <= '9') || (digest[i] >= 'a' && digest[i] <= 'f'))) return false;
		return true;
	};
	auto diagnostics_contain = [](const Vector<String>& diagnostics, const String& token) {
		for(const String& diagnostic : diagnostics) if(diagnostic.Find(token) >= 0) return true;
		return false;
	};
	String digest_a = DataStore::ContentDigest("synthetic-content-a");
	String digest_b = DataStore::ContentDigest("synthetic-content-b");
	if(!is_sha256(digest_a) || !is_sha256(digest_b) || digest_a == digest_b) {
		Cerr() << "FAILED: shared synthetic content digests are not distinct SHA-256 hex values\n";
		return false;
	}
	Cout() << "ok    shared source content digest uses distinct 64-hex SHA-256 values\n";
	if(DataStore::ExceedsSourceBudget(DataStore::MAX_SOURCE_FILE_BYTES, 0) ||
	   !DataStore::ExceedsSourceBudget(DataStore::MAX_SOURCE_FILE_BYTES + 1, 0) ||
	   DataStore::ExceedsSourceBudget(128, DataStore::MAX_TOTAL_SOURCE_BYTES - 128) ||
	   !DataStore::ExceedsSourceBudget(129, DataStore::MAX_TOTAL_SOURCE_BYTES - 128)) {
		Cerr() << "FAILED: aggregate/per-file byte budget boundary predicate\n";
		return false;
	}
	Cout() << "ok    aggregate and per-file byte budget boundaries are deterministic\n";

	// T61 emitted record envelopes are checked by the normal isolated loader.
	{
		ValueMap projection; projection.Add("status","configured"); projection.Add("workspace_id","t61-schema-workspace");
		projection.Add("rubric_id","synthetic-rubric"); projection.Add("rubric_revision","r1");
		projection.Add("tool_config","unconfigured"); projection.Add("model_config","unconfigured");
		ValueArray pins, relations, groups, evidence_keys, sources, findings, categories, candidates;
		ValueMap pin; pin.Add("workspace_id","t61-schema-workspace"); pin.Add("id","input"); pin.Add("revision","1"); pins.Add(pin);
		ValueMap source; source.Add("workspace_id","t61-schema-workspace"); source.Add("identity","synthetic-source"); source.Add("revision","r1"); sources.Add(source);
		ValueMap source2; source2.Add("workspace_id","t61-schema-workspace"); source2.Add("identity","synthetic-source-2"); source2.Add("revision","r1"); sources.Add(source2);
		ValueMap relationship; relationship.Add("workspace_id","t61-schema-workspace"); relationship.Add("left","synthetic-candidate"); relationship.Add("left_kind","candidate"); relationship.Add("right","synthetic-candidate-2"); relationship.Add("right_kind","candidate"); relationship.Add("type","overlap"); relations.Add(relationship);
		ValueMap source_relationship; source_relationship.Add("workspace_id","t61-schema-workspace"); source_relationship.Add("left","synthetic-source"); source_relationship.Add("left_kind","source"); source_relationship.Add("right","synthetic-source-2"); source_relationship.Add("right_kind","source"); source_relationship.Add("type","related"); relations.Add(source_relationship);
		ValueArray members; members.Add("candidate:synthetic-candidate"); members.Add("candidate:synthetic-candidate-2"); ValueMap group; group.Add("group_id","synthetic-overlap"); group.Add("members",members); groups.Add(group);
		projection.Add("input_revisions",pins); projection.Add("relationships",relations); projection.Add("overlap_groups",groups);
		projection.Add("evidence_keys",evidence_keys); projection.Add("source_provenance",sources);
		projection.Add("unique_source_count",1); projection.Add("unique_source_revision_count",1); projection.Add("unique_evidence_count",0);
		projection.Add("uncertainty","insufficient_evidence");
		ValueMap finding; finding.Add("code","synthetic-finding"); finding.Add("candidate_id","synthetic-candidate"); finding.Add("dimension_id","generic-dimension"); finding.Add("evidence_id","synthetic-evidence"); findings.Add(finding);
		ValueMap category; category.Add("id","unused"); category.Add("candidate_id","synthetic-candidate"); category.Add("analysis_run","pending");
		category.Add("provenance","authored"); ValueMap assertion; assertion.Add("workspace_id","t61-schema-workspace"); assertion.Add("candidate_id","synthetic-candidate"); assertion.Add("dimension_id","generic-dimension"); assertion.Add("score","5"); assertion.Add("provenance","authored"); ValueArray assertion_evidence; assertion_evidence.Add("synthetic-evidence"); assertion.Add("evidence_ids",assertion_evidence); category.Add("assessment",assertion); categories.Add(category);
	ValueMap candidate; candidate.Add("candidate_id","synthetic-candidate"); candidate.Add("provenance","computed"); candidate.Add("uncertainty","insufficient_or_conflicted"); ValueArray dims; for(int i=0;i<6;i++){ValueMap dimension;dimension.Add("dimension_id",Format("generic-%d",i));dimension.Add("score","unknown");dimension.Add("score_state","unknown");dimension.Add("evidence_count",0);dimension.Add("coverage_state","incomplete");dims.Add(dimension);}candidate.Add("dimensions",dims); candidate.Add("tier","unknown"); candidates.Add(candidate);
		ValueMap candidate2=candidate; candidate2.Set("candidate_id","synthetic-candidate-2"); candidates.Add(candidate2);
		projection.Add("findings",findings); projection.Add("category_assessments",categories); projection.Add("candidate_assessments",candidates);
		ValueArray records=AssessmentService::BuildRecords(projection);
		String t61_root=workspace.FixtureRoot("t61-assessment-schema"); if(!CreatePrivateDirectory(t61_root)) return false;
		for(int i=0;i<records.GetCount();i++) {
			ValueMap record=records[i]; String path=AppendFileName(t61_root,Format("record-%d.json",i));
			if(!SaveFile(path,AsJSON(record,true))) return false;
		}
		DataStore validation; bool loaded=validation.LoadFixtureForTests(t61_root); if(!loaded||validation.GetDocuments().GetCount()!=records.GetCount()) {
			Cerr()<<"FAILED: T61 emitted record fixture load count loaded="<<loaded<<" records="<<records.GetCount()<<" indexed="<<validation.GetDocuments().GetCount()<<"\n";
			for(const String& diagnostic:validation.GetSourceDiagnostics()) Cerr()<<"T61 schema diagnostic="<<diagnostic<<"\n";
			return false;
		}
		for(const DataDocument& record:validation.GetDocuments()) if(!record.valid) {
			Cerr()<<"FAILED: T61 emitted schema "<<record.kind<<" invalid: "<<record.invalid_reason<<"\n"; return false;
		}
		Cout()<<"ok    T61 analysis/finding/category/candidate envelopes pass isolated DataStore schema validation\n";
	}
	// T62 refresh and override records use only isolated synthetic writable roots.
	{
		const String ws="synthetic-t62-workspace", source="synthetic-t62-source";
		String root=workspace.FixtureRoot("t62-assessment-write");if(!CreatePrivateDirectory(root))return false;
		DataStore writer;if(!writer.LoadWriteFixtureForTests(root,source))return false;
		DataWriteAuthorization auth;auth.authenticated=true;auth.action_scope="record:write";auth.principal="synthetic-t62-actor";auth.source_id=source;auth.workspace_id=ws;
		ValueMap rubric;rubric.Add("id","synthetic-t62-rubric");rubric.Add("workspace_id",ws);rubric.Add("revision","r1");rubric.Add("score_scale","integer-0-10");rubric.Add("total_rule","weighted-mean");
		ValueArray dims,cov,tiers;for(int i=0;i<6;i++){ValueMap d;d.Add("id",Format("axis-%d",i));d.Add("weight",1);d.Add("minimum_score",0);d.Add("maximum_score",10);d.Add("minimum_evidence_count",1);dims.Add(d);cov.Add(Format("axis-%d",i));}
		rubric.Add("dimensions",dims);rubric.Add("coverage_required",cov);ValueMap tier;tier.Add("id","band");tier.Add("minimum_numerator",0);tier.Add("minimum_denominator",1);tier.Add("maximum_numerator",10);tier.Add("maximum_denominator",1);tiers.Add(tier);rubric.Add("tiers",tiers);
		auto make_inputs=[&](const String& revision,const String& tool,const String& candidate_id,const String& source_id){ValueMap in;in.Add("workspace_id",ws);in.Add("tool_config",tool);in.Add("model_config","unconfigured");ValueArray sources,evidence,scores,pins,rels;
			ValueMap src;src.Add("workspace_id",ws);src.Add("identity",source_id);src.Add("revision",revision);sources.Add(src);ValueMap pin;pin.Add("workspace_id",ws);pin.Add("id",source_id);pin.Add("revision",revision);pins.Add(pin);
			for(int i=0;i<6;i++){String eid=Format("evidence-%d",i);ValueMap e;e.Add("workspace_id",ws);e.Add("item_id",eid);e.Add("snapshot_id",eid);e.Add("source_identity",source_id);e.Add("source_revision",revision);e.Add("resolution","resolved");e.Add("visibility","visible");evidence.Add(e);ValueMap score;score.Add("workspace_id",ws);score.Add("candidate_id",candidate_id);score.Add("dimension_id",Format("axis-%d",i));score.Add("score","5");score.Add("provenance","computed");ValueArray refs;refs.Add(eid);score.Add("evidence_ids",refs);scores.Add(score);ValueMap ep;ep.Add("workspace_id",ws);ep.Add("id",eid);ep.Add("revision",revision);pins.Add(ep);}
			in.Add("sources",sources);in.Add("evidence",evidence);in.Add("scores",scores);in.Add("input_revisions",pins);in.Add("relationships",rels);return in;};
		auto append_source=[&](ValueMap in,const String& identity,const String& revision){ValueArray sources=MapGet(in,"sources"),pins=MapGet(in,"input_revisions");ValueMap s;s.Add("workspace_id",ws);s.Add("identity",identity);s.Add("revision",revision);sources.Add(s);ValueMap pin;pin.Add("workspace_id",ws);pin.Add("id",identity);pin.Add("revision",revision);pins.Add(pin);in.Set("sources",sources);in.Set("input_revisions",pins);return in;};
		ValueMap inputs=make_inputs("r1","tool-a","candidate-a","source-a");AssessmentService::OperationResult first=AssessmentService::Refresh(writer,rubric,inputs,auth);
		if(first.status!="committed"||first.changed_ids.GetCount()>64||writer.RecoverPendingBatches(auth).status!="recovered"){Cerr()<<"FAILED T62 initial refresh: "<<first.status<<" ids="<<first.changed_ids.GetCount()<<"\n";return false;}
		DataStore current;if(!current.LoadWriteFixtureForTests(root,source)||current.GetLoadStatus()!="loaded"){Cerr()<<"FAILED T62 reload: "<<current.GetLoadStatus()<<"\n";return false;}
		int initial_count=current.GetDocuments().GetCount();AssessmentService::OperationResult retry=AssessmentService::Refresh(current,rubric,inputs,auth);
		if(retry.status!="already_current"||current.GetDocuments().GetCount()!=initial_count){Cerr()<<"FAILED T62 retry: "<<retry.status<<" count="<<current.GetDocuments().GetCount()<<" initial="<<initial_count<<"\n";return false;}
		ValueMap huge_inputs=inputs;ValueArray huge_scores=MapGet(inputs,"scores");for(int c=0;c<11;c++)for(int d=0;d<6;d++){ValueMap score=huge_scores[d];score.Set("candidate_id",Format("candidate-extra-%d",c));huge_scores.Add(score);}huge_inputs.Set("scores",huge_scores);
		AssessmentService::OperationResult oversized=AssessmentService::Refresh(current,rubric,huge_inputs,auth);
		DataWriteAuthorization bad_refresh_source=auth;bad_refresh_source.source_id="synthetic-t62-other";DataWriteAuthorization bad_refresh_workspace=auth;bad_refresh_workspace.workspace_id="synthetic-t62-other";
		AssessmentService::OperationResult refresh_source_denied=AssessmentService::Refresh(current,rubric,inputs,bad_refresh_source);
		AssessmentService::OperationResult refresh_workspace_denied=AssessmentService::Refresh(current,rubric,inputs,bad_refresh_workspace);
		DataStore readonly_view;if(!readonly_view.LoadFixtureForTests(root)){Cerr()<<"FAILED T62 readonly load: "<<readonly_view.GetLoadStatus()<<"\n";return false;}
		AssessmentService::OperationResult incomplete_denied=AssessmentService::Refresh(readonly_view,rubric,inputs,auth);
		String bad_root=workspace.FixtureRoot("t62-incomplete-root");if(!CreatePrivateDirectory(bad_root)||!SaveFile(AppendFileName(bad_root,"broken.json"),"{ malformed synthetic record")){Cerr()<<"FAILED T62 malformed fixture setup\n";return false;}
		DataStore bad_view;bad_view.LoadWriteFixtureForTests(bad_root,source);AssessmentService::OperationResult malformed_denied=AssessmentService::Refresh(bad_view,rubric,inputs,auth);
		String pending_root=workspace.FixtureRoot("t62-pending-root");if(!CreatePrivateDirectory(pending_root)||!CreatePrivateDirectory(AppendFileName(pending_root,".mgr-journal"))){Cerr()<<"FAILED T62 pending fixture setup\n";return false;}
		DataStore pending_view;pending_view.LoadWriteFixtureForTests(pending_root,source);AssessmentService::OperationResult pending_denied=AssessmentService::Refresh(pending_view,rubric,inputs,auth);
		if(oversized.status!="batch_too_large"||current.GetDocuments().GetCount()!=initial_count||refresh_source_denied.status!="denied"||refresh_workspace_denied.status!="denied"||incomplete_denied.status!="denied"||bad_view.GetLoadStatus()=="loaded"||malformed_denied.status!="denied"||pending_view.GetLoadStatus()=="loaded"||pending_denied.status!="denied"){Cerr()<<"FAILED T62 size/scope/view fail-closed checks: "<<oversized.status<<"/"<<refresh_source_denied.status<<"/"<<refresh_workspace_denied.status<<"/"<<incomplete_denied.status<<"/"<<bad_view.GetLoadStatus()<<"/"<<malformed_denied.status<<"/"<<pending_view.GetLoadStatus()<<"/"<<pending_denied.status<<"\n";return false;}
		ValueMap unrelated=make_inputs("r1","tool-a","candidate-b","source-b");AssessmentService::OperationResult independent=AssessmentService::Refresh(current,rubric,unrelated,auth);
		String recovery_status=current.RecoverPendingBatches(auth).status;if(independent.status!="committed"||recovery_status!="recovered"){Cerr()<<"FAILED T62 independent refresh: "<<independent.status<<"/"<<recovery_status<<"\n";return false;}
		DataStore independent_view;if(!independent_view.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 independent reload\n";return false;}
		int independent_markers=0;for(const DataDocument& d:independent_view.GetDocuments())if(d.kind=="assessment-staleness")independent_markers++;
		if(independent_markers!=0){Cerr()<<"FAILED T62 unrelated candidate/source was marked stale\n";return false;}
		current=independent_view;
		ValueMap shared_revision=make_inputs("r2","tool-a","candidate-c","source-a");AssessmentService::OperationResult shared_refresh=AssessmentService::Refresh(current,rubric,shared_revision,auth);
		recovery_status=current.RecoverPendingBatches(auth).status;if(shared_refresh.status!="committed"||recovery_status!="recovered"){Cerr()<<"FAILED T62 shared refresh: "<<shared_refresh.status<<"/"<<recovery_status<<"\n";return false;}
		DataStore shared_view;if(!shared_view.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 shared reload\n";return false;}bool shared_stale=false,unrelated_stale=false;
		for(const DataDocument& d:shared_view.GetDocuments())if(d.kind=="assessment-staleness"&&MapGetString(d.data,"replacement_run_id")==shared_refresh.run_id){if(MapGetString(d.data,"prior_run_id")==first.run_id){ValueMap f=MapGet(d.data,"reason_flags");shared_stale=MapGet(f,"source_revision_changed");}if(MapGetString(d.data,"prior_run_id")==independent.run_id)unrelated_stale=true;}
		if(!shared_stale||unrelated_stale){Cerr()<<"FAILED T62 shared-source revision scope\n";return false;}current=shared_view;
		ValueMap with_extra=append_source(make_inputs("r1","tool-a","candidate-a","source-a"),"source-extra","x1");
		AssessmentService::OperationResult added_source=AssessmentService::Refresh(current,rubric,with_extra,auth);
		recovery_status=current.RecoverPendingBatches(auth).status;if(added_source.status!="committed"||recovery_status!="recovered"){Cerr()<<"FAILED T62 source-add refresh: "<<added_source.status<<"/"<<recovery_status<<"\n";return false;}
		DataStore added_view;if(!added_view.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 source-add reload\n";return false;}bool addition_marked=false;
		for(const DataDocument& d:added_view.GetDocuments())if(d.kind=="assessment-staleness"&&MapGetString(d.data,"prior_run_id")==first.run_id&&MapGetString(d.data,"replacement_run_id")==added_source.run_id){ValueMap f=MapGet(d.data,"reason_flags");addition_marked=MapGet(f,"source_dependency_set_changed");}
		if(!addition_marked){Cerr()<<"FAILED T62 source addition not stale\n";return false;}current=added_view;
		ValueMap removed_source=make_inputs("r1","tool-a","candidate-a","source-a");AssessmentService::OperationResult removed=AssessmentService::Refresh(current,rubric,removed_source,auth);
		recovery_status=current.RecoverPendingBatches(auth).status;if(removed.status!="committed"||recovery_status!="recovered"){Cerr()<<"FAILED T62 source-remove refresh: "<<removed.status<<"/"<<recovery_status<<"\n";return false;}
		DataStore removed_view;if(!removed_view.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 source-remove reload\n";return false;}bool removal_marked=false;
		for(const DataDocument& d:removed_view.GetDocuments())if(d.kind=="assessment-staleness"&&MapGetString(d.data,"prior_run_id")==added_source.run_id&&MapGetString(d.data,"replacement_run_id")==removed.run_id){ValueMap f=MapGet(d.data,"reason_flags");removal_marked=MapGet(f,"source_dependency_set_changed");}
		if(!removal_marked){Cerr()<<"FAILED T62 source removal not stale\n";return false;}current=removed_view;
		String prior_run_path;for(const DataDocument& d:current.GetDocuments())if(d.kind=="analysis-run"&&d.id==first.run_id)prior_run_path=d.path;if(prior_run_path.IsEmpty()){Cerr()<<"FAILED T62 prior run missing\n";return false;}
		String prior_bytes=LoadFile(prior_run_path);ValueMap rubric2=rubric;rubric2.Set("revision","r2");ValueMap changed=make_inputs("r2","tool-b","candidate-a","source-a");changed.Set("model_config","model-b");
		AssessmentService::OperationResult refreshed=AssessmentService::Refresh(current,rubric2,changed,auth);String changed_recovery=current.RecoverPendingBatches(auth).status;if(refreshed.status!="committed"||refreshed.run_id==first.run_id||LoadFile(prior_run_path)!=prior_bytes||changed_recovery!="recovered"){Cerr()<<"FAILED T62 changed refresh: "<<refreshed.status<<"/"<<changed_recovery<<" run="<<refreshed.run_id<<"\n";return false;}
		DataStore latest;if(!latest.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 latest reload: "<<latest.GetLoadStatus()<<"\n";return false;}int stale_count=0;const DataDocument *candidate=NULL;
		for(const DataDocument& d:latest.GetDocuments()){if(d.kind=="assessment-staleness"&&d.valid&&d.validated)stale_count++;if(d.kind=="candidate-assessment"&&MapGetString(d.data,"analysis_run")==refreshed.run_id)candidate=&d;}
		const DataDocument *stale_marker=NULL;for(const DataDocument& d:latest.GetDocuments())if(d.kind=="assessment-staleness"&&MapGetString(d.data,"prior_run_id")==added_source.run_id&&MapGetString(d.data,"replacement_run_id")==refreshed.run_id)stale_marker=&d;
		ValueMap stale_flags;if(stale_marker)stale_flags=MapGet(stale_marker->data,"reason_flags");
		auto true_flag=[](const ValueMap& map,const String& key){return AsJSON(MapGet(map,key),true)=="true";};
		if(stale_count<3||!candidate||!stale_marker||!true_flag(stale_flags,"source_revision_changed")||!true_flag(stale_flags,"source_dependency_set_changed")||!true_flag(stale_flags,"input_revision_changed")||!true_flag(stale_flags,"rubric_revision_changed")||!true_flag(stale_flags,"tool_config_changed")||!true_flag(stale_flags,"model_config_changed")){Cerr()<<"FAILED T62 scoped staleness records="<<stale_count<<" candidate="<<(candidate!=NULL)<<" flags="<<AsJSON(stale_flags,true)<<" docs="<<latest.GetDocuments().GetCount()<<"\n";for(const DataDocument& d:latest.GetDocuments())if(!d.valid)Cerr()<<"T62 invalid "<<d.kind<<" "<<d.invalid_reason<<"\n";return false;}
		String target_id=candidate->id,target_revision=candidate->revision,old_candidate=LoadFile(candidate->path);
		String old_target_id,old_target_revision;for(const DataDocument& d:latest.GetDocuments())if(d.kind=="candidate-assessment"&&MapGetString(d.data,"analysis_run")==first.run_id){old_target_id=d.id;old_target_revision=d.revision;}
		const DataDocument *old_target=latest.FindDocument(ws+":"+old_target_id);if(!old_target){Cerr()<<"FAILED T62 old candidate lookup: "<<old_target_id<<"\n";return false;}String old_candidate_bytes=LoadFile(old_target->path);
		AssessmentService::OperationResult stale_old_override=AssessmentService::Override(latest,rubric2,changed,ws,old_target_id,old_target_revision,"axis-0","7","","stale target reason","synthetic-t62-actor","2026-01-01T00:00:00Z","stale-old-target",auth);
		AssessmentService::OverrideHistory stale_old_history=AssessmentService::GetOverrideHistory(latest,rubric2,changed,old_target_id,old_target_revision,"axis-0","missing-head");
		if(stale_old_override.status!="stale_target"||stale_old_history.status!="stale_target"||LoadFile(old_target->path)!=old_candidate_bytes){Cerr()<<"FAILED T62 stale old candidate: "<<stale_old_override.status<<"/"<<stale_old_history.status<<"\n";return false;}
		AssessmentService::OperationResult over1=AssessmentService::Override(latest,rubric2,changed,ws,target_id,target_revision,"axis-0","7","","synthetic reason","synthetic-t62-actor","2026-01-01T00:00:00Z","override-one",auth);
		if(over1.status!="committed"||latest.RecoverPendingBatches(auth).status!="recovered"){Cerr()<<"FAILED T62 override one: "<<over1.status<<"\n";return false;}
		DataStore after1;if(!after1.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 after override reload\n";return false;}
		AssessmentService::OperationResult over1retry=AssessmentService::Override(after1,rubric2,changed,ws,target_id,target_revision,"axis-0","7","","synthetic reason","synthetic-t62-actor","2026-01-01T00:00:00Z","override-one",auth);
		AssessmentService::OperationResult reason_conflict=AssessmentService::Override(after1,rubric2,changed,ws,target_id,target_revision,"axis-0","7","","changed reason","synthetic-t62-actor","2026-01-01T00:00:00Z","override-one",auth);
		AssessmentService::OperationResult wrong_actor=AssessmentService::Override(after1,rubric2,changed,ws,target_id,target_revision,"axis-0","7","","synthetic reason","different-actor","2026-01-01T00:00:00Z","wrong-actor",auth);
		AssessmentService::OperationResult missing_head=AssessmentService::Override(after1,rubric2,changed,ws,target_id,target_revision,"axis-0","7","missing-override-head","reason","synthetic-t62-actor","2026-01-01T00:00:00Z","missing-head",auth);
		String first_override=over1.changed_ids.IsEmpty()?String():over1.changed_ids[0].Mid(ws.GetCount()+1);
		if(over1retry.status!="already_committed"||reason_conflict.status!="idempotency_conflict"||wrong_actor.status!="denied"||missing_head.status!="override_head_conflict"||LoadFile(candidate->path)!=old_candidate){Cerr()<<"FAILED T62 override retry/conflict: "<<over1retry.status<<"/"<<reason_conflict.status<<"/"<<wrong_actor.status<<"/"<<missing_head.status<<"\n";return false;}
		AssessmentService::OperationResult over2=AssessmentService::Override(after1,rubric2,changed,ws,target_id,target_revision,"axis-0","8",first_override,"second synthetic reason","synthetic-t62-actor","2026-01-01T00:00:01Z","override-two",auth);
		if(over2.status!="committed"||after1.RecoverPendingBatches(auth).status!="recovered"){Cerr()<<"FAILED T62 override two: "<<over2.status<<"\n";return false;}
		DataStore after2;if(!after2.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 after second override reload\n";return false;}
		String latest_override=over2.changed_ids.IsEmpty()?String():over2.changed_ids[0].Mid(ws.GetCount()+1);
		AssessmentService::OverrideHistory history=AssessmentService::GetOverrideHistory(after2,rubric2,changed,target_id,target_revision,"axis-0",latest_override);
		ValueMap event_one=history.events.GetCount()>0?(ValueMap)history.events[0]:ValueMap(),event_two=history.events.GetCount()>1?(ValueMap)history.events[1]:ValueMap();
		AssessmentService::OperationResult over2retry=AssessmentService::Override(after2,rubric2,changed,ws,target_id,target_revision,"axis-0","8",first_override,"second synthetic reason","synthetic-t62-actor","2026-01-01T00:00:01Z","override-two",auth);
		AssessmentService::OperationResult over2reason_conflict=AssessmentService::Override(after2,rubric2,changed,ws,target_id,target_revision,"axis-0","8",first_override,"changed second reason","synthetic-t62-actor","2026-01-01T00:00:01Z","override-two",auth);
		AssessmentService::OperationResult over2predecessor_conflict=AssessmentService::Override(after2,rubric2,changed,ws,target_id,target_revision,"axis-0","8","","second synthetic reason","synthetic-t62-actor","2026-01-01T00:00:01Z","override-two",auth);
		if(over2retry.status!="already_committed"||over2reason_conflict.status!="idempotency_conflict"||over2predecessor_conflict.status!="idempotency_conflict"){Cerr()<<"FAILED T62 second override retry/conflict: "<<over2retry.status<<"/"<<over2reason_conflict.status<<"/"<<over2predecessor_conflict.status<<"\n";return false;}
		int before_bad_schema=after2.GetDocuments().GetCount();ValueMap bad_marker=stale_marker->data;String bad_marker_id="assessment-staleness-test-extra";bad_marker.Set("id",bad_marker_id);ValueMap bad_env=MapGet(bad_marker,"envelope");bad_env.Set("stable_id",bad_marker_id);bad_marker.Set("envelope",bad_env);ValueMap bad_flags=MapGet(bad_marker,"reason_flags");bad_flags.Add("unregistered_reason",true);bad_marker.Set("reason_flags",bad_flags);
		DataBatchChange bad_marker_change;bad_marker_change.action=DATA_BATCH_CREATE;bad_marker_change.qualified_id=ws+":"+bad_marker_id;bad_marker_change.proposed=bad_marker;Vector<DataBatchChange> bad_marker_batch;bad_marker_batch.Add(bad_marker_change);
		String bad_marker_status=after2.PublishBatch(auth,"t62-bad-reason-flags",bad_marker_batch).status;if(bad_marker_status=="committed"||after2.GetDocuments().GetCount()!=before_bad_schema){Cerr()<<"FAILED T62 reason flag schema: "<<bad_marker_status<<"\n";return false;}
		auto rejects_override_schema=[&](ValueMap rec,const String& id,const String& op){rec.Set("id",id);ValueMap env=MapGet(rec,"envelope");env.Set("stable_id",id);rec.Set("envelope",env);DataBatchChange c;c.action=DATA_BATCH_CREATE;c.qualified_id=ws+":"+id;c.proposed=rec;Vector<DataBatchChange> batch;batch.Add(c);return after2.PublishBatch(auth,op,batch).status!="committed"&&after2.GetDocuments().GetCount()==before_bad_schema;};
		ValueMap missing_computed=event_two;missing_computed.Remove(missing_computed.Find("computed_value"));
		ValueMap bad_authority=event_two;bad_authority.Set("authority","record:admin");ValueMap bad_field=event_two;bad_field.Set("field","acceptance");
		ValueMap long_reason=event_two;long_reason.Set("reason",String('r',4097));ValueMap long_time=event_two;long_time.Set("timestamp","2026-01-01T00:00:00.000Z");
		ValueMap missing_flag_marker=stale_marker->data;String missing_flag_id="assessment-staleness-test-missing-flag";missing_flag_marker.Set("id",missing_flag_id);ValueMap missing_flag_env=MapGet(missing_flag_marker,"envelope");missing_flag_env.Set("stable_id",missing_flag_id);missing_flag_marker.Set("envelope",missing_flag_env);ValueMap missing_flags=MapGet(missing_flag_marker,"reason_flags");missing_flags.Remove(missing_flags.Find("model_config_changed"));missing_flag_marker.Set("reason_flags",missing_flags);
		if(!rejects_override_schema(missing_computed,"assessment-override-test-missing-computed","t62-schema-required")||!rejects_override_schema(bad_authority,"assessment-override-test-authority-enum","t62-schema-authority")||!rejects_override_schema(bad_field,"assessment-override-test-field-enum","t62-schema-field")||!rejects_override_schema(long_reason,"assessment-override-test-long-reason","t62-schema-long-reason")||!rejects_override_schema(long_time,"assessment-override-test-long-time","t62-schema-long-time")){Cerr()<<"FAILED T62 override required/bound/enum schema checks\n";return false;}
		DataBatchChange missing_flag_change;missing_flag_change.action=DATA_BATCH_CREATE;missing_flag_change.qualified_id=ws+":"+missing_flag_id;missing_flag_change.proposed=missing_flag_marker;Vector<DataBatchChange> missing_flag_batch;missing_flag_batch.Add(missing_flag_change);
		if(after2.PublishBatch(auth,"t62-schema-missing-flag",missing_flag_batch).status=="committed"||after2.GetDocuments().GetCount()!=before_bad_schema){Cerr()<<"FAILED T62 required reason flag schema\n";return false;}
		DataWriteAuthorization wrong_source=auth;wrong_source.source_id="synthetic-other-source";DataWriteAuthorization wrong_workspace=auth;wrong_workspace.workspace_id="synthetic-other-workspace";
		AssessmentService::OperationResult stale=AssessmentService::Override(after2,rubric2,changed,ws,target_id,"999","axis-0","9",latest_override,"reason","synthetic-t62-actor","2026-01-01T00:00:02Z","stale-target",auth);
		AssessmentService::OperationResult denied_source=AssessmentService::Override(after2,rubric2,changed,ws,target_id,target_revision,"axis-0","9",latest_override,"reason","synthetic-t62-actor","2026-01-01T00:00:02Z","denied-source",wrong_source);
		AssessmentService::OperationResult denied_workspace=AssessmentService::Override(after2,rubric2,changed,ws,target_id,target_revision,"axis-0","9",latest_override,"reason","synthetic-t62-actor","2026-01-01T00:00:02Z","denied-workspace",wrong_workspace);
		AssessmentService::OperationResult empty_reason=AssessmentService::Override(after2,rubric2,changed,ws,target_id,target_revision,"axis-0","9",latest_override,"","synthetic-t62-actor","2026-01-01T00:00:02Z","empty-reason",auth);
		AssessmentService::OperationResult invalid_timestamp=AssessmentService::Override(after2,rubric2,changed,ws,target_id,target_revision,"axis-0","9",latest_override,"reason","synthetic-t62-actor","2026-02-31T00:00:02Z","invalid-time",auth);
		if(history.status!="ok"||history.events.GetCount()!=2||history.computed_value!="5"||MapGetString(event_one,"computed_value")!="5"||MapGetString(event_one,"prior_value")!="5"||MapGetString(event_one,"field")!="score"||MapGetString(event_one,"authority")!="record:write"||MapGetString(event_one,"timestamp")!="2026-01-01T00:00:00Z"||MapGetString(event_two,"computed_value")!="5"||MapGetString(event_two,"prior_value")!="7"||stale.status!="stale_target"||denied_source.status!="denied"||denied_workspace.status!="denied"||empty_reason.status!="denied"||invalid_timestamp.status!="denied"){Cerr()<<"FAILED T62 history/denial checks: history="<<history.status<<" events="<<history.events.GetCount()<<" computed="<<history.computed_value<<" stale="<<stale.status<<" source="<<denied_source.status<<" workspace="<<denied_workspace.status<<" reason="<<empty_reason.status<<" time="<<invalid_timestamp.status<<"\n";return false;}
		auto make_override_record=[&](const String& id,const String& dimension,const String& previous,const String& prior,const String& replacement,const String& op){ValueMap rec=event_one;rec.Set("id",id);rec.Set("dimension_id",dimension);rec.Set("previous_override_id",previous);rec.Set("computed_value","5");rec.Set("prior_value",prior);rec.Set("replacement",replacement);rec.Set("operation_id",op);ValueMap env=MapGet(rec,"envelope");env.Set("stable_id",id);rec.Set("envelope",env);return rec;};
		String orphan_id="assessment-override-t62-orphan";ValueMap orphan=make_override_record(orphan_id,"axis-0","unlinked-parent","5","6","t62-orphan-op");DataBatchChange orphan_change;orphan_change.action=DATA_BATCH_CREATE;orphan_change.qualified_id=ws+":"+orphan_id;orphan_change.proposed=orphan;Vector<DataBatchChange> orphan_batch;orphan_batch.Add(orphan_change);
		String orphan_status=after2.PublishBatch(auth,"t62-orphan-chain",orphan_batch).status;String orphan_recovery=after2.RecoverPendingBatches(auth).status;if(orphan_status!="committed"||orphan_recovery!="recovered"){Cerr()<<"FAILED T62 orphan fixture publish: "<<orphan_status<<"/"<<orphan_recovery<<"\n";return false;}
		DataStore orphan_view;if(!orphan_view.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 orphan reload\n";return false;}String orphan_history=AssessmentService::GetOverrideHistory(orphan_view,rubric2,changed,target_id,target_revision,"axis-0",latest_override).status;if(orphan_history!="missing_link"){Cerr()<<"FAILED T62 disconnected history: "<<orphan_history<<"\n";return false;}
		String cycle_root_id="assessment-override-t62-cycle-root",cycle_a="assessment-override-t62-cycle-a",cycle_b="assessment-override-t62-cycle-b";
		ValueMap cycle_root=make_override_record(cycle_root_id,"axis-2","none","5","5","t62-cycle-root");
		ValueMap cycle_rec_a=make_override_record(cycle_a,"axis-2",cycle_b,"5","6","t62-cycle-a"),cycle_rec_b=make_override_record(cycle_b,"axis-2",cycle_a,"6","5","t62-cycle-b");
		Vector<DataBatchChange> cycle_batch;for(const ValueMap& rec:{cycle_root,cycle_rec_a,cycle_rec_b}){DataBatchChange c;c.action=DATA_BATCH_CREATE;c.qualified_id=ws+":"+MapGetString(rec,"id");c.proposed=rec;cycle_batch.Add(c);}
		String cycle_status=orphan_view.PublishBatch(auth,"t62-cycle-chain",cycle_batch).status;String cycle_recovery=orphan_view.RecoverPendingBatches(auth).status;if(cycle_status!="committed"||cycle_recovery!="recovered"){Cerr()<<"FAILED T62 cycle fixture: "<<cycle_status<<"/"<<cycle_recovery<<"\n";return false;}
		DataStore cycle_view;if(!cycle_view.LoadWriteFixtureForTests(root,source)){Cerr()<<"FAILED T62 cycle reload\n";return false;}String cycle_history=AssessmentService::GetOverrideHistory(cycle_view,rubric2,changed,target_id,target_revision,"axis-2",cycle_root_id).status;if(cycle_history!="cycle"){Cerr()<<"FAILED T62 cyclic history: "<<cycle_history<<"\n";return false;}
		ValueMap fork_record;for(const DataDocument& d:after2.GetDocuments())if(d.kind=="assessment-override"&&d.id==first_override)fork_record=d.data;
		String fork_id="assessment-override-synthetic-fork";fork_record.Set("id",fork_id);fork_record.Set("previous_override_id","none");ValueMap fork_env=MapGet(fork_record,"envelope");fork_env.Set("stable_id",fork_id);fork_record.Set("envelope",fork_env);
		DataBatchChange fork_change;fork_change.action=DATA_BATCH_CREATE;fork_change.qualified_id=ws+":"+fork_id;fork_change.proposed=fork_record;Vector<DataBatchChange> fork_changes;fork_changes.Add(fork_change);
		if(after2.PublishBatch(auth,"t62-synthetic-fork",fork_changes).status!="committed"||after2.RecoverPendingBatches(auth).status!="recovered")return false;
		DataStore fork_view;if(!fork_view.LoadWriteFixtureForTests(root,source))return false;
		if(AssessmentService::GetOverrideHistory(fork_view,rubric2,changed,target_id,target_revision,"axis-0",latest_override).status!="fork"){Cerr()<<"FAILED T62 fork history did not fail closed\n";return false;}
		String boundary_root_id="assessment-override-t62-boundary-root";ValueMap boundary_record=make_override_record(boundary_root_id,"axis-1","none","5","5","t62-boundary-root-op");
		// Filesystem probing found /tmp EDQUOT (errno 122) at 27 records: 129-byte path, 18-byte leaf, 991-byte JSON payload. Keep the limit-only fixture in the pinned synthetic store's memory.
		DataStore boundary_view=fork_view;
		auto append_synthetic_event=[&](DataStore& view,const ValueMap& record){DataDocument doc;doc.source_id=source;doc.read_only=false;doc.id=MapGetString(record,"id");doc.qualified_id=ws+":"+doc.id;doc.workspace_id=ws;doc.kind="assessment-override";doc.revision="1";doc.data=record;doc.valid=true;doc.validated=true;doc.validation_state="valid";view.docs.Add(doc);};
		append_synthetic_event(boundary_view,boundary_record);
		String boundary_head=boundary_root_id,previous_score="5";int event_number=0;Index<String> fixture_ids;
		for(;event_number<AssessmentService::MAX_OVERRIDE_HISTORY-1;event_number++){
			String id=Format("assessment-override-t62-boundary-%04d",event_number);String op=Format("t62-boundary-event-%04d",event_number);
			ValueMap rec=make_override_record(id,"axis-1",boundary_head,previous_score,"5",op);
			if(fixture_ids.Find(id)>=0){Cerr()<<"FAILED T62 in-memory boundary duplicate id="<<id<<" event="<<event_number<<"\n";return false;}fixture_ids.Add(id);
			String record_bytes=AsJSON(rec,true);if(record_bytes.GetCount()>DataStore::MAX_SOURCE_FILE_BYTES){Cerr()<<"FAILED T62 synthetic boundary record id="<<id<<" record_bytes="<<record_bytes.GetCount()<<"\n";return false;}
			append_synthetic_event(boundary_view,rec);
			boundary_head=id;
		}
		AssessmentService::OverrideHistory boundary_history=AssessmentService::GetOverrideHistory(boundary_view,rubric2,changed,target_id,target_revision,"axis-1",boundary_head);
		if(boundary_history.status!="ok"||boundary_history.events.GetCount()!=AssessmentService::MAX_OVERRIDE_HISTORY){Cerr()<<"FAILED T62 history boundary: "<<boundary_history.status<<" count="<<boundary_history.events.GetCount()<<"\n";return false;}
		String over_limit_id="assessment-override-t62-boundary-over";ValueMap over_limit=make_override_record(over_limit_id,"axis-1",boundary_head,"5","5","t62-history-over-op");
		if(fixture_ids.Find(over_limit_id)>=0){Cerr()<<"FAILED T62 over-limit synthetic duplicate id="<<over_limit_id<<"\n";return false;}
		append_synthetic_event(boundary_view,over_limit);String over_history=AssessmentService::GetOverrideHistory(boundary_view,rubric2,changed,target_id,target_revision,"axis-1",over_limit_id).status;if(over_history!="history_limit"){Cerr()<<"FAILED T62 history over limit: "<<over_history<<"\n";return false;}
		Cout()<<"ok    T62 pinned refresh idempotency/staleness and append-only override chain/denials\n";
	}
	// T15 integration tests exercise real PublishBatch creates in an isolated root.
	{
		const String t15_workspace = "synthetic-t15-workspace";
		const String t15_source = "synthetic-t15-source";
		const String t15_root = workspace.FixtureRoot("t15-migration-write");
		if(!CreatePrivateDirectory(t15_root)) return false;
		DataStore t15_store;
		if(!t15_store.LoadWriteFixtureForTests(t15_root, t15_source)) return false;
		DataWriteAuthorization t15_auth;
		t15_auth.authenticated = true; t15_auth.action_scope = "record:write";
		t15_auth.principal = "synthetic-t15-operator"; t15_auth.source_id = t15_source;
		t15_auth.workspace_id = t15_workspace;
		ValueMap task_assertion; task_assertion.Add("source_id", "synthetic-task-assertion");
		task_assertion.Add("candidate_id", "synthetic-candidate"); task_assertion.Add("candidate_kind", "research-stage");
		task_assertion.Add("origin", "task-record"); task_assertion.Add("rubric", "synthetic-rubric");
		ValueMap task_scores; task_scores.Add("synthetic-category", "1"); task_assertion.Add("category_scores", task_scores);
		String origin_id = "synthetic-t15-origin";
		ValueArray links; links.Add(t15_workspace + ":" + origin_id); task_assertion.Add("original_links", links);
		ValueMap profile_assertion = task_assertion; profile_assertion.Set("source_id", "synthetic-profile-assertion");
		profile_assertion.Set("origin", "authored-profile"); ValueMap profile_scores; profile_scores.Add("synthetic-category", "2");
		profile_assertion.Set("category_scores", profile_scores);
		ValueArray t15_assertions; t15_assertions.Add(task_assertion); t15_assertions.Add(profile_assertion);
		ValueMap t15_snapshot; t15_snapshot.Add("snapshot_id", "synthetic-t15-snapshot");
		t15_snapshot.Add("snapshot_revision", "synthetic-revision"); t15_snapshot.Add("assertions", t15_assertions);
		String t15_operation = HistoricalAssessmentMigration::OperationId(t15_snapshot, t15_workspace, t15_source);
		if(t15_operation.IsEmpty()) return false;
		DataWriteAuthorization writer_denied, source_denied = t15_auth, workspace_denied = t15_auth;
		source_denied.source_id = "synthetic-other-source";
		workspace_denied.workspace_id = "synthetic-other-workspace";
		DataBatchResult denied_write = HistoricalAssessmentMigration::Import(t15_snapshot, t15_workspace, t15_source,
			 t15_operation, writer_denied, t15_store);
		DataBatchResult denied_source = HistoricalAssessmentMigration::Import(t15_snapshot, t15_workspace, t15_source,
			 t15_operation, source_denied, t15_store);
		DataBatchResult denied_workspace = HistoricalAssessmentMigration::Import(t15_snapshot, t15_workspace, t15_source,
			 t15_operation, workspace_denied, t15_store);
		if(denied_write.status != "denied" || denied_source.status != "denied" || denied_workspace.status != "denied" ||
		   !denied_write.operation_id.IsEmpty() || !denied_source.changed_ids.IsEmpty() || !denied_workspace.changed_ids.IsEmpty()) {
			Cerr() << "FAILED: T15 import authorization denial or result redaction\n"; return false;
		}
		ValueMap duplicate_profile = profile_assertion;
		duplicate_profile.Set("source_id", "synthetic-task-assertion");
		ValueArray duplicate_assertions; duplicate_assertions.Add(task_assertion); duplicate_assertions.Add(duplicate_profile);
		ValueMap duplicate_snapshot = t15_snapshot; duplicate_snapshot.Set("assertions", duplicate_assertions);
		DataBatchResult duplicate_import = HistoricalAssessmentMigration::Import(duplicate_snapshot, t15_workspace, t15_source,
			 HistoricalAssessmentMigration::OperationId(duplicate_snapshot, t15_workspace, t15_source), t15_auth, t15_store);
		if(duplicate_import.status != "conflict" || !duplicate_import.changed_ids.IsEmpty() ||
		   !t15_store.GetDocuments().IsEmpty() || DirectoryExists(AppendFileName(t15_root, "records"))) {
			Cerr() << "FAILED: T15 duplicate assertion source IDs were not blocked before create\n"; return false;
		}
		ValueMap origin_record; origin_record.Add("id", origin_id); origin_record.Add("kind", "task-proposal");
		origin_record.Add("title", "Synthetic linked origin"); origin_record.Add("status", "proposed");
		ValueMap origin_envelope; origin_envelope.Add("schema_id", "skylark/task-proposal"); origin_envelope.Add("schema_version", "1");
		origin_envelope.Add("workspace_id", t15_workspace); origin_envelope.Add("stable_id", origin_id);
		origin_envelope.Add("record_kind", "task-proposal"); origin_envelope.Add("revision", "1");
		origin_envelope.Add("visibility", "private"); origin_envelope.Add("provenance_ref", t15_source);
		origin_record.Add("envelope", origin_envelope);
		DataBatchChange origin_create; origin_create.action = DATA_BATCH_CREATE;
		origin_create.qualified_id = t15_workspace + ":" + origin_id; origin_create.proposed = origin_record;
		Vector<DataBatchChange> origin_changes; origin_changes.Add(origin_create);
		DataBatchResult origin_written = t15_store.PublishBatch(t15_auth, "t15-origin-create", origin_changes);
		if(origin_written.status != "committed" || t15_store.RecoverPendingBatches(t15_auth).status != "recovered") {
			Cerr() << "FAILED: T15 synthetic source-link target creation/recovery status=" << origin_written.status << "\n"; return false;
		}
		DataStore migration_store;
		if(!migration_store.LoadWriteFixtureForTests(t15_root, t15_source)) return false;
		DataBatchResult imported = HistoricalAssessmentMigration::Import(t15_snapshot, t15_workspace, t15_source,
			 t15_operation, t15_auth, migration_store);
		if(imported.status != "committed" || imported.operation_id != t15_operation || imported.changed_ids.GetCount() != 2) {
			Cerr() << "FAILED: T15 PublishBatch create status=" << imported.status << "\n"; return false;
		}
		String commit_recovery = migration_store.RecoverPendingBatches(t15_auth).status;
		if(commit_recovery != "recovered") {
			Cerr() << "FAILED: T15 committed batch recovery status=" << commit_recovery << "\n"; return false;
		}
		DataStore recovered;
		bool reloaded = recovered.LoadWriteFixtureForTests(t15_root, t15_source);
		String recovery_status = reloaded ? recovered.RecoverPendingBatches(t15_auth).status : "not_run";
		if(!reloaded || recovery_status != "clean" || recovered.GetDocuments().GetCount() != 3) {
			Cerr() << "FAILED: T15 post-commit reload/recovery or canonical count (loaded=" << reloaded
			       << ", recovery=" << recovery_status << ", records=" << recovered.GetDocuments().GetCount()
			       << ", source_errors=" << recovered.GetSourceErrorCount() << ")\n";
			for(const String& diagnostic : recovered.GetSourceDiagnostics()) Cerr() << "T15 reload diagnostic=" << diagnostic << "\n";
			return false;
		}
		DataBatchResult repeated = HistoricalAssessmentMigration::Import(t15_snapshot, t15_workspace, t15_source,
			 t15_operation, t15_auth, recovered);
		if(repeated.status != "exact_repeat" || !repeated.changed_ids.IsEmpty() || recovered.GetDocuments().GetCount() != 3) {
			Cerr() << "FAILED: T15 exact repeat changed canonical count (status=" << repeated.status
			       << ", changed=" << repeated.changed_ids.GetCount() << ", records=" << recovered.GetDocuments().GetCount() << ")\n";
			return false;
		}
		ValueMap changed_snapshot = t15_snapshot;
		ValueArray changed_assertions; changed_assertions.Add(task_assertion);
		ValueMap changed_profile = profile_assertion; ValueMap changed_scores; changed_scores.Add("synthetic-category", "changed");
		changed_profile.Set("category_scores", changed_scores); changed_assertions.Add(changed_profile);
		changed_snapshot.Set("assertions", changed_assertions);
		String changed_operation = HistoricalAssessmentMigration::OperationId(changed_snapshot, t15_workspace, t15_source);
		DataBatchResult changed = HistoricalAssessmentMigration::Import(changed_snapshot, t15_workspace, t15_source,
			 changed_operation, t15_auth, recovered);
		if(changed.status != "conflict" || !changed.operation_id.IsEmpty() || !changed.changed_ids.IsEmpty() ||
		   recovered.GetDocuments().GetCount() != 3) {
			Cerr() << "FAILED: T15 changed payload was not blocked as destination collision\n"; return false;
		}
		ValueMap next_revision_snapshot = t15_snapshot; next_revision_snapshot.Set("snapshot_revision", "synthetic-revision-2");
		String next_operation = HistoricalAssessmentMigration::OperationId(next_revision_snapshot, t15_workspace, t15_source);
		ValueArray old_mapped_records = MapGet(HistoricalAssessmentMigration::Preview(t15_snapshot, t15_workspace, t15_source, t15_auth, recovered), "records");
		String old_assessment_id = MapGetString((ValueMap)old_mapped_records[0], "id");
		DataBatchResult next_import = HistoricalAssessmentMigration::Import(next_revision_snapshot, t15_workspace, t15_source,
			 next_operation, t15_auth, recovered);
		if(next_import.status != "committed" || recovered.RecoverPendingBatches(t15_auth).status != "recovered") {
			Cerr() << "FAILED: T15 new source snapshot revision import/recovery status=" << next_import.status << "\n"; return false;
		}
		DataStore final_store;
		if(!final_store.LoadWriteFixtureForTests(t15_root, t15_source) || final_store.RecoverPendingBatches(t15_auth).status != "clean" ||
		   final_store.GetDocuments().GetCount() != 5) {
			Cerr() << "FAILED: T15 new snapshot revision did not create distinct records\n"; return false;
		}
		const DataDocument* old_record = final_store.FindDocument(t15_workspace + ":" + old_assessment_id);
		if(!old_record || old_record->revision != "1" || MapGetString(old_record->data, "snapshot_revision") != "synthetic-revision") {
			Cerr() << "FAILED: T15 new snapshot revision mutated prior canonical record\n"; return false;
		}
		const DataDocument* linked_record = final_store.FindDocument(t15_workspace + ":" + old_assessment_id);
		ValueMap linked_assertion = MapGet(linked_record->data, "source_assertion");
		if(MapGetString(linked_assertion, "source_link_state") != "resolved" ||
		   !MapGet(linked_assertion, "resolved_links").Is<ValueArray>()) {
			Cerr() << "FAILED: T15 same-workspace source link did not resolve\n"; return false;
		}
		Cout() << "ok    T15 PublishBatch validates closed assertion schema; link resolution, create/recovery, repeat, revision and denial checks passed\n";
	}
	// T60 synthetic schema validation fixtures exercise trusted cards through the
	// same DataStore load/validation path used before canonical writes.
	{
		// Earlier fixtures are complete; retain only the lineage store consumed
		// again below, and reclaim their bytes before the largest records are saved.
		Vector<String> stale_fixtures;
		FindFile shards(AppendFileName(workspace.fixtures, "*"));
		while(shards) {
			if(shards.GetName() != "." && shards.GetName() != ".." &&
			   shards.IsDirectory() && !shards.IsSymLink()) {
				String shard_path = shards.GetPath();
				FindFile entries(AppendFileName(shard_path, "*"));
				while(entries) {
					String entry_name = entries.GetName();
					bool retained = entry_name == "t64-lineage-store" ||
					                entry_name == "t17-populated" ||
					                entry_name == "t17-partial-catalog";
					if(entry_name != "." && entry_name != ".." && !retained &&
					   entries.IsDirectory() && !entries.IsSymLink())
						stale_fixtures.Add(entries.GetPath());
					entries.Next();
				}
			}
			shards.Next();
		}
		for(const String& stale : stale_fixtures)
			RemoveOwnedTree(stale);
		String schema_root = workspace.FixtureRoot("t60-schema");
		if(!CreatePrivateDirectory(schema_root)) {
			Cerr() << "FAILED: T60 synthetic schema fixture directory setup\n";
			return false;
		}
		auto envelope = [](const String& kind, const String& id) {
			ValueMap e; e.Add("schema_id", "skylark/" + kind); e.Add("schema_version", kind.StartsWith("evidence-") ? "2" : "1");
			e.Add("workspace_id", "synthetic-workspace"); e.Add("stable_id", id); e.Add("record_kind", kind);
			e.Add("revision", "1"); e.Add("visibility", "private"); e.Add("provenance_ref", "synthetic"); return e;
		};
		auto snapshot = [&](const String& id) {
			ValueMap r; r.Add("id", id); r.Add("kind", "evidence-snapshot"); r.Add("workspace_id", "synthetic-workspace");
			r.Add("source_kind", "paper"); r.Add("source_identity", "synthetic-source"); r.Add("upstream_revision", "rev-1");
			r.Add("content_sha256", String('a', 64)); r.Add("captured_at", "2026-01-02T03:04:05Z"); r.Add("source_at", "2026-01-02T03:04:05Z");
			r.Add("collector", "synthetic"); ValueMap locator; locator.Add("type", "paper-section"); locator.Add("id", "paper-1"); locator.Add("page", 1); locator.Add("section", "intro"); r.Add("locator", locator);
			r.Add("resolution", "resolved"); r.Add("content", "x"); r.Add("classification", "private"); r.Add("visibility", "restricted");
			r.Add("retracted", false); r.Add("predecessor", "evi-s-previous"); ValueArray contributors; contributors.Add("synthetic-source"); r.Add("contributors", contributors);
			r.Add("envelope", envelope("evidence-snapshot", id)); return r;
		};
			auto validates = [&](ValueMap record, bool expected) {
			String record_path = AppendFileName(schema_root, "record.json");
			String record_data = AsJSON(record, true);
			if(!SaveFile(record_path, record_data)) {
				FindFile existing(record_path);
				bool leaf_exists = existing;
				bool leaf_symlink = leaf_exists && existing.IsSymLink();
				String probe_path = AppendFileName(schema_root, "write-probe.tmp");
				bool probe_write = SaveFile(probe_path, "probe");
				if(probe_write) FileDelete(probe_path);
				FileOut diagnostic_out(record_path);
				Cerr() << "FAILED: T60 synthetic schema fixture write (errno=" << errno
				       << ", path=" << NormalizePath(record_path)
				       << ", shard=" << GetFileName(GetFileFolder(schema_root))
				       << ", directory=" << DirectoryExists(schema_root)
				       << ", parent=" << DirectoryExists(GetFileFolder(record_path))
				       << ", leaf_exists=" << leaf_exists
				       << ", leaf_symlink=" << leaf_symlink
				       << ", leaf_directory=" << DirectoryExists(record_path)
				       << ", probe_write=" << probe_write
				       << ", fileout_open=" << diagnostic_out.IsOpen()
				       << ", data_bytes=" << record_data.GetCount()
				       << ", last_error=" << GetLastError() << ")\n";
				return false;
			}
			DataStore store;
			if(!store.LoadFixtureForTests(schema_root) || store.GetDocuments().GetCount() != 1) {
				Cerr() << "FAILED: T60 synthetic schema fixture load\n";
				return false;
			}
			if(store.GetDocuments()[0].valid != expected) {
				Cerr() << "FAILED: T60 fixture " << MapGetString(record, "id") << " validation=" << store.GetDocuments()[0].valid
				       << " expected=" << expected << " reason=" << store.GetDocuments()[0].invalid_reason;
				for(const String& d : store.GetDocuments()[0].diagnostics) Cerr() << " diagnostic=" << d;
				Cerr() << "\n";
				return false;
			}
			if(MapGetString(record, "id") == "t60-extra") {
				bool generic = false;
				for(const String& diagnostic : store.GetDocuments()[0].diagnostics) {
					if(diagnostic == "additional_property:locator") generic = true;
					if(diagnostic.Find("hostile-untrusted-property-name") >= 0) return false;
				}
				if(!generic) return false;
			}
			return true;
		};
		ValueMap boundary = snapshot("t60-boundary"); boundary.Set("content", String('x', 256 * 1024));
		if(!validates(boundary, true)) return false;
		boundary.Set("content", String('x', 256 * 1024 + 1));
		if(!validates(boundary, false)) return false;
		ValueMap array_boundary = snapshot("t60-array"); ValueArray many;
		for(int i = 0; i < 256; i++) many.Add("synthetic"); array_boundary.Set("contributors", many);
		if(!validates(array_boundary, true)) return false;
		many.Add("overflow"); array_boundary.Set("contributors", many);
		if(!validates(array_boundary, false)) return false;
		ValueMap item_bytes = snapshot("t60-item-bytes"); ValueArray long_contributor; long_contributor.Add(String('x', 257)); item_bytes.Set("contributors", long_contributor);
		if(!validates(item_bytes, false)) return false;
		ValueMap integer_items = snapshot("t60-integer-items"); ValueArray integer_values; integer_values.Add(1); integer_values.Add((int64)2); integer_items.Set("source_lines", integer_values);
		if(!validates(integer_items, true)) return false;
		integer_values.Add("3"); integer_items.Set("source_lines", integer_values);
		if(!validates(integer_items, false)) return false;
		integer_items.Set("source_lines", 3);
		if(!validates(integer_items, false)) return false;
		ValueMap wrong_type = snapshot("t60-wrong-type"); wrong_type.Set("content", 17);
		if(!validates(wrong_type, false)) return false;
		ValueMap missing = snapshot("t60-missing"); ValueMap missing_locator = MapGet(missing, "locator"); missing_locator.Remove(missing_locator.Find("type")); missing.Set("locator", missing_locator);
		if(!validates(missing, false)) return false;
		missing = snapshot("t60-missing-id"); missing_locator = MapGet(missing, "locator"); missing_locator.Remove(missing_locator.Find("id")); missing.Set("locator", missing_locator);
		if(!validates(missing, false)) return false;
		ValueMap bad_tag = snapshot("t60-bad-tag"); ValueMap bad_tag_locator = MapGet(bad_tag, "locator"); bad_tag_locator.Set("type", "untrusted-tag"); bad_tag.Set("locator", bad_tag_locator);
		if(!validates(bad_tag, false)) return false;
		ValueMap long_id = snapshot("t60-long-locator-id"); ValueMap long_id_locator = MapGet(long_id, "locator"); long_id_locator.Set("id", String('x', 129)); long_id.Set("locator", long_id_locator);
		if(!validates(long_id, false)) return false;
		ValueMap long_path = snapshot("t60-long-locator-path"); ValueMap long_path_locator = MapGet(long_path, "locator"); long_path_locator.Set("path", String('x', 241)); long_path.Set("locator", long_path_locator);
		if(!validates(long_path, false)) return false;
		ValueMap extreme = snapshot("t60-integer-extreme"); ValueMap extreme_locator = MapGet(extreme, "locator"); extreme_locator.Set("page", (int64)9223372036854775807LL); extreme.Set("locator", extreme_locator);
		if(!validates(extreme, false)) return false;
		extreme = snapshot("t60-integer-minimum"); extreme_locator = MapGet(extreme, "locator"); extreme_locator.Set("page", (int64)(-9223372036854775807LL - 1)); extreme.Set("locator", extreme_locator);
		if(!validates(extreme, false)) return false;
		ValueMap extra = snapshot("t60-extra"); ValueMap extra_locator = MapGet(extra, "locator"); extra_locator.Set("hostile-untrusted-property-name", "no"); extra.Set("locator", extra_locator);
		if(!validates(extra, false)) return false;
		ValueMap malformed = snapshot("t60-malformed"); malformed.Set("locator", "not-an-object");
		if(!validates(malformed, false)) return false;
		malformed = snapshot("t60-malformed-field"); ValueMap bad_locator = MapGet(malformed, "locator"); bad_locator.Set("page", "one"); malformed.Set("locator", bad_locator);
		if(!validates(malformed, false)) return false;
		ValueMap legacy; legacy.Add("id", "t60-legacy"); legacy.Add("kind", "paper"); legacy.Add("title", String('x', 300000)); legacy.Add("source", String('y', 300000)); legacy.Add("status", "draft"); legacy.Add("legacy_extra", true); legacy.Add("envelope", envelope("paper", "t60-legacy"));
		if(MapGetString(legacy, "title").GetCount() != 300000 || MapGetString(legacy, "source").GetCount() != 300000) return false;
		ValueMap legacy_title = legacy; legacy_title.Set("id", "t60-legacy-title"); legacy_title.Set("title", String('x', 300000)); legacy_title.Set("source", "synthetic");
		ValueMap legacy_title_envelope = MapGet(legacy_title, "envelope"); legacy_title_envelope.Set("stable_id", "t60-legacy-title"); legacy_title.Set("envelope", legacy_title_envelope);
		ValueMap legacy_source = legacy; legacy_source.Set("id", "t60-legacy-source"); legacy_source.Set("title", "synthetic"); legacy_source.Set("source", String('y', 300000));
		ValueMap legacy_source_envelope = MapGet(legacy_source, "envelope"); legacy_source_envelope.Set("stable_id", "t60-legacy-source"); legacy_source.Set("envelope", legacy_source_envelope);
		if(!validates(legacy_title, true) || !validates(legacy_source, true)) return false;
		Cout() << "ok    T60 bounded strings/arrays, exact types, closed locator shape and legacy schema defaults\n";
	}
	String valid_fixture_root = workspace.root;
	String wrong_fixture_root = workspace.FixtureRoot("wrong-fixture");
	if(!CreatePrivateDirectory(wrong_fixture_root) ||
	   !CreatePrivateDirectory(AppendFileName(wrong_fixture_root, "v2")) ||
	   !CreatePrivateDirectory(AppendFileName(wrong_fixture_root, "data"))) {
		Cerr() << "FAILED: could not create isolated root-mismatch fixture\n";
		return false;
	}
	ManagerSkylarkApp app(valid_fixture_root);
	cwd.Restore();
	app.ip = "127.0.0.1";
	String expected_local_root = NormalizePath(AppendFileName(workspace.root, "mgr"));
	port = FindAvailableTestPort(port);
	if(!port) {
		Cerr() << "FAILED: no available loopback test port\n";
		return false;
	}
	app.port = port;
	app.threads = 1;
	if(app.local_store.RootDir() != expected_local_root) {
		Cerr() << "FAILED: ManagerStore did not capture the isolated fixture root before InitApp\n";
		return false;
	}
	app.test_fixture_root = wrong_fixture_root;
	bool rejected_wrong_root = !app.InitApp();
	bool stores_unloaded = !app.PinLatestCatalog() && app.local_store.Projects().IsEmpty();
	app.test_fixture_root = valid_fixture_root;
	if(!rejected_wrong_root || !stores_unloaded || !app.InitApp()) {
		Cerr() << "FAILED: test initialization did not reject a mismatched root before loading stores\n";
		return false;
	}
	// T64 projections use the initialized app's trusted visibility registry and pinned fixture stores.
	{
		DataStore store; String root=workspace.FixtureRoot("t64-lineage-store");
		if(!store.LoadFixtureForTests(root)) { Cerr()<<"FAILED T64 pinned store reload\n"; return false; }
		ValueMap view=CandidateLineage::Project(store,app,"t64-workspace"); ValueArray rows=MapGet(view,"records");
		String serialized_view=AsJSON(view,true); int redacted_placeholders=0; bool placeholder_shape=false;
		for(const Value& v:rows) if(IsValueMap(v)) { ValueMap record=v; if(MapGetString(record,"kind")=="redacted-lineage-link") { redacted_placeholders++; placeholder_shape=record.GetCount()==2&&MapGetString(record,"resolution")=="denied"&&record.Find("target_id")<0&&record.Find("id")<0; } }
		if(MapGetString(view,"status")!="visible" || rows.GetCount()!=6 || redacted_placeholders!=1 || !placeholder_shape || serialized_view!=AsJSON(CandidateLineage::Project(store,app,"t64-workspace"),true) || serialized_view.Find("private-parent-marker")>=0 || serialized_view.Find("second-private-parent-marker")>=0 || serialized_view.Find("t64-private-child")>=0 || serialized_view.Find("t64-secret-content-marker")>=0 || serialized_view.Find("source-parent-link")>=0 || MapGetString(CandidateLineage::Project(store,app,"unknown-workspace"),"status")!="denied") { Cerr()<<"FAILED T64 policy-aware redacted placeholder/confidentiality projection status="<<MapGetString(view,"status")<<" rows="<<rows.GetCount()<<" placeholders="<<redacted_placeholders<<" shape="<<placeholder_shape<<" docs="<<store.GetDocuments().GetCount()<<"\n"<<serialized_view<<"\n"; return false; }
		bool observed=false,hypothesis=false,local=false,proposal=false,decision=false;
		for(const Value& v:rows) { ValueMap r=v; String kind=MapGetString(r,"kind"); if(kind=="observed-submission") observed=MapGetString(r,"id")!=MapGetString(r,"external_identity")&&MapGetString(r,"owner_kind")=="external-unknown"; if(kind=="local-interpretation") hypothesis=MapGetString(r,"submission_state")=="not-a-real-submission"&&MapGetString(r,"approval_state")=="not-approved"&&MapGetString(r,"author_state")=="unknown"&&MapGetString(r,"intent_state")=="unknown"&&MapGet(r,"hypothetical")==true; if(kind=="local-candidate") local=MapGetString(r,"origin_kind")=="locally-authored"; if(kind=="proposal-revision") proposal=MapGetString(r,"revision")=="draft"&&MapGetString(r,"submission_state")=="not-submitted"; if(kind=="selection-decision") decision=r.Find("implementation_authorized")<0&&r.Find("accepted_state")<0; }
		if(!observed||!hypothesis||!local||!proposal||!decision) { Cerr()<<"FAILED T64 kind identity/origin guarantees\n"; return false; }
		DataStore invalid_store=store; String invalid_record_id; const String invalid_secret_id="t64-invalid-private-id", invalid_secret_content="t64-invalid-private-content";
		for(DataDocument& d:invalid_store.docs) if(d.workspace_id=="t64-workspace" && d.kind=="local-candidate") { d.valid=false; invalid_record_id=d.id; d.data.Set("owner_kind",invalid_secret_id); d.data.Set("uncertainty",invalid_secret_content); }
		ValueMap invalid_view=CandidateLineage::Project(invalid_store,app,"t64-workspace"); String invalid_json=AsJSON(invalid_view,true);
		if(invalid_record_id.IsEmpty() || MapGetString(invalid_view,"status")!="invalid" || !MapGet(invalid_view,"records").Is<ValueArray>() || MapGet(invalid_view,"records").GetCount()!=0 || invalid_json.Find(invalid_record_id)>=0 || invalid_json.Find(invalid_secret_id)>=0 || invalid_json.Find(invalid_secret_content)>=0 || invalid_json.Find("locally-authored")>=0 || invalid_json.Find("local-author")>=0) { Cerr()<<"FAILED T64 invalid-record projection was not fail-closed/redacted\n"<<invalid_json<<"\n"; return false; }
		auto make_assertion=[](const String& stable,const String& revision,const String& parent) { ValueMap a; a.Add("stable_id",stable); a.Add("kind","proposal-revision"); a.Add("proposal_id","chain"); a.Add("revision_id",revision); a.Add("candidate_id","candidate"); if(!parent.IsEmpty()) a.Add("parent_revision_id",parent); a.Add("provenance","synthetic"); a.Add("uncertainty","unknown"); return a; };
		auto check_lineage=[&](const String& name,const String& ws,const ValueArray& assertions,const String& expected) {
			ValueMap spec; spec.Add("workspace_id",ws); spec.Add("assertions",assertions); ValueArray built=CandidateLineage::BuildRecords(spec); String sub=workspace.FixtureRoot(name); if(built.IsEmpty()||!CreatePrivateDirectory(sub)) return false;
			for(const Value& v:built) { ValueMap r=v; String file=AppendFileName(sub,"records/"+MapGetString(r,"id")+".json"); if(!RealizeDirectory(GetFileFolder(file))||!SaveFile(file,AsJSON(r,true))) return false; }
			DataStore pinned; return pinned.LoadFixtureForTests(sub)&&MapGetString(CandidateLineage::Project(pinned,app,ws),"status")==expected;
		};
		ValueArray duplicate; duplicate.Add(make_assertion("dup-a","same","")); duplicate.Add(make_assertion("dup-b","same",""));
		ValueArray cycle; cycle.Add(make_assertion("cycle-a","a","b")); cycle.Add(make_assertion("cycle-b","b","a"));
		ValueArray missing; missing.Add(make_assertion("missing-parent","a","absent"));
		if(!check_lineage("t64-duplicates","t64-duplicates",duplicate,"ambiguous") || !check_lineage("t64-cycle","t64-cycle",cycle,"cyclic") || !check_lineage("t64-missing","t64-missing",missing,"visible")) { Cerr()<<"FAILED T64 duplicate/cyclic/missing lineage resolution\n"; return false; }
		// A hidden source on a cyclic revision must not make raw records available on early return.
		ValueArray hidden_cycle; ValueMap hc1=make_assertion("hidden-cycle-a","a","b"); hc1.Add("source_identity","t64-cycle-secret-source"); hc1.Set("uncertainty","t64-cycle-secret-content"); hidden_cycle.Add(hc1); hidden_cycle.Add(make_assertion("hidden-cycle-b","b","a"));
		ValueMap hidden_cycle_spec; hidden_cycle_spec.Add("workspace_id","t64-hidden-cycle"); hidden_cycle_spec.Add("assertions",hidden_cycle); ValueArray hidden_cycle_records=CandidateLineage::BuildRecords(hidden_cycle_spec); String hidden_cycle_root=workspace.FixtureRoot("t64-hidden-cycle");
		if(hidden_cycle_records.GetCount()!=2 || !CreatePrivateDirectory(hidden_cycle_root)) return false;
		for(const Value& item:hidden_cycle_records) { ValueMap r=item; String file=AppendFileName(hidden_cycle_root,"records/"+MapGetString(r,"id")+".json"); if(!RealizeDirectory(GetFileFolder(file))||!SaveFile(file,AsJSON(r,true)))return false; }
		DataStore hidden_cycle_store; if(!hidden_cycle_store.LoadFixtureForTests(hidden_cycle_root)) return false;
		ValueMap hidden_cycle_view=CandidateLineage::Project(hidden_cycle_store,app,"t64-hidden-cycle"); String hidden_cycle_json=AsJSON(hidden_cycle_view,true);
		if(MapGetString(hidden_cycle_view,"status")!="cyclic" || hidden_cycle_json.Find("hidden-cycle-a")>=0 || hidden_cycle_json.Find("hidden-cycle-b")>=0 || hidden_cycle_json.Find("t64-cycle-secret-source")>=0 || hidden_cycle_json.Find("t64-cycle-secret-content")>=0 || hidden_cycle_json.Find(MapGetString((ValueMap)hidden_cycle_records[0],"id"))>=0 || hidden_cycle_json.Find(MapGetString((ValueMap)hidden_cycle_records[1],"id"))>=0 || MapGet(hidden_cycle_view,"records").GetCount()!=1 || ((ValueMap)MapGet(hidden_cycle_view,"records")[0]).GetCount()!=2 || MapGetString((ValueMap)MapGet(hidden_cycle_view,"records")[0],"kind")!="redacted-lineage-link") { Cerr()<<"FAILED T64 hidden-source cycle leaked raw lineage data\n"; return false; }

		// Check zero/one/multiple exact source-parent matches under trusted visibility.
		String rel_root=workspace.FixtureRoot("t64-source-relationship-cases"); if(!CreatePrivateDirectory(rel_root)) return false;
		ValueMap relation_spec; relation_spec.Add("workspace_id","t64-relations"); ValueArray relation_assertions;
		auto source_child=[&](const String& stable,const String& target) { ValueMap a=make_assertion(stable,"child-"+stable,""); a.Set("kind","local-interpretation"); for(const char *key:{"proposal_id","revision_id","candidate_id"}) { int ix=a.Find(key); if(ix>=0)a.Remove(ix); } ValueMap link; link.Add("id","edge-"+stable); link.Add("type","evidence"); link.Add("workspace_id","t64-relations"); link.Add("target_id",target); link.Add("target_kind","evidence-snapshot"); link.Add("resolution","unresolved"); ValueArray links; links.Add(link); a.Add("relationships",links); return a; };
		relation_assertions.Add(source_child("missing-child","no-matching-parent"));
		relation_assertions.Add(source_child("visible-ambiguous-child","visible-ambiguous-parent"));
		relation_assertions.Add(source_child("hidden-ambiguous-child","hidden-ambiguous-parent"));
		relation_assertions.Add(source_child("unique-hidden-child","unique-hidden-parent"));
		relation_spec.Add("assertions",relation_assertions); ValueArray relation_records=CandidateLineage::BuildRecords(relation_spec); if(relation_records.GetCount()!=4) { Cerr()<<"FAILED T64 source-link fixture construction\n"; return false; }
		for(const Value& item:relation_records) { ValueMap r=item; String file=AppendFileName(rel_root,"lineage/"+MapGetString(r,"id")+".json"); if(!RealizeDirectory(GetFileFolder(file))||!SaveFile(file,AsJSON(r,true)))return false; }
		auto evidence_snapshot=[&](const String& id,const String& source,const String& contents) {
			ValueMap r; r.Add("id",id); r.Add("kind","evidence-snapshot"); r.Add("workspace_id","t64-relations"); r.Add("schema_id","skylark/evidence-snapshot"); r.Add("schema_version","2"); r.Add("source_kind","paper"); r.Add("source_identity",source); r.Add("upstream_revision","r1"); r.Add("content_sha256",String('0',64)); r.Add("captured_at","synthetic"); r.Add("source_at","synthetic"); r.Add("collector","synthetic-test"); ValueMap locator; locator.Add("type","paper-section"); locator.Add("id","section"); r.Add("locator",locator); r.Add("resolution","resolved"); r.Add("content",contents); r.Add("classification","public"); r.Add("visibility","visible"); r.Add("retracted",false); r.Add("predecessor","none"); r.Add("contributors",ValueArray()); r.Add("source_lines",ValueArray()); ValueMap env; env.Add("schema_id","skylark/evidence-snapshot"); env.Add("schema_version","2"); env.Add("workspace_id","t64-relations"); env.Add("stable_id",id); env.Add("record_kind","evidence-snapshot"); env.Add("revision","1"); env.Add("visibility","private"); env.Add("provenance_ref","synthetic"); r.Add("envelope",env); return r;
		};
		auto save_evidence=[&](const String& folder,const String& id,const String& source,const String& content) { ValueMap r=evidence_snapshot(id,source,content); String file=AppendFileName(rel_root,folder+"/"+id+".json"); return RealizeDirectory(GetFileFolder(file))&&SaveFile(file,AsJSON(r,true)); };
		if(!save_evidence("evidence-a","visible-ambiguous-parent","t64-visible-source","visible synthetic source") || !save_evidence("evidence-b","visible-ambiguous-parent","t64-visible-source","visible duplicate source") ||
		   !save_evidence("evidence-c","hidden-ambiguous-parent","t64-visible-source","visible half of ambiguous parent") || !save_evidence("evidence-d","hidden-ambiguous-parent","t64-hidden-source","t64-hidden-parent-secret") ||
		   !save_evidence("evidence-e","unique-hidden-parent","t64-hidden-source","t64-unique-parent-secret")) return false;
		ValueMap cross_workspace_parent=evidence_snapshot("no-matching-parent","t64-other-workspace-source","t64-other-workspace-secret-content");
		cross_workspace_parent.Set("workspace_id","t64-other-relations"); ValueMap cross_envelope=MapGet(cross_workspace_parent,"envelope"); cross_envelope.Set("workspace_id","t64-other-relations"); cross_workspace_parent.Set("envelope",cross_envelope);
		String cross_workspace_path=AppendFileName(rel_root,"evidence-other-workspace/no-matching-parent.json");
		if(!RealizeDirectory(GetFileFolder(cross_workspace_path))||!SaveFile(cross_workspace_path,AsJSON(cross_workspace_parent,true))) return false;
		DataStore relation_store; if(!relation_store.LoadFixtureForTests(rel_root)||relation_store.GetLoadStatus()!="loaded") { Cerr()<<"FAILED T64 relationship store load: "<<relation_store.GetLoadStatus()<<"\n"; for(const DataDocument& d:relation_store.GetDocuments()) if(!d.valid) Cerr()<<d.kind<<" invalid "<<d.invalid_reason<<"\n"; return false; }
		// Duplicate qualified IDs are quarantined by the loader; revive only these synthetic
		// in-memory parent fixtures to exercise the projector's ambiguous-match policy branch.
		for(DataDocument& parent:relation_store.docs) if(parent.kind=="evidence-snapshot") { parent.valid=true; parent.validated=true; parent.validation_state="valid"; parent.invalid_reason.Clear(); }
		TrustedPolicyRegistry relation_policy; relation_policy.configured=true; PolicyWorkspaceFact relation_workspace; relation_workspace.identity.workspace_id="t64-relations"; relation_workspace.identity.object_id="t64-relations"; relation_policy.workspaces.Add(relation_workspace);
		for(const String& source:{"t64-visible-source","t64-hidden-source"}) { PolicyIdentity identity; identity.workspace_id="t64-relations"; identity.object_id=source; PolicySourceFact fact; fact.workspace=relation_workspace.identity; fact.identity=identity; fact.classification=POLICY_PUBLIC; fact.visible=source=="t64-visible-source"; relation_policy.sources.Add(fact); PolicyProviderFact provider; provider.workspace=relation_workspace.identity; provider.identity=identity; provider.training_policy="eligible"; provider.enabled=true; provider.config_revision="synthetic-r1"; provider.secret_reference="secretref:synthetic-t64"; relation_policy.providers.Add(provider); }
		PolicySettings configured_instance; configured_instance.configured=true; PolicySettings no_policy_overrides;
		if(!app.InstallSyntheticPolicyContextForTests(configured_instance,no_policy_overrides,no_policy_overrides,relation_policy)) return false;
		bool visible_source_policy=app.IsAssessmentSourceVisible("t64-relations","t64-visible-source"), hidden_source_policy=app.IsAssessmentSourceVisible("t64-relations","t64-hidden-source");
		ValueMap relation_view=CandidateLineage::Project(relation_store,app,"t64-relations"); app.ResetSyntheticPolicyContextForTests(); ValueArray relation_rows=MapGet(relation_view,"records"); String relation_json=AsJSON(relation_view,true);
		bool missing_unresolved=false, visible_ambiguous=false; int relation_placeholders=0;
		for(const Value& item:relation_rows) if(IsValueMap(item)) { ValueMap row=item; if(MapGetString(row,"kind")=="redacted-lineage-link") relation_placeholders++; if(MapGetString(row,"kind")!="local-interpretation") continue; ValueArray links=MapGet(row,"relationships"); if(links.IsEmpty()) continue; ValueMap link=links[0]; if(MapGetString(link,"target_id")=="no-matching-parent") missing_unresolved=MapGetString(link,"target_id")=="no-matching-parent"&&MapGetString(link,"target_kind")=="evidence-snapshot"&&MapGetString(link,"resolution")=="unresolved"; if(MapGetString(link,"target_id")=="visible-ambiguous-parent") visible_ambiguous=MapGetString(link,"resolution")=="ambiguous"&&MapGetString(link,"target_kind")=="evidence-snapshot"; }
		bool cross_workspace_parent_loaded=false; for(const DataDocument& d:relation_store.GetDocuments()) if(d.id=="no-matching-parent"&&d.workspace_id=="t64-other-relations"&&d.valid) cross_workspace_parent_loaded=true;
		if(MapGetString(relation_view,"status")!="visible"||!cross_workspace_parent_loaded||!missing_unresolved||!visible_ambiguous||relation_placeholders!=1||relation_json.Find("hidden-ambiguous-child")>=0||relation_json.Find("unique-hidden-child")>=0||relation_json.Find("hidden-ambiguous-parent")>=0||relation_json.Find("unique-hidden-parent")>=0||relation_json.Find("t64-hidden-source")>=0||relation_json.Find("t64-hidden-parent-secret")>=0||relation_json.Find("t64-unique-parent-secret")>=0||relation_json.Find("t64-other-relations")>=0||relation_json.Find("t64-other-workspace-source")>=0||relation_json.Find("t64-other-workspace-secret-content")>=0||relation_json.Find(MapGetString((ValueMap)relation_records[2],"id"))>=0||relation_json.Find(MapGetString((ValueMap)relation_records[3],"id"))>=0) { Cerr()<<"FAILED T64 source-parent missing/ambiguous workspace isolation matrix visible="<<visible_ambiguous<<" missing="<<missing_unresolved<<" cross-workspace-parent="<<cross_workspace_parent_loaded<<" placeholders="<<relation_placeholders<<" policy="<<visible_source_policy<<"/"<<hidden_source_policy<<"\n"<<relation_json<<"\n"; return false; }
		Cout()<<"ok    T64 trusted-policy projection, source-parent redaction, deterministic ordering and revision-chain ambiguity/cycle checks\n";
	}
	// T17 synthetic headless parity boundary: empty and unknown selections stay explicit,
	// carry no invented dimensions, and the HTML renderer exposes accessible controls.
	{
		CatalogReadHandle pinned=app.PinCatalog();
		if(!pinned) return false;
		ValueMap empty_view=AssessmentViewService::Build(app,*pinned,"synthetic-assessment-workspace","","","","","","");
		ValueMap unknown_view=AssessmentViewService::Build(app,*pinned,"synthetic-assessment-workspace","missing-qualified-run","","","","","");
		String rendered=AssessmentViewService::Render(empty_view,"overview");
		if(MapGetString(empty_view,"view_state")!="empty"||MapGet(empty_view,"filtered_count")!=0||
		   MapGetString(unknown_view,"view_state")!="unavailable"||rendered.Find("av-filter")<0||
		   rendered.Find("caption")<0||rendered.Find("Apply view")<0||
		   ManagerSkylarkApp::GetPageCustomStyles().Find("assessment-table-wrap:focus-visible")<0) {
			Cerr()<<"FAILED T17 synthetic empty/unknown/accessibility parity\n"; return false;
		}
		Cout()<<"ok    T17 synthetic empty/unknown states, shared headless HTML result and accessible filter/focus markers\n";
	}
	// Populated T17 fixture is built and loaded only under the isolated self-test root.
	{
		const String ws="synthetic-t17-workspace", source="synthetic-t17-source", denied_source="synthetic-t17-denied";
		String root=workspace.FixtureRoot("t17-populated");
		if(!CreatePrivateDirectory(root)||!CreatePrivateDirectory(AppendFileName(root,"data"))||!CreatePrivateDirectory(AppendFileName(root,"v2"))) return false;
		ValueMap projection; projection.Add("status","configured"); projection.Add("workspace_id",ws); projection.Add("rubric_id","generic-rubric"); projection.Add("rubric_revision","r7"); projection.Add("tool_config","synthetic-tool"); projection.Add("model_config","unconfigured");
		ValueArray pins,sources,evidence_keys,relationships,groups,findings,categories,candidates;
		ValueMap pin; pin.Add("workspace_id",ws); pin.Add("id","input-a"); pin.Add("revision","pin-r1"); pins.Add(pin);
		ValueMap src; src.Add("workspace_id",ws); src.Add("identity",source); src.Add("revision","source-r1"); sources.Add(src);
		ValueMap src_revision_two=src; src_revision_two.Set("revision","source-r2"); sources.Add(src_revision_two);
		for(int c=0;c<6;c++) {
			String cid=Format("synthetic-candidate-%d",c);
			const int stored_totals[]={20,20,16,12,20,20};
			ValueMap candidate; candidate.Add("candidate_id",cid); candidate.Add("provenance","computed"); candidate.Add("uncertainty","complete"); candidate.Add("tier",Vector<String>{"research-ready","paper-backed","interesting-risk","avoid-for-now"}[c%4]); candidate.Add("total_state","defined"); candidate.Add("total_numerator",AsString(stored_totals[c])); candidate.Add("total_denominator","6");
			ValueArray dimensions;
			for(int d=0;d<6;d++) { ValueMap dim; dim.Add("dimension_id",Format("generic-axis-%d",d)); dim.Add("score",AsString(4+c%3)); dim.Add("score_state","known"); dim.Add("coverage_state","covered"); dim.Add("evidence_count",1); dimensions.Add(dim); }
			candidate.Add("dimensions",dimensions); candidates.Add(candidate);
			ValueMap category; category.Add("candidate_id",cid); category.Add("provenance","authored"); category.Add("assessment",ValueMap());
			ValueMap ca; ca.Add("workspace_id",ws); ca.Add("candidate_id",cid); ca.Add("dimension_id","generic-axis-0"); ca.Add("score",AsString(4+c)); ca.Add("provenance","authored"); ValueArray refs; refs.Add("t17-evidence-item"); ca.Add("evidence_ids",refs);
			ca.Add("rationale",c==0?String("<em>T17 category rationale 0</em>"):Format("T17 category rationale %d",c));
			ca.Add("crowding_judgment",Format("T17 crowding judgment %d",c));
			ca.Add("opportunity_gap",Format("T17 opportunity gap %d",c));
			category.Set("assessment",ca); categories.Add(category);
		}
		ValueMap hidden_finding; hidden_finding.Add("workspace_id",ws); hidden_finding.Add("code","synthetic-denied-contributor"); hidden_finding.Add("candidate_id","synthetic-candidate-5"); hidden_finding.Add("provenance","computed"); ValueArray secret_source; secret_source.Add(denied_source); hidden_finding.Add("source_ids",secret_source); findings.Add(hidden_finding);
		projection.Add("input_revisions",pins); projection.Add("source_provenance",sources); projection.Add("evidence_keys",evidence_keys); projection.Add("relationships",relationships); projection.Add("overlap_groups",groups); projection.Add("findings",findings); projection.Add("category_assessments",categories); projection.Add("candidate_assessments",candidates);
		projection.Add("unique_source_count",1); projection.Add("unique_source_revision_count",2); projection.Add("unique_evidence_count",0); projection.Add("uncertainty","complete_supplied_evidence");
		ValueArray records=AssessmentService::BuildRecords(projection); if(records.GetCount()<10) return false;
		for(int i=0;i<records.GetCount();i++) if(!SaveFile(AppendFileName(AppendFileName(root,"data"),Format("record-%d.json",i)),AsJSON(records[i],true))) return false;
		ValueMap replacement_run=records[0]; replacement_run.Set("id","analysis-run-t17-replacement"); ValueMap replacement_env=MapGet(replacement_run,"envelope"); replacement_env.Set("stable_id","analysis-run-t17-replacement"); replacement_run.Set("envelope",replacement_env);
		if(!SaveFile(AppendFileName(AppendFileName(root,"data"),"replacement-run.json"),AsJSON(replacement_run,true))) return false;
		ValueMap rubric_record; rubric_record.Add("id","generic-rubric"); rubric_record.Add("kind","assessment-rubric"); rubric_record.Add("workspace_id",ws); rubric_record.Add("revision","r7"); rubric_record.Add("provenance","authored"); rubric_record.Add("score_scale","integer-0-10"); rubric_record.Add("total_rule","weighted-mean");
		ValueArray rubric_dimensions,coverage; for(int d=0;d<6;d++){ValueMap rd;rd.Add("id",Format("generic-axis-%d",d));rd.Add("weight",1);rd.Add("minimum_score",0);rd.Add("maximum_score",10);rd.Add("minimum_evidence_count",1);rubric_dimensions.Add(rd);coverage.Add(Format("generic-axis-%d",d));} rubric_record.Add("dimensions",rubric_dimensions); rubric_record.Add("coverage_required",coverage);
		ValueArray rubric_tiers; const char *tier_names[]={"research-ready","paper-backed","interesting-risk","avoid-for-now"}; for(int t=0;t<4;t++){ValueMap tier;tier.Add("id",tier_names[t]);tier.Add("minimum_numerator",t*6);tier.Add("minimum_denominator",1);tier.Add("maximum_numerator",(t+1)*6);tier.Add("maximum_denominator",1);rubric_tiers.Add(tier);} rubric_record.Add("tiers",rubric_tiers);
		ValueMap rubric_env; rubric_env.Add("schema_id","skylark/assessment-rubric");rubric_env.Add("schema_version","1");rubric_env.Add("workspace_id",ws);rubric_env.Add("stable_id","generic-rubric");rubric_env.Add("record_kind","assessment-rubric");rubric_env.Add("revision","r7");rubric_env.Add("visibility","private");rubric_env.Add("provenance_ref","authored");rubric_record.Add("envelope",rubric_env);
		if(!SaveFile(AppendFileName(AppendFileName(root,"data"),"rubric.json"),AsJSON(rubric_record,true))) return false;
		ValueMap locator; locator.Add("type","paper-section"); locator.Add("id","synthetic-paper"); locator.Add("page",3); locator.Add("section","methods");
		String hostile_excerpt="<img src=x onerror=alert(1)> synthetic evidence excerpt";
		ValueMap snapshot; snapshot.Add("id","t17-evidence-snapshot"); snapshot.Add("kind","evidence-snapshot"); snapshot.Add("workspace_id",ws); snapshot.Add("source_kind","paper"); snapshot.Add("source_identity",source); snapshot.Add("upstream_revision","source-r2"); snapshot.Add("previous_revision","source-r1"); snapshot.Add("content_sha256",DataStore::ContentDigest(hostile_excerpt)); snapshot.Add("captured_at","2026-01-01T00:00:00Z"); snapshot.Add("source_at","2025-12-31T00:00:00Z"); snapshot.Add("collector","synthetic"); snapshot.Add("locator",locator); snapshot.Add("resolution","resolved"); snapshot.Add("content",hostile_excerpt); snapshot.Add("classification","public"); snapshot.Add("visibility","visible"); snapshot.Add("retracted",false); snapshot.Add("predecessor","none"); ValueArray contributors; contributors.Add(source); snapshot.Add("contributors",contributors);
		ValueMap snapshot_env; snapshot_env.Add("schema_id","skylark/evidence-snapshot");snapshot_env.Add("schema_version","2");snapshot_env.Add("workspace_id",ws);snapshot_env.Add("stable_id","t17-evidence-snapshot");snapshot_env.Add("record_kind","evidence-snapshot");snapshot_env.Add("revision","1");snapshot_env.Add("visibility","private");snapshot_env.Add("provenance_ref","synthetic");snapshot.Add("envelope",snapshot_env);
		ValueMap evidence; evidence.Add("id","t17-evidence-item"); evidence.Add("kind","evidence-item"); evidence.Add("workspace_id",ws); evidence.Add("snapshot","t17-evidence-snapshot"); evidence.Add("source_identity",source); evidence.Add("upstream_revision","source-r2"); evidence.Add("content_sha256",DataStore::ContentDigest(hostile_excerpt)); evidence.Add("locator",locator); evidence.Add("resolution","resolved"); evidence.Add("excerpt",hostile_excerpt); evidence.Add("classification","public"); evidence.Add("visibility","visible"); evidence.Add("contributors",contributors);
		ValueMap evidence_env; evidence_env.Add("schema_id","skylark/evidence-item");evidence_env.Add("schema_version","2");evidence_env.Add("workspace_id",ws);evidence_env.Add("stable_id","t17-evidence-item");evidence_env.Add("record_kind","evidence-item");evidence_env.Add("revision","1");evidence_env.Add("visibility","private");evidence_env.Add("provenance_ref","synthetic");evidence.Add("envelope",evidence_env);
		if(!SaveFile(AppendFileName(AppendFileName(root,"data"),"snapshot.json"),AsJSON(snapshot,true))||!SaveFile(AppendFileName(AppendFileName(root,"data"),"evidence.json"),AsJSON(evidence,true))) return false;
		ValueMap stale; stale.Add("id","synthetic-t17-stale"); stale.Add("kind","assessment-staleness"); stale.Add("workspace_id",ws); stale.Add("revision","1"); stale.Add("provenance","computed"); stale.Add("prior_run_id",MapGetString((ValueMap)records[0],"id")); stale.Add("replacement_run_id","analysis-run-t17-replacement"); ValueMap flags; for(const char *flag:{"source_dependency_set_changed","source_revision_changed","input_revision_changed","rubric_revision_changed","tool_config_changed","model_config_changed"}) flags.Add(flag,true); stale.Add("reason_flags",flags);
		ValueMap stale_env; stale_env.Add("schema_id","skylark/assessment-staleness"); stale_env.Add("schema_version","1"); stale_env.Add("stable_id","synthetic-t17-stale"); stale_env.Add("workspace_id",ws); stale_env.Add("record_kind","assessment-staleness"); stale_env.Add("revision","1"); stale_env.Add("visibility","private"); stale_env.Add("provenance_ref","computed"); stale.Add("envelope",stale_env);
		if(!SaveFile(AppendFileName(AppendFileName(root,"data"),"stale.json"),AsJSON(stale,true))) return false;
		DataStore data; V2Store v2; if(!data.LoadFixtureForTests(AppendFileName(root,"data"))||!v2.LoadFixtureForTests(AppendFileName(root,"v2"))) { Cerr()<<"T17 populated fixture load failed\n"; return false; }
		int valid_t17_categories=0; bool persisted_t63_category=false;
		for(const DataDocument& doc:data.GetDocuments()) if(doc.kind=="category-assessment"&&doc.workspace_id==ws) {
			if(!doc.valid||!doc.validated||doc.validation_state!="valid") { Cerr()<<"T17 T63 category failed isolated DataStore validation: "<<doc.id<<"\n"; return false; }
			valid_t17_categories++; ValueMap assessment=MapGet(doc.data,"assessment");
			if(MapGetString(assessment,"candidate_id")=="synthetic-candidate-0")
				persisted_t63_category=MapGetString(assessment,"rationale")=="<em>T17 category rationale 0</em>"&&MapGetString(assessment,"crowding_judgment")=="T17 crowding judgment 0"&&MapGetString(assessment,"opportunity_gap")=="T17 opportunity gap 0";
		}
		if(valid_t17_categories!=6||!persisted_t63_category) { Cerr()<<"T17 persisted category qualitative fields were missing or invalid\n"; return false; }
		One<DataStore> data_owner; data_owner.Create(); *data_owner=data; One<V2Store> v2_owner; v2_owner.Create(); *v2_owner=v2;
		CatalogGeneration populated(171,pick(v2_owner),pick(data_owner),true,true);
		TrustedPolicyRegistry registry; registry.configured=true; PolicyWorkspaceFact workspace_fact; workspace_fact.identity.workspace_id=ws; workspace_fact.identity.object_id=ws; registry.workspaces.Add(workspace_fact);
		for(const String& identity:{source,denied_source}) { PolicyIdentity id; id.workspace_id=ws; id.object_id=identity; PolicySourceFact sf; sf.workspace.workspace_id=ws; sf.workspace.object_id=ws; sf.identity=id; sf.classification=POLICY_PUBLIC; sf.visible=identity!=denied_source; registry.sources.Add(sf); PolicyProviderFact pf; pf.workspace.workspace_id=ws; pf.workspace.object_id=ws; pf.identity=id; pf.training_policy="eligible"; pf.enabled=true; pf.config_revision="synthetic-r1"; pf.secret_reference="secretref:synthetic-rest"; registry.providers.Add(pf); }
		PolicySettings instance; instance.configured=true; PolicySettings workspace_policy,operator_policy;
		if(!app.InstallSyntheticPolicyContextForTests(instance,workspace_policy,operator_policy,registry)) { Cerr()<<"T17 policy fixture setup failed\n"; return false; }
		String run_qid=ws+":"+MapGetString((ValueMap)records[0],"id");
		ValueMap populated_view=AssessmentViewService::Build(app,populated,ws,run_qid,"","candidate","asc","","");
		ValueMap filtered_view=AssessmentViewService::Build(app,populated,ws,run_qid,"candidate-0","candidate","asc","","");
		String overview=AssessmentViewService::Render(populated_view,"overview"),compare=AssessmentViewService::Render(populated_view,"compare");
		ValueArray populated_rows=MapGet(populated_view,"rows"); ValueMap first_candidate=populated_rows.IsEmpty()?ValueMap():(ValueMap)populated_rows[0];
		ValueMap detail_view=AssessmentViewService::Build(app,populated,ws,run_qid,"","candidate","asc","",MapGetString(first_candidate,"id"));
		String detail_html=AssessmentViewService::Render(detail_view,"detail"); String serialized=AsJSON(populated_view,true); ValueMap api_parity=ParseJSON(serialized);
		ValueArray headless_categories=MapGet((ValueMap)populated_rows[0],"categories"); ValueMap headless_category=headless_categories.IsEmpty()?ValueMap():(ValueMap)headless_categories[0];
		if(MapGetString(populated_view,"view_state")!="ready"||MapGet(populated_view,"candidate_count")!=5||MapGet(filtered_view,"filtered_count")!=1||
		   MapGet(populated_view,"unique_visible_source_count")!=1||MapGet(populated_view,"unique_visible_source_revision_count")!=2||
		   overview.Find("synthetic-candidate-5")>=0||compare.Find("synthetic-candidate-5")>=0||compare.Find(denied_source)>=0||compare.Find("research-ready")<0||compare.Find("paper-backed")<0||compare.Find("interesting-risk")<0||compare.Find("avoid-for-now")<0||
		   compare.Find("generic-axis-0")<0||compare.Find("Scrollable candidate assessment table")<0||compare.Find("crowding judgment")<0||compare.Find("opportunity gap")<0||overview.Find("source-r1")<0||overview.Find("source-r2")<0||detail_html.Find("&lt;img src=x onerror=alert(1)&gt;")<0||detail_html.Find("synthetic-t17-workspace%3A")<0||
		   MapGetString(headless_category,"rationale")!="<em>T17 category rationale 0</em>"||MapGetString(headless_category,"crowding_judgment")!="T17 crowding judgment 0"||MapGetString(headless_category,"opportunity_gap")!="T17 opportunity gap 0"||AsJSON(populated_view,true).Find("T17 opportunity gap 0")<0||
		   MapGetString(api_parity,"run_id")!=MapGetString(populated_view,"run_id")||MapGet(api_parity,"candidate_count")!=MapGet(populated_view,"candidate_count")) { ValueArray stale_diagnostics=MapGet(populated_view,"stale_reasons"); Cerr()<<"FAILED T17 populated projection/filter/denial/tier/evidence/parity fixture: state="<<MapGetString(populated_view,"view_state")<<" rows="<<MapGet(populated_view,"candidate_count")<<" source_count="<<MapGet(populated_view,"unique_visible_source_count")<<" source_rev_count="<<MapGet(populated_view,"unique_visible_source_revision_count")<<" filters="<<MapGet(filtered_view,"filtered_count")<<" stale="<<stale_diagnostics.GetCount()<<"\n"; return false; }
		ValueArray stale_views=MapGet(populated_view,"stale_reasons");
		if(stale_views.GetCount()!=1||MapGet((ValueMap)stale_views[0],"replacement_snapshot").IsVoid()||AsJSON(MapGet((ValueMap)stale_views[0],"reason_flags"),true).Find("source_revision_changed")<0) { Cerr()<<"T17 stale reason/replacement snapshot missing: "<<AsJSON(populated_view,true)<<"\n"; return false; }
		ValueMap total_ascending=AssessmentViewService::Build(app,populated,ws,run_qid,"","total","asc","",""); ValueArray total_asc_rows=MapGet(total_ascending,"rows");
		ValueMap total_descending=AssessmentViewService::Build(app,populated,ws,run_qid,"","total","desc","",""); ValueArray total_desc_rows=MapGet(total_descending,"rows");
		if(total_asc_rows.GetCount()!=5||total_desc_rows.GetCount()!=5||MapGetString((ValueMap)total_asc_rows[2],"id")>=MapGetString((ValueMap)total_asc_rows[3],"id")||MapGetString((ValueMap)total_asc_rows[3],"id")>=MapGetString((ValueMap)total_asc_rows[4],"id")||MapGetString((ValueMap)total_desc_rows[0],"id")>=MapGetString((ValueMap)total_desc_rows[1],"id")||MapGetString((ValueMap)total_desc_rows[1],"id")>=MapGetString((ValueMap)total_desc_rows[2],"id")) { Cerr()<<"T17 defined-total tie ordering failed\n"; return false; }
		ValueMap display_fixture=populated_view; ValueArray display_rows=MapGet(display_fixture,"rows"); ValueMap display_row=display_rows[0],display_assessment=MapGet(display_row,"assessment");
		display_assessment.Set("rationale","<candidate rationale>"); display_assessment.Set("coverage","coverage stored by fixture"); display_assessment.Set("uncertainty","uncertainty stored by fixture"); display_row.Set("assessment",display_assessment);
		ValueArray display_categories=MapGet(display_row,"categories"); ValueMap display_category=display_categories[0];
		display_category.Set("rationale","<category rationale>"); display_category.Set("crowding_judgment","crowding stored by fixture"); display_category.Set("opportunity_gap","opportunity stored by fixture"); display_category.Set("coverage","category coverage stored"); display_category.Set("uncertainty","category uncertainty stored"); display_categories.Set(0,display_category); display_row.Set("categories",display_categories); display_rows.Set(0,display_row); display_fixture.Set("rows",display_rows);
		String display_html=AssessmentViewService::Render(display_fixture,"compare"),display_json=AsJSON(display_fixture,true);
		if(display_html.Find("&lt;candidate rationale&gt;")<0||display_html.Find("&lt;category rationale&gt;")<0||display_html.Find("crowding stored by fixture")<0||display_html.Find("opportunity stored by fixture")<0||display_html.Find("category coverage stored")<0||display_html.Find("category uncertainty stored")<0||display_json.Find("<category rationale>")<0||display_json.Find("crowding stored by fixture")<0) return false;
		ValueMap absent_fixture=populated_view; ValueArray absent_rows=MapGet(absent_fixture,"rows"); ValueMap absent_row=absent_rows[0]; ValueArray absent_categories=MapGet(absent_row,"categories"); ValueMap absent_category; absent_category.Add("dimension_id","absent-field-axis"); absent_categories.Set(0,absent_category); absent_row.Set("categories",absent_categories); absent_rows.Set(0,absent_row); absent_fixture.Set("rows",absent_rows);
		String absent_html=AssessmentViewService::Render(absent_fixture,"compare"); if(absent_html.Find("crowding judgment</strong>: Not recorded")<0||absent_html.Find("opportunity gap</strong>: Not recorded")<0) return false;
		if(MapGetString((ValueMap)populated_rows[0],"candidate_id")!="synthetic-candidate-0") { Cerr()<<"T17 candidate sort order failed\n"; return false; }
		ValueMap sorted_desc=AssessmentViewService::Build(app,populated,ws,run_qid,"","candidate","desc","","");
		ValueArray desc_rows=MapGet(sorted_desc,"rows"); if(desc_rows.IsEmpty()||MapGetString((ValueMap)desc_rows[0],"candidate_id")!="synthetic-candidate-4") { Cerr()<<"T17 descending candidate sort failed\n"; return false; }
		ValueMap tier_sorted=AssessmentViewService::Build(app,populated,ws,run_qid,"","tier","asc","",""); ValueArray tier_rows=MapGet(tier_sorted,"rows");
		if(tier_rows.GetCount()!=5||MapGetString((ValueMap)tier_rows[3],"tier")!="research-ready"||MapGetString((ValueMap)tier_rows[4],"tier")!="research-ready"||MapGetString((ValueMap)tier_rows[3],"id")>=MapGetString((ValueMap)tier_rows[4],"id")) { Cerr()<<"T17 tier tie-break failed\n"; return false; }
		ValueMap dimension_sorted=AssessmentViewService::Build(app,populated,ws,run_qid,"","dimension","asc","generic-axis-0",""); ValueArray dimension_rows=MapGet(dimension_sorted,"rows");
		if(dimension_rows.GetCount()!=5||MapGetString((ValueMap)dimension_rows[0],"id")>=MapGetString((ValueMap)dimension_rows[1],"id")) { Cerr()<<"T17 dimension tie-break failed\n"; return false; }
		ValueMap hidden_run=AssessmentViewService::Build(app,populated,ws,ws+":missing-run","","candidate","asc","","");
		ValueArray hidden_rows=MapGet(hidden_run,"rows"),hidden_runs=MapGet(hidden_run,"runs");
		if(MapGetString(hidden_run,"view_state")!="unavailable"||!hidden_rows.IsEmpty()||!hidden_runs.IsEmpty()) { Cerr()<<"T17 hidden run generic denial failed\n"; return false; }
		String empty_provenance_root=workspace.FixtureRoot("t17-empty-provenance"); if(!CreatePrivateDirectory(empty_provenance_root)||!CreatePrivateDirectory(AppendFileName(empty_provenance_root,"data"))||!CreatePrivateDirectory(AppendFileName(empty_provenance_root,"v2"))) return false;
		ValueMap no_source_run=records[0]; no_source_run.Set("source_provenance",ValueArray()); if(!SaveFile(AppendFileName(AppendFileName(empty_provenance_root,"data"),"run.json"),AsJSON(no_source_run,true))) return false;
		DataStore empty_provenance_data; V2Store empty_provenance_v2; if(!empty_provenance_data.LoadFixtureForTests(AppendFileName(empty_provenance_root,"data"))||!empty_provenance_v2.LoadFixtureForTests(AppendFileName(empty_provenance_root,"v2"))) return false;
		One<DataStore> epd; epd.Create(); *epd=empty_provenance_data; One<V2Store> epv; epv.Create(); *epv=empty_provenance_v2; CatalogGeneration empty_provenance(172,pick(epv),pick(epd),true,true);
		ValueMap no_provenance_view=AssessmentViewService::Build(app,empty_provenance,ws,run_qid,"","candidate","asc","",""); ValueArray no_provenance_rows=MapGet(no_provenance_view,"rows");
		if(MapGetString(no_provenance_view,"view_state")!="unavailable"||!no_provenance_rows.IsEmpty()||MapGet(no_provenance_view,"filtered_count")!=0) { Cerr()<<"T17 empty provenance denial failed\n"; return false; }
		String partial_root=workspace.FixtureRoot("t17-partial-catalog"); if(!CreatePrivateDirectory(partial_root)||!CreatePrivateDirectory(AppendFileName(partial_root,"data"))||!CreatePrivateDirectory(AppendFileName(partial_root,"v2"))||!SaveFile(AppendFileName(AppendFileName(partial_root,"data"),"malformed.json"),"{ malformed synthetic source")) return false;
		DataStore partial_data; V2Store partial_v2; if(!partial_data.LoadFixtureForTests(AppendFileName(partial_root,"data"))||!partial_v2.LoadFixtureForTests(AppendFileName(partial_root,"v2"))) return false;
		One<DataStore> pd;pd.Create();*pd=partial_data;One<V2Store> pv;pv.Create();*pv=partial_v2;CatalogGeneration partial(173,pick(pv),pick(pd),true,true);
		ValueMap partial_view=AssessmentViewService::Build(app,partial,ws,run_qid,"","candidate","asc","",""); ValueArray partial_rows=MapGet(partial_view,"rows");
		if(partial.data_status!="loaded_with_errors"||MapGetString(partial_view,"view_state")!="incomplete"||!partial_rows.IsEmpty()) { Cerr()<<"T17 partial catalog denial failed data="<<partial.data_status<<" state="<<MapGetString(partial_view,"view_state")<<"\n"; return false; }
		app.ResetSyntheticPolicyContextForTests();
		Cout()<<"ok    T17 populated isolated six-dimension fixture, candidate-only joins, filters, tier labels, denied contributor suppression and generic direct-run denial\n";
	}
	if(app.IsCatalogIndexEnabled() || app.RebuildCatalogIndex().status != "disabled") {
		Cerr() << "FAILED: catalog index service was not disabled by default\n";
		return false;
	}
	Cout() << "ok    app receives isolated mode/root before initialization and rejects mismatch before store loads\n";
	#ifdef PLATFORM_POSIX
	{
		String root_a = workspace.FixtureRoot("refresh-registration-a");
		String root_b = workspace.FixtureRoot("refresh-registration-b");
		if(!CreatePrivateDirectory(root_a) || !CreatePrivateDirectory(root_b)) return false;
		RealizeDirectory(AppendFileName(root_a, "goals"));
		RealizeDirectory(AppendFileName(root_b, "goals"));
		app.test_registration_provider = true;
		app.test_registration.id = "synthetic-refresh-source";
		app.test_registration.root = NormalizePath(root_a);
		app.test_registration.enabled = true;
		app.test_registration.read_only = false;
		app.test_registration.write_enabled = true;
		app.test_registration_change = app.test_registration;
		app.test_registration_change.root = NormalizePath(root_b);
		app.test_registration_change_point = 2;
		app.test_registration_scanned_root.Clear();
		bool rejected_after_scan = !app.RefreshCatalog();
		auto changed_after_scan = app.PinLatestCatalog();
		bool postload_fail_closed = rejected_after_scan && changed_after_scan && changed_after_scan->status == "load_error" &&
		                           changed_after_scan->v2.GetGoals().IsEmpty() && changed_after_scan->data.GetDocuments().IsEmpty() &&
		                           app.test_registration_scanned_root == NormalizePath(root_a) &&
		                           !FileExists(AppendFileName(root_b, ".mgr-write.lock"));
		if(!postload_fail_closed) {
			Cerr() << "FAILED: refresh did not discard captured-root results on synthetic post-load registration change\n";
			return false;
		}
		app.test_registration = app.test_registration_change;
		FileDelete(AppendFileName(root_a, ".mgr-write.lock"));
		app.test_registration_change = app.test_registration;
		app.test_registration_change.root = NormalizePath(root_a);
		app.test_registration_change_point = 1;
		app.test_registration_scanned_root.Clear();
		bool rejected_before_scan = !app.RefreshCatalog();
		auto changed_before_scan = app.PinLatestCatalog();
		bool prelock_fail_closed = rejected_before_scan && changed_before_scan && changed_before_scan->status == "load_error" &&
		                          changed_before_scan->v2.GetGoals().IsEmpty() && changed_before_scan->data.GetDocuments().IsEmpty() &&
		                          app.test_registration_scanned_root.IsEmpty() &&
		                          !FileExists(AppendFileName(root_a, ".mgr-write.lock")) &&
		                          !FileExists(AppendFileName(root_b, ".mgr-write.lock"));
		if(!prelock_fail_closed) {
		Cerr() << "FAILED: registration change before shared-lock acquisition scanned a source or published records; rejected=" << rejected_before_scan
			       << " status=" << (changed_before_scan ? changed_before_scan->status : String("none"))
			       << " scanned=" << app.test_registration_scanned_root
			       << " lockA=" << FileExists(AppendFileName(root_a, ".mgr-write.lock"))
			       << " lockB=" << FileExists(AppendFileName(root_b, ".mgr-write.lock")) << "\n";
			return false;
		}
		app.test_registration = app.test_registration_change;
		app.test_registration_change_point = 0;
		if(!app.RefreshCatalog() || app.PinLatestCatalog()->status == "load_error" ||
		   app.PinLatestCatalog()->v2.GetRootDir() != NormalizePath(root_a) ||
		   app.PinLatestCatalog()->data.GetRootDir() != NormalizePath(root_a)) {
			Cerr() << "FAILED: refresh did not recover after restoring the captured synthetic registration\n";
			return false;
		}
		String readonly_root = workspace.FixtureRoot("refresh-readonly-source");
		if(!CreatePrivateDirectory(readonly_root)) return false;
		app.test_registration.id = "synthetic-readonly-source";
		app.test_registration.root = NormalizePath(readonly_root);
		app.test_registration.read_only = true;
		app.test_registration.write_enabled = false;
		if(!app.RefreshCatalog() || FileExists(AppendFileName(readonly_root, ".mgr-write.lock"))) {
			Cerr() << "FAILED: read-only refresh created an absent source lock file\n";
			return false;
		}
		String unsafe_root = workspace.FixtureRoot("refresh-readonly-unsafe-lock");
		String unsafe_target = workspace.FixtureRoot("refresh-readonly-lock-target");
		if(!CreatePrivateDirectory(unsafe_root) || !SaveFile(unsafe_target, "synthetic lock target") ||
		   symlink(unsafe_target.ToStd().c_str(), AppendFileName(unsafe_root, ".mgr-write.lock").ToStd().c_str()) != 0) return false;
		int unsafe_fd = -1;
		if(DataStore::AcquireSourceLock(unsafe_root, true, false, unsafe_fd)) {
			DataStore::ReleaseSourceLock(unsafe_fd);
			Cerr() << "FAILED: read-only lock acquisition accepted a symlinked lock artifact\n";
			return false;
		}
		String lock_path = AppendFileName(readonly_root, ".mgr-write.lock");
		int exclusive_fd = open(lock_path.ToStd().c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
		if(exclusive_fd < 0 || flock(exclusive_fd, LOCK_EX) != 0) {
			if(exclusive_fd >= 0) close(exclusive_fd);
			Cerr() << "FAILED: could not establish synthetic exclusive source lock\n";
			return false;
		}
		struct stat before_lock, after_lock;
		if(fstat(exclusive_fd, &before_lock) != 0) { flock(exclusive_fd, LOCK_UN); close(exclusive_fd); return false; }
		std::atomic<bool> refresh_done(false), refresh_ok(false);
		Thread refresh_thread;
		refresh_thread.Run([&]() {
			refresh_ok.store(app.RefreshCatalog());
			refresh_done.store(true);
		});
		Sleep(50);
		bool waited_for_exclusive = !refresh_done.load();
		flock(exclusive_fd, LOCK_UN);
		refresh_thread.Wait();
		bool unchanged_lock = fstat(exclusive_fd, &after_lock) == 0 &&
		                      before_lock.st_ino == after_lock.st_ino &&
		                      before_lock.st_mode == after_lock.st_mode &&
		                      before_lock.st_size == after_lock.st_size;
		close(exclusive_fd);
		if(!waited_for_exclusive || !refresh_ok.load() || !unchanged_lock ||
		   !FileExists(lock_path) || app.PinLatestCatalog()->status == "load_error" ||
		   app.PinLatestCatalog()->v2.GetRootDir() != NormalizePath(readonly_root) ||
		   app.PinLatestCatalog()->data.GetRootDir() != NormalizePath(readonly_root)) {
			Cerr() << "FAILED: read-only refresh did not wait on an existing exclusive lock without changing it\n";
			return false;
		}
		app.test_registration.id = "synthetic-init-source";
		app.test_registration.root = NormalizePath(root_a);
		app.test_registration.enabled = true;
		app.test_registration.read_only = false;
		app.test_registration.write_enabled = true;
		app.test_registration_change = app.test_registration;
		app.test_registration_change.root = NormalizePath(root_b);
		app.test_registration_scanned_root.Clear();
		app.test_registration_change_point = 1;
		FileDelete(AppendFileName(root_a, ".mgr-write.lock"));
		FileDelete(AppendFileName(root_b, ".mgr-write.lock"));
		bool init_rejected_before_lock = !app.InitApp();
		auto init_before_lock = app.PinLatestCatalog();
		if(!init_rejected_before_lock || !init_before_lock || init_before_lock->status != "load_error" ||
		   !init_before_lock->v2.GetGoals().IsEmpty() || !init_before_lock->data.GetDocuments().IsEmpty() ||
		   !app.test_registration_scanned_root.IsEmpty() ||
		   FileExists(AppendFileName(root_a, ".mgr-write.lock")) ||
		   FileExists(AppendFileName(root_b, ".mgr-write.lock"))) {
			Cerr() << "FAILED: InitApp pre-lock registration change touched or scanned a synthetic root\n";
			return false;
		}
		app.test_registration = app.test_registration_change;
		app.test_registration_change = app.test_registration;
		app.test_registration_change.root = NormalizePath(root_a);
		app.test_registration_change_point = 0;
		if(!app.InitApp() || app.PinLatestCatalog()->v2.GetRootDir() != NormalizePath(root_b)) {
			Cerr() << "FAILED: InitApp did not recover at the valid synthetic registration B\n";
			return false;
		}
		app.test_registration = app.test_registration_change;
		app.test_registration_change = app.test_registration;
		app.test_registration_change.root = NormalizePath(root_b);
		app.test_registration_change_point = 3;
		FileDelete(AppendFileName(root_b, ".mgr-write.lock"));
		app.test_registration_scanned_root.Clear();
		bool init_rejected_after_lock = !app.InitApp();
		auto init_after_lock = app.PinLatestCatalog();
		if(!init_rejected_after_lock || !init_after_lock || init_after_lock->status != "load_error" ||
		   !init_after_lock->v2.GetGoals().IsEmpty() || !init_after_lock->data.GetDocuments().IsEmpty() ||
		   !app.test_registration_scanned_root.IsEmpty() ||
		   !FileExists(AppendFileName(root_a, ".mgr-write.lock")) ||
		   FileExists(AppendFileName(root_b, ".mgr-write.lock"))) {
			Cerr() << "FAILED: InitApp post-lock registration change scanned or published a source; rejected=" << init_rejected_after_lock
			       << " status=" << (init_after_lock ? init_after_lock->status : String("none"))
			       << " scanned=" << app.test_registration_scanned_root
			       << " lockA=" << FileExists(AppendFileName(root_a, ".mgr-write.lock"))
			       << " lockB=" << FileExists(AppendFileName(root_b, ".mgr-write.lock")) << "\n";
			return false;
		}
		app.test_registration = app.test_registration_change;
		app.test_registration_change = app.test_registration;
		app.test_registration_change.root = NormalizePath(root_a);
		app.test_registration_change_point = 0;
		if(!app.InitApp() || app.PinLatestCatalog()->v2.GetRootDir() != NormalizePath(root_b)) {
			Cerr() << "FAILED: InitApp did not recover after post-lock mismatch\n";
			return false;
		}
		FileDelete(AppendFileName(root_a, ".mgr-write.lock"));
		app.test_registration_change = app.test_registration;
		app.test_registration_change.root = NormalizePath(root_a);
		app.test_registration_change_point = 2;
		app.test_registration_scanned_root.Clear();
		bool init_rejected_after_scan = !app.InitApp();
		auto init_after_scan = app.PinLatestCatalog();
		if(!init_rejected_after_scan || !init_after_scan || init_after_scan->status != "load_error" ||
		   !init_after_scan->v2.GetGoals().IsEmpty() || !init_after_scan->data.GetDocuments().IsEmpty() ||
		   app.test_registration_scanned_root != NormalizePath(root_b) ||
		   FileExists(AppendFileName(root_a, ".mgr-write.lock"))) {
			Cerr() << "FAILED: InitApp post-scan registration change did not discard candidate generation\n";
			return false;
		}
		app.test_registration = app.test_registration_change;
		app.test_registration_change_point = 0;
		if(!app.InitApp() || app.PinLatestCatalog()->v2.GetRootDir() != NormalizePath(root_a)) {
			Cerr() << "FAILED: InitApp did not recover after post-scan mismatch\n";
			return false;
		}
		FileDelete(AppendFileName(root_a, ".mgr-write.lock"));
		app.test_registration_provider = false;
		if(!app.InitApp() || app.PinLatestCatalog()->v2.GetRootDir() != NormalizePath(AppendFileName(workspace.root, "v2"))) {
			Cerr() << "FAILED: fixture registration provider cleanup did not restore isolated app catalog\n";
			return false;
		}
		Cout() << "ok    RefreshCatalog and InitApp fail closed at pre-lock, post-lock, and post-scan registration changes; both recover and read-only locking stays side-effect-free\n";
	}
	#endif

	String expected_v2_root = AppendFileName(workspace.root, "v2");
	String expected_data_root = AppendFileName(workspace.root, "data");
	auto initial_catalog = app.PinLatestCatalog();
	if(app.GetLocalStore().RootDir() != expected_local_root ||
	   initial_catalog->v2.GetRootDir() != expected_v2_root ||
	   initial_catalog->data.GetRootDir() != expected_data_root ||
	   initial_catalog->data.GetSourceId() != "self-test-fixture") {
		Cerr() << "FAILED: test stores did not capture their isolated workspace roots\n";
		return false;
	}
	// 1. Model & Store Tests
	const V2Store& v2 = initial_catalog->v2;
	if(!v2.GetGoals().IsEmpty() || !v2.GetIssues().IsEmpty() || !v2.GetReleases().IsEmpty() ||
	   !initial_catalog->data.GetDocuments().IsEmpty()) {
		Cerr() << "FAILED: test stores must be empty before synthetic fixtures are seeded\n";
		return false;
	}
	Cout() << "Loaded " << v2.GetGoals().GetCount() << " v2 goals, "
	       << v2.GetIssues().GetCount() << " issues, "
	       << v2.GetReleases().GetCount() << " releases\n";

	ManagerStore& local = app.GetLocalStore();
	if(local.Projects().IsEmpty()) {
		Cout() << "Notice: Seeding standalone self-test fixture project...\n";
		Project p;
		p.slug = "manager-skylark";
		p.title = "Synthetic <script>alert(7)</script> workspace";
		p.status = "active";
		p.description = "Test fixture for ManagerSkylark automated verification";
		local.SaveProject(p);

		Phase ph;
		ph.project = "manager-skylark";
		ph.seq = 1;
		ph.slug = "self-test";
		ph.title = "Self Test Phase";
		ph.status = "in-progress";
		local.SavePhase(ph);

		Task t;
		t.project = "manager-skylark";
		t.phase_seq = 1;
		t.seq = 1;
		t.slug = "automated-test-verification";
		t.title = "Run automated tests";
		t.status = "open";
		local.SaveTask(t);

		local.LoadAll();
	}
	Cout() << "Loaded " << local.Projects().GetCount() << " local projects\n";

	if(local.Projects().GetCount() != 1 || local.Projects()[0].slug != "manager-skylark") {
		Cerr() << "FAILED: local project store contains data outside the synthetic fixture\n";
		return false;
	}

	// 1b. Generic DataStore Fixture & Logic Tests
	// Setup a temporary, isolated synthetic source root.
	// Ensures the generic entry point logic (searching recursively for *.json) works.
	Cout() << "=== Generic DataStore, DiagramRenderer & TopologyGraph Verification ===\n";
	{
		String hostile_title = "<script>alert(9)</script>\" onload=\"alert(10)";
		String page = PageBuilder().Title(hostile_title).TrustedContent("<p>synthetic content</p>").Build();
		if(page.Find("<title>" + hostile_title + "</title>") >= 0 ||
		   page.Find("<title>&lt;script&gt;alert(9)&lt;/script&gt;&quot; onload=&quot;alert(10)</title>") < 0) {
			Cerr() << "FAILED: PageBuilder did not escape hostile document-title text\n";
			return false;
		}
		Cout() << "ok    PageBuilder escapes hostile metadata before HTML insertion\n";
	}

	String fixture_root = AppendFileName(workspace.root, "data");
	auto record_envelope = [](const String& schema, const String& kind, const String& id) {
		ValueMap envelope;
		envelope.Add("schema_id", "skylark/" + schema);
		envelope.Add("schema_version", "1");
		envelope.Add("workspace_id", "synthetic-workspace");
		envelope.Add("stable_id", id);
		envelope.Add("record_kind", kind);
		envelope.Add("revision", "1");
		envelope.Add("visibility", "private");
		envelope.Add("provenance_ref", "synthetic-fixture");
		return envelope;
	};

	// SQLite index tests use only this isolated synthetic source and explicit temp target.
	{
		String index_source = workspace.FixtureRoot("index-source");
		String index_record_dir = AppendFileName(index_source, "papers");
		String index_record = AppendFileName(index_record_dir, "synthetic.json");
		String db_path = AppendFileName(GetFileFolder(index_source), "private-index/catalog.sqlite3");
		if(!CreatePrivateDirectory(index_source) || !CreatePrivateDirectory(index_record_dir) ||
		   !CreatePrivateDirectory(GetFileFolder(db_path))) return false;
		if(app.IsCatalogIndexEnabled() || FileExists(db_path)) {
			Cerr() << "FAILED: default test startup opened or created the synthetic index\n";
			return false;
		}
		ValueMap indexed;
		indexed.Add("id", "index-' OR 1=1 --");
		indexed.Add("kind", "paper");
		indexed.Add("title", "private synthetic title");
		indexed.Add("status", "draft");
		indexed.Add("envelope", record_envelope("paper", "paper", "index-' OR 1=1 --"));
		String canonical = AsJSON(indexed, true);
		if(!SaveFile(index_record, canonical)) return false;
		One<V2Store> index_v2; index_v2.Create();
		One<DataStore> index_data; index_data.Create();
		if(!index_v2->LoadFixtureForTests(index_source) || !index_data->LoadFixtureForTests(index_source)) {
			Cerr() << "FAILED: synthetic catalog index source load\n";
			return false;
		}
		CatalogGeneration index_generation(1, pick(index_v2), pick(index_data), true, true);
		String query_status;
		Vector<const DataDocument*> default_json = app.QueryCatalogDocuments(index_generation, "paper", String(), String(), String(), &query_status);
		if(query_status != "disabled_json" || default_json.GetCount() != 1 || FileExists(db_path)) {
			Cerr() << "FAILED: default index-disabled query did not remain canonical JSON/no-I/O\n";
			return false;
		}
		CatalogIndexResult rebuilt = CatalogIndex::Rebuild(index_generation, db_path);
		// T54 parity matrix: collisions are resolved by workspace-qualified IDs,
		// while invalid and quarantined records remain explicit index projections.
		String matrix_workspace = workspace.FixtureRoot("index-matrix-workspace");
		String matrix_source = AppendFileName(matrix_workspace, "data");
		String matrix_db = AppendFileName(matrix_workspace, "private-index/catalog.sqlite3");
		if(!CreatePrivateDirectory(matrix_workspace) ||
		   !CreatePrivateDirectory(AppendFileName(matrix_workspace, "private-index"))) {
			Cerr() << "FAILED: T54 could not create the isolated matrix index directory\n";
			return false;
		}
		auto save_matrix_record = [&](const String& workspace_id, const String& scope, const String& id,
		                              const String& title, bool include_title, const String& schema_kind) {
			String folder = AppendFileName(matrix_source, "papers/" + scope);
			RealizeDirectory(folder);
			ValueMap item; item.Add("id", id); item.Add("kind", "paper"); item.Add("source", "synthetic-source");
			if(include_title) item.Add("title", title);
			item.Add("status", "draft");
			item.Add("envelope", record_envelope(schema_kind, "paper", id));
			ValueMap env = MapGet(item, "envelope"); env.Set("workspace_id", workspace_id); item.Set("envelope", env);
			return SaveFile(AppendFileName(folder, workspace_id + "-" + id + ".json"), AsJSON(item, true));
		};
		if(!save_matrix_record("workspace-a", "scope-a", "collision", "A collision", true, "paper") ||
		   !save_matrix_record("workspace-b", "scope-a", "collision", "B collision", true, "paper") ||
		   !save_matrix_record("workspace-a", "scope-b", "tie", "Tie title", true, "paper") ||
		   !save_matrix_record("workspace-a", "scope-b", "tie-peer", "Tie title", true, "paper") ||
		   !save_matrix_record("workspace-a", "scope-b", "unique-alias", "Unique alias", true, "paper") ||
		   !save_matrix_record("workspace-a", "scope-b", "invalid", "", false, "paper") ||
		   !save_matrix_record("workspace-b", "scope-c", "quarantined", "Unsupported", true, "unknown-paper")) {
			Cerr() << "FAILED: T54 could not write synthetic matrix records\n";
			return false;
		}
		One<V2Store> matrix_v2; matrix_v2.Create();
		One<DataStore> matrix_data; matrix_data.Create();
		if(!matrix_v2->LoadFixtureForTests(matrix_source) || !matrix_data->LoadFixtureForTests(matrix_source)) {
			Cerr() << "FAILED: T54 could not load synthetic matrix stores\n";
			return false;
		}
		CatalogGeneration matrix_generation(10, pick(matrix_v2), pick(matrix_data), true, true);
		CatalogIndexResult matrix_rebuild = CatalogIndex::Rebuild(matrix_generation, matrix_db);
		if(matrix_rebuild.status != "ready") {
			Cerr() << "FAILED: T54 matrix index rebuild status=" << matrix_rebuild.status << " diagnostic=" << matrix_rebuild.diagnostic << "\n";
			return false;
		}
		String matrix_status;
		CatalogIndexPin matrix_pin = CatalogIndex::Pin(matrix_generation, matrix_status);
		CatalogIndexFilter matrix_filter; matrix_filter.data_documents_only = true;
		Vector<CatalogIndexRow> matrix_rows = CatalogIndex::Page(matrix_pin, matrix_filter, 0, 20, matrix_status);
		Vector<const DataDocument*> canonical_matrix;
		for(const DataDocument& d : matrix_generation.data.GetDocuments()) canonical_matrix.Add(&d);
		Sort(canonical_matrix, [](const DataDocument *a, const DataDocument *b) {
			if(a->source_id != b->source_id) return a->source_id < b->source_id;
			if(a->workspace_id != b->workspace_id) return a->workspace_id < b->workspace_id;
			return a->qualified_id < b->qualified_id;
		});
		bool matrix_parity = matrix_status == "ready" && matrix_rows.GetCount() == canonical_matrix.GetCount() && matrix_rows.GetCount() == 7;
		String invalid_id, quarantined_id;
		for(const DataDocument& d : matrix_generation.data.GetDocuments()) {
			if(d.validation_state == "invalid") invalid_id = d.qualified_id;
			if(d.validation_state == "quarantined") quarantined_id = d.qualified_id;
		}
		for(int i = 0; matrix_parity && i < matrix_rows.GetCount(); i++) {
			const CatalogIndexRow& row = matrix_rows[i]; const DataDocument& d = *canonical_matrix[i];
			matrix_parity = row.source_id == d.source_id && row.workspace_id == d.workspace_id &&
			                row.qualified_id == d.qualified_id && row.collection == d.collection && row.kind == d.kind &&
			                row.schema_id == d.schema_id && row.schema_version == d.schema_version && row.revision == d.revision &&
			                row.source_revision == d.source_revision && row.scope == d.scope && row.visibility == d.visibility &&
			                row.validation_state == d.validation_state && row.title == d.title && row.status == d.status;
		}
		CatalogIndexFilter workspace_filter; workspace_filter.workspace_id = "workspace-a"; workspace_filter.data_documents_only = true;
		Vector<CatalogIndexRow> workspace_rows = CatalogIndex::Page(matrix_pin, workspace_filter, 0, 20, matrix_status);
		CatalogIndexFilter scope_filter; scope_filter.scope = "scope-b"; scope_filter.data_documents_only = true;
		Vector<CatalogIndexRow> scope_rows = CatalogIndex::Page(matrix_pin, scope_filter, 0, 20, matrix_status);
		CatalogIndexFilter workspace_b_filter; workspace_b_filter.workspace_id = "workspace-b"; workspace_b_filter.data_documents_only = true;
		Vector<CatalogIndexRow> workspace_b_rows = CatalogIndex::Page(matrix_pin, workspace_b_filter, 0, 20, matrix_status);
		Vector<CatalogIndexRow> first_page = CatalogIndex::Page(matrix_pin, matrix_filter, 0, 2, matrix_status);
		Vector<CatalogIndexRow> second_page = CatalogIndex::Page(matrix_pin, matrix_filter, 2, 2, matrix_status);
		Vector<CatalogIndexRow> third_page = CatalogIndex::Page(matrix_pin, matrix_filter, 4, 2, matrix_status);
		Vector<CatalogIndexRow> fourth_page = CatalogIndex::Page(matrix_pin, matrix_filter, 6, 2, matrix_status);
		Vector<CatalogIndexRow> end_page = CatalogIndex::Page(matrix_pin, matrix_filter, 8, 2, matrix_status);
		Vector<CatalogIndexRow> concatenated_pages;
		for(const CatalogIndexRow& row : first_page) concatenated_pages.Add(row);
		for(const CatalogIndexRow& row : second_page) concatenated_pages.Add(row);
		for(const CatalogIndexRow& row : third_page) concatenated_pages.Add(row);
		for(const CatalogIndexRow& row : fourth_page) concatenated_pages.Add(row);
		CatalogIndexFilter absent_filter; absent_filter.workspace_id = "workspace-missing";
		Vector<CatalogIndexRow> absent_rows = CatalogIndex::Page(matrix_pin, absent_filter, 0, 10, matrix_status);
		Vector<String> expected_workspace_ids = { "workspace-a:collision", "workspace-a:invalid", "workspace-a:tie",
		                                          "workspace-a:tie-peer", "workspace-a:unique-alias" };
		Vector<String> expected_scope_ids = { "workspace-a:invalid", "workspace-a:tie", "workspace-a:tie-peer", "workspace-a:unique-alias" };
		Vector<String> expected_workspace_b_ids = { "workspace-b:collision", "workspace-b:quarantined" };
		Vector<String> actual_workspace_ids, actual_scope_ids, actual_workspace_b_ids, actual_page_ids;
		for(const CatalogIndexRow& row : workspace_rows) actual_workspace_ids.Add(row.qualified_id);
		for(const CatalogIndexRow& row : scope_rows) actual_scope_ids.Add(row.qualified_id);
		for(const CatalogIndexRow& row : workspace_b_rows) actual_workspace_b_ids.Add(row.qualified_id);
		for(const CatalogIndexRow& row : concatenated_pages) actual_page_ids.Add(row.qualified_id);
		Vector<String> expected_page_ids;
		for(const DataDocument *d : canonical_matrix) expected_page_ids.Add(d->qualified_id);
		Vector<CatalogIndexRow> empty_page = CatalogIndex::Page(matrix_pin, matrix_filter, 20, 2, matrix_status);
		Vector<const DataDocument*> empty_json;
		for(const DataDocument& d : matrix_generation.data.GetDocuments())
			if(d.workspace_id == "workspace-missing") empty_json.Add(&d);
		CatalogRecordRead unique_alias = ReadCatalogRecord(matrix_generation, "unique-alias", true);
		CatalogRecordRead ambiguous_alias = ReadCatalogRecord(matrix_generation, "collision", true);
		const DataDocument *unique_alias_doc = matrix_generation.data.FindDocument("workspace-a:unique-alias");
		const CatalogIndexRow *unique_alias_row = NULL;
		for(const CatalogIndexRow& row : matrix_rows) if(row.qualified_id == "workspace-a:unique-alias") unique_alias_row = &row;
		ValueMap unique_alias_record = MapGet(unique_alias.response, "record");
		bool alias_parity = unique_alias.status == "ok" && ambiguous_alias.status == "ambiguous" && unique_alias_doc && unique_alias_row &&
		                    MapGetString(unique_alias.response, "qualified_id") == unique_alias_doc->qualified_id &&
		                    AsJSON(unique_alias_record) == AsJSON(unique_alias_doc->data) &&
		                    unique_alias_row->qualified_id == unique_alias_doc->qualified_id &&
		                    unique_alias_row->title == unique_alias_doc->title && unique_alias_row->status == unique_alias_doc->status;
		bool tie_order = matrix_rows.GetCount() >= 4 && matrix_rows[2].title == matrix_rows[3].title &&
		                 matrix_rows[2].workspace_id == matrix_rows[3].workspace_id &&
		                 matrix_rows[2].qualified_id == "workspace-a:tie" && matrix_rows[3].qualified_id == "workspace-a:tie-peer";
		bool empty_semantics = matrix_status == "ready" && absent_rows.IsEmpty() && empty_page.IsEmpty() && empty_json.IsEmpty();
		if(!matrix_parity || actual_workspace_ids.GetCount() != expected_workspace_ids.GetCount() ||
		   actual_scope_ids.GetCount() != expected_scope_ids.GetCount() || actual_workspace_b_ids.GetCount() != expected_workspace_b_ids.GetCount() ||
		   actual_page_ids.GetCount() != expected_page_ids.GetCount() || invalid_id != "workspace-a:invalid" ||
		   quarantined_id != "workspace-b:quarantined" || !alias_parity || !tie_order || !empty_semantics ||
		   first_page.GetCount() != 2 || second_page.GetCount() != 2 || third_page.GetCount() != 2 ||
		   fourth_page.GetCount() != 1 || !end_page.IsEmpty()) {
			Cerr() << "FAILED: T54 workspace/scope/alias/state/order/page-boundary parity matrix; rows=" << matrix_rows.GetCount()
			       << " canonical=" << canonical_matrix.GetCount() << " workspace_ids=" << actual_workspace_ids.GetCount()
			       << " scope_ids=" << actual_scope_ids.GetCount() << " workspace_b_ids=" << actual_workspace_b_ids.GetCount()
			       << " page_ids=" << actual_page_ids.GetCount() << " alias=" << alias_parity << " tie=" << tie_order
			       << " invalid=" << invalid_id << " quarantine=" << quarantined_id << " empty=" << empty_semantics
			       << " status=" << matrix_status << " parity=" << matrix_parity << "\n";
			return false;
		}
		for(int i = 0; i < expected_workspace_ids.GetCount(); i++) if(actual_workspace_ids[i] != expected_workspace_ids[i]) return false;
		for(int i = 0; i < expected_scope_ids.GetCount(); i++) if(actual_scope_ids[i] != expected_scope_ids[i]) return false;
		for(int i = 0; i < expected_workspace_b_ids.GetCount(); i++) if(actual_workspace_b_ids[i] != expected_workspace_b_ids[i]) return false;
		for(int i = 0; i < expected_page_ids.GetCount(); i++) if(actual_page_ids[i] != expected_page_ids[i]) return false;
		Cout() << "ok    T54 SQLite/JSON qualified identity, workspace/scope filters, validation states, ordering and pagination parity\n";
		String page_status;
		Vector<CatalogIndexRow> page = CatalogIndex::Page(db_path, index_generation.source_revision, 0, 10, page_status);
		String pin_status;
		CatalogIndexPin generation_pin = CatalogIndex::Pin(index_generation, pin_status);
		CatalogIndexFilter paper_filter;
		paper_filter.kind = "paper";
		Vector<CatalogIndexRow> pinned_first = CatalogIndex::Page(generation_pin, paper_filter, 0, 1, page_status);
		Vector<CatalogIndexRow> pinned_second = CatalogIndex::Page(generation_pin, paper_filter, 1, 1, page_status);
		CatalogIndexFilter injection_filter;
		injection_filter.kind = "paper' OR 1=1 --";
		Vector<CatalogIndexRow> injected = CatalogIndex::Page(generation_pin, injection_filter, 0, 10, page_status);
		app.EnableCatalogIndex(true);
		Vector<const DataDocument*> indexed_docs = app.QueryCatalogDocuments(index_generation, "paper", String(), String(), String(), &query_status);
		bool secure_mode = true;
#ifdef PLATFORM_POSIX
		struct stat index_stat;
		String index_parent = GetFileFolder(db_path);
		secure_mode = stat(index_parent.ToStd().c_str(), &index_stat) == 0 && (index_stat.st_mode & 0777) == 0700 &&
		              stat(db_path.ToStd().c_str(), &index_stat) == 0 && (index_stat.st_mode & 0777) == 0600;
		for(const String& sidecar : { db_path + "-wal", db_path + "-shm" })
			if(FileExists(sidecar)) secure_mode = secure_mode && stat(sidecar.ToStd().c_str(), &index_stat) == 0 && (index_stat.st_mode & 0777) == 0600;
#endif
		if(rebuilt.status != "ready" || page_status != "ready" || page.GetCount() != 1 ||
		   page[0].title != "private synthetic title" || page[0].qualified_id.Find("' OR 1=1 --") < 0 ||
		   !generation_pin.IsValid() || pin_status != "ready" || pinned_first.GetCount() != 1 ||
		   !pinned_second.IsEmpty() || pinned_first[0].title != page[0].title || !injected.IsEmpty() ||
		   app.IsCatalogIndexEnabled() == false || query_status != "sqlite_pinned" || indexed_docs.GetCount() != 1 ||
		   default_json.GetCount() != 1 || default_json[0]->qualified_id != indexed_docs[0]->qualified_id ||
		   CatalogIndex::Check(db_path, "different-generation").status != "stale" ||
		   CatalogIndex::Check(db_path, "different-generation").diagnostic != "source_hash_mismatch" ||
		   LoadFile(index_record) != canonical || !secure_mode) {
			Cerr() << "FAILED: synthetic index rebuild/parity/generation/permissions: rebuild=" << rebuilt.status
			       << " page=" << page_status << " count=" << page.GetCount() << " secure=" << secure_mode << "\n";
			return false;
		}
#ifdef PLATFORM_POSIX
		sqlite3 *permission_probe = NULL;
		bool sidecars_private = sqlite3_open_v2(db_path.ToStd().c_str(), &permission_probe,
		                                       SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, NULL) == SQLITE_OK;
		if(sidecars_private) sidecars_private = sqlite3_exec(permission_probe,
			"BEGIN IMMEDIATE; INSERT INTO metadata(key,value) VALUES('synthetic_permission_probe','1')", NULL, NULL, NULL) == SQLITE_OK;
		for(const String& sidecar : { db_path + "-wal", db_path + "-shm" }) {
			struct stat sidecar_stat;
			sidecars_private = sidecars_private && stat(sidecar.ToStd().c_str(), &sidecar_stat) == 0 &&
			                   (sidecar_stat.st_mode & 0777) == 0600 && sidecar_stat.st_uid == geteuid();
		}
		if(permission_probe) {
			sqlite3_exec(permission_probe, "ROLLBACK", NULL, NULL, NULL);
			sqlite3_close(permission_probe);
		}
		if(!sidecars_private) {
			Cerr() << "FAILED: SQLite WAL/SHM artifacts were not owner-only\n";
			return false;
		}
#endif
		if(CatalogIndex::Rebuild(index_generation, db_path, CATALOG_INDEX_FAIL_BEFORE_PUBLISH).status != "rebuild_failed" ||
		   CatalogIndex::Check(db_path, index_generation.source_revision).status != "ready") {
			Cerr() << "FAILED: interrupted index transaction did not preserve the published generation\n";
			return false;
		}
		std::atomic<int> concurrent_failures(0);
		auto read_index = [&]() {
			String status;
			Vector<CatalogIndexRow> rows = CatalogIndex::Page(db_path, index_generation.source_revision, 0, 10, status);
			if(status != "ready" || rows.GetCount() != 1) concurrent_failures++;
		};
		std::thread reader1(read_index), reader2(read_index), reader3(read_index), reader4(read_index);
		reader1.join(); reader2.join(); reader3.join(); reader4.join();
		if(concurrent_failures) { Cerr() << "FAILED: concurrent index readers\n"; return false; }
		ValueMap changed = ParseJSON(canonical);
		changed.Set("title", "edited synthetic title");
		if(!SaveFile(index_record, AsJSON(changed, true))) return false;
		One<V2Store> edited_v2; edited_v2.Create();
		One<DataStore> edited_data; edited_data.Create();
		if(!edited_v2->LoadFixtureForTests(index_source) || !edited_data->LoadFixtureForTests(index_source)) return false;
		CatalogGeneration edited_generation(2, pick(edited_v2), pick(edited_data), true, true);
		if(CatalogIndex::Rebuild(edited_generation, db_path).status != "ready") return false;
		Vector<const DataDocument*> edited_docs = app.QueryCatalogDocuments(edited_generation, "paper", String(), String(), String(), &query_status);
		if(query_status != "sqlite_pinned" || edited_docs.GetCount() != 1 || edited_docs[0]->title != "edited synthetic title") return false;
		Vector<CatalogIndexRow> old_pin_after_rebuild = CatalogIndex::Page(generation_pin, paper_filter, 0, 10, page_status);
		Vector<const DataDocument*> old_json_fallback = app.QueryCatalogDocuments(index_generation, "paper", String(), String(), String(), &query_status);
		if(page_status != "ready" || old_pin_after_rebuild.GetCount() != 1 ||
		   old_pin_after_rebuild[0].title != "private synthetic title" ||
		   query_status != "fallback_json:stale" || old_json_fallback.GetCount() != 1 ||
		   old_json_fallback[0]->title != "private synthetic title") {
			Cerr() << "FAILED: pinned index generation or same-catalog JSON fallback changed after rebuild\n";
			return false;
		}
		page = CatalogIndex::Page(db_path, edited_generation.source_revision, 0, 10, page_status);
		if(page_status != "ready" || page.GetCount() != 1 || page[0].title != "edited synthetic title" ||
		   CatalogIndex::Page(db_path, index_generation.source_revision, 0, 10, page_status).GetCount() != 0 || page_status != "stale") {
			Cerr() << "FAILED: edited source did not atomically replace its indexed generation\n";
			return false;
		}
		if(!FileDelete(index_record)) return false;
		One<V2Store> deleted_v2; deleted_v2.Create();
		One<DataStore> deleted_data; deleted_data.Create();
		if(!deleted_v2->LoadFixtureForTests(index_source) || !deleted_data->LoadFixtureForTests(index_source)) return false;
		CatalogGeneration deleted_generation(3, pick(deleted_v2), pick(deleted_data), true, true);
		if(CatalogIndex::Rebuild(deleted_generation, db_path).status != "ready" ||
		   !CatalogIndex::Page(db_path, deleted_generation.source_revision, 0, 10, page_status).IsEmpty() || page_status != "ready") {
			Cerr() << "FAILED: deleted canonical record remained in rebuilt projection\n";
			return false;
		}
		Vector<const DataDocument*> deleted_docs = app.QueryCatalogDocuments(deleted_generation, "paper", String(), String(), String(), &query_status);
		if(query_status != "sqlite_pinned" || !deleted_docs.IsEmpty()) return false;
		One<V2Store> bad_v2; bad_v2.Create();
		One<DataStore> bad_data; bad_data.Create();
		CatalogGeneration bad_generation(4, pick(bad_v2), pick(bad_data), false, false, "synthetic_source_error");
		if(CatalogIndex::Rebuild(bad_generation, db_path).status != "source_incomplete" ||
		   CatalogIndex::Check(db_path, deleted_generation.source_revision).status != "ready") {
			Cerr() << "FAILED: failed source load replaced or misreported published index\n";
			return false;
		}
		if(CatalogIndex::Rebuild(index_generation, db_path).status != "ready" ||
		   CatalogIndex::Page(db_path, deleted_generation.source_revision, 1, 1, page_status).GetCount() != 0 ||
	   page_status != "stale") {
			Cerr() << "FAILED: synthetic index stable pagination/rebuild\n";
			return false;
		}
		String missing_path = AppendFileName(workspace.FixtureRoot("missing-index"), "catalog.sqlite3");
		if(CatalogIndex::Check(missing_path, index_generation.source_revision).status != "missing") return false;
		String corrupt_root = workspace.FixtureRoot("corrupt-index");
		if(!CreatePrivateDirectory(corrupt_root)) return false;
		String corrupt_path = AppendFileName(corrupt_root, "catalog.sqlite3");
		if(!SaveFile(corrupt_path, "not sqlite") || CatalogIndex::Check(corrupt_path, index_generation.source_revision).status == "ready") return false;
		app.EnableCatalogIndex(false);
		Vector<const DataDocument*> disabled_again = app.QueryCatalogDocuments(index_generation, "paper", String(), String(), String(), &query_status);
		if(query_status != "disabled_json" || disabled_again.GetCount() != 1 || app.IsCatalogIndexEnabled()) {
			Cerr() << "FAILED: explicitly disabled index did not return same-generation JSON projection\n";
			return false;
		}
		app.EnableCatalogIndex(true);
		if(!SaveFile(db_path, "not sqlite")) return false;
		Vector<const DataDocument*> corrupt_fallback = app.QueryCatalogDocuments(index_generation, "paper", String(), String(), String(), &query_status);
		if(query_status != "fallback_json:unavailable" || corrupt_fallback.GetCount() != 1 ||
		   corrupt_fallback[0]->title != "private synthetic title") {
			Cerr() << "FAILED: corrupt index did not fall back to the same pinned canonical generation\n";
			return false;
		}
		app.EnableCatalogIndex(false);
		Cout() << "ok    opt-in SQLite query projection pins generations; default/stale/corrupt paths stay on canonical JSON\n";
		Cout() << "ok    synthetic SQLite projection is derived, generation-checked, parameter-safe and owner-only\n";
	}

	// T40 exercises only a dedicated synthetic source below the self-test temp root.
	{
	String manifest = "{\"version\":1,\"operation_id\":\"op-1\",\"source_id\":\"synthetic\",\"workspace_id\":\"synthetic-workspace\",\"state\":\"prepared\",\"records\":[{\"source_id\":\"synthetic\",\"record_id\":\"r-1\",\"path\":\"papers/r-1.json\",\"expected_revision\":\"1\",\"new_revision\":\"2\",\"backup_ref\":\"backup-0.bin\",\"payload_ref\":\"payload-0.bin\",\"old_hash\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"new_hash\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"state\":\"pending\"}]}";
		JournalManifest parsed;
		String oversized;
		for(int i = 0; i < 65537; i++) oversized.Cat('x');
		if(!DataStore::ParseJournalManifest(manifest, parsed) || parsed.version != 1 || parsed.records.GetCount() != 1 ||
		   DataStore::ParseJournalManifest(manifest + " ", parsed) == false ||
		   DataStore::ParseJournalManifest(oversized, parsed) ||
		   DataStore::ParseJournalManifest("{\\\"version\\\":1}", parsed)) {
			Cerr() << "FAILED: bounded journal manifest parser accepted/rejected an invalid case\n";
			return false;
		}
		String journal_root = workspace.FixtureRoot("journal-source");
		String journal_dir = AppendFileName(journal_root, ".mgr-journal");
		if(!CreatePrivateDirectory(journal_root) || !CreatePrivateDirectory(journal_dir) ||
		   !SaveFile(AppendFileName(journal_dir, "op-1.json"), manifest)) {
			Cerr() << "FAILED: synthetic journal fixture setup\n";
			return false;
		}
		DataStore blocked_catalog;
		if(blocked_catalog.LoadFixtureForTests(journal_root) || blocked_catalog.GetDocuments().GetCount() != 0 ||
		   blocked_catalog.GetSourceDiagnostics().GetCount() != 1 || blocked_catalog.GetSourceDiagnostics()[0] != "journal:recovery_required") {
			Cerr() << "FAILED: catalog exposed a source with unresolved journal state\n";
			return false;
		}
		Cout() << "ok    bounded journal manifest parsing and catalog fail-closed visibility\n";
	}
	#ifdef PLATFORM_POSIX
	{
		String lock_root = workspace.FixtureRoot("catalog-lock");
		if(!CreatePrivateDirectory(lock_root)) return false;
		DataStore read_only_catalog;
		if(!read_only_catalog.LoadFixtureForTests(lock_root) || FileExists(AppendFileName(lock_root, ".mgr-write.lock"))) {
			Cerr() << "FAILED: read-only catalog load created a writer lock artifact\n";
			return false;
		}
		int shared_fd = -1, second_shared_fd = -1;
		if(!DataStore::AcquireSourceLock(lock_root, true, true, shared_fd) ||
		   !DataStore::AcquireSourceLock(lock_root, true, true, second_shared_fd)) {
			DataStore::ReleaseSourceLock(shared_fd);
			DataStore::ReleaseSourceLock(second_shared_fd);
			Cerr() << "FAILED: writable source could not acquire shared catalog locks\n";
			return false;
		}
		int probe_fd = open(AppendFileName(lock_root, ".mgr-write.lock").ToStd().c_str(), O_RDWR);
		bool exclusive_blocked = probe_fd >= 0 && flock(probe_fd, LOCK_EX | LOCK_NB) != 0;
		if(probe_fd >= 0) close(probe_fd);
		DataStore::ReleaseSourceLock(second_shared_fd);
		DataStore::ReleaseSourceLock(shared_fd);
		if(!exclusive_blocked) {
			Cerr() << "FAILED: shared catalog lock did not exclude an exclusive writer lock\n";
			return false;
		}
		String root_a = workspace.FixtureRoot("captured-root-a");
		String root_b = workspace.FixtureRoot("captured-root-b");
		String goal_a = AppendFileName(root_a, "goals/synthetic/captured.json");
		String goal_b = AppendFileName(root_b, "goals/synthetic/other.json");
		if(!CreatePrivateDirectory(root_a) || !CreatePrivateDirectory(root_b)) return false;
		RealizeDirectory(GetFileFolder(goal_a));
		RealizeDirectory(GetFileFolder(goal_b));
		String record_a = "{\"id\":\"synthetic:goal:captured-a\",\"kind\":\"goal\",\"repo\":\"synthetic\",\"title\":\"captured A\",\"tags\":[],\"state\":{},\"tasks\":[]}";
		String record_b = "{\"id\":\"synthetic:goal:captured-b\",\"kind\":\"goal\",\"repo\":\"synthetic\",\"title\":\"captured B\",\"tags\":[],\"state\":{},\"tasks\":[]}";
		if(!SaveFile(goal_a, record_a) || !SaveFile(goal_b, record_b)) return false;
		DataSourceRegistration captured_registration;
		captured_registration.id = "synthetic-captured";
		captured_registration.root = NormalizePath(root_a);
		captured_registration.enabled = true;
		captured_registration.read_only = false;
		captured_registration.write_enabled = true;
		One<V2Store> captured_v2; captured_v2.Create();
		One<DataStore> captured_data; captured_data.Create();
		String captured_error;
		Vector<String> captured_diagnostics;
		bool captured_loaded = ManagerSkylarkApp::LoadCatalogRegistration(captured_registration,
			captured_v2, captured_data, captured_error, captured_diagnostics);
		bool captured_only_a = captured_loaded && captured_v2->GetRootDir() == NormalizePath(root_a) &&
		                       captured_data->GetRootDir() == NormalizePath(root_a) &&
		                       captured_v2->GetGoals().GetCount() == 1 &&
		                       captured_v2->GetGoals()[0].title == "captured A";
		if(!captured_only_a) {
			Cerr() << "FAILED: catalog loaders did not remain bound to captured root; loaded=" << captured_loaded
			       << " error=" << captured_error << " v2root=" << captured_v2->GetRootDir()
			       << " dataroot=" << captured_data->GetRootDir() << " goals=" << captured_v2->GetGoals().GetCount() << "\n";
			return false;
		}
		Cout() << "ok    writable catalog scanners remain bound to the captured root when another synthetic root exists\n";
	}
	#endif
	{
		String write_root = workspace.FixtureRoot("write-source");
		String write_dir = AppendFileName(write_root, "papers");
		String analysis_dir = AppendFileName(write_root, "analyses");
		if(!CreatePrivateDirectory(write_root) || !CreatePrivateDirectory(write_dir) || !CreatePrivateDirectory(analysis_dir)) {
			Cerr() << "FAILED: isolated write fixture directory creation\n";
			return false;
		}
		ValueMap record;
		record.Add("id", "write-paper");
		record.Add("kind", "paper");
		record.Add("title", "Synthetic original");
		record.Add("source", "synthetic");
		record.Add("status", "draft");
		record.Add("envelope", record_envelope("paper", "paper", "write-paper"));
		String record_path = AppendFileName(write_dir, "write-paper.json");
		String original_bytes = AsJSON(record, true);
		SaveFile(record_path, original_bytes);
		ValueMap concurrent_record = record;
		concurrent_record.Set("id", "concurrent-paper");
		ValueMap concurrent_envelope = MapGet(concurrent_record, "envelope");
		concurrent_envelope.Set("stable_id", "concurrent-paper");
		concurrent_record.Set("envelope", concurrent_envelope);
		String concurrent_path = AppendFileName(write_dir, "concurrent-paper.json");
		String concurrent_bytes = AsJSON(concurrent_record, true);
		SaveFile(concurrent_path, concurrent_bytes);
		ValueMap analysis;
		analysis.Add("id", "analysis-1");
		analysis.Add("kind", "paper-analysis");
		analysis.Add("paper", "write-paper");
		analysis.Add("repo", "synthetic");
		analysis.Add("version", "v1");
		analysis.Add("tasks", ValueArray());
		analysis.Add("findings", ValueArray());
		analysis.Add("envelope", record_envelope("paper-analysis", "paper-analysis", "analysis-1"));
		String analysis_path = AppendFileName(analysis_dir, "analysis-1.json");
		String analysis_bytes = AsJSON(analysis, true);
		SaveFile(analysis_path, analysis_bytes);
		ValueMap foreign_paper = record;
		foreign_paper.Set("id", "foreign-paper");
		ValueMap foreign_envelope = MapGet(foreign_paper, "envelope");
		foreign_envelope.Set("stable_id", "foreign-paper");
		foreign_envelope.Set("workspace_id", "foreign-workspace");
		foreign_paper.Set("envelope", foreign_envelope);
		String foreign_dir = AppendFileName(write_root, "foreign-papers");
		CreatePrivateDirectory(foreign_dir);
		SaveFile(AppendFileName(foreign_dir, "foreign-paper.json"), AsJSON(foreign_paper, true));
		DataSourceRegistration source;
		source.id = "synthetic-write-source";
		source.root = write_root;
		source.read_only = false;
		source.write_enabled = true;
		String qualified = "synthetic-workspace:write-paper";
		DataStore exposed_store;
		if(!exposed_store.LoadFixtureForTests(write_root)) {
			Cerr() << "FAILED: synthetic catalog could not expose writer fixture revisions\n";
			return false;
		}
		const DataDocument* exposed_doc = exposed_store.FindDocument("synthetic-workspace:write-paper");
		const DataDocument* exposed_concurrent = exposed_store.FindDocument("synthetic-workspace:concurrent-paper");
		String source_revision = exposed_doc ? exposed_doc->source_revision : String();
		const DataDocument* exposed_analysis = exposed_store.FindDocument("synthetic-workspace:analysis-1");
		if(!exposed_doc || !exposed_doc->valid || !exposed_concurrent || !exposed_concurrent->valid || !exposed_analysis || !exposed_analysis->valid ||
		   source_revision != DataStore::RecordSourceRevision(exposed_doc->rel_path, original_bytes)) {
			Cerr() << "FAILED: catalog source revisions were not path-qualified canonical fingerprints\n";
			return false;
		}
		DataWriteAuthorization auth;
		auth.authenticated = true;
		auth.action_scope = "record:write";
		auth.principal = "synthetic-operator";
		auth.source_id = source.id;
		auth.workspace_id = "synthetic-workspace";
		if(exposed_store.WriteRecord(auth, qualified, "1", source_revision, "key-denied", record).status != "read_only" ||
		   LoadFile(record_path) != original_bytes) {
			Cerr() << "FAILED: source registration default/read-only denial changed bytes\n";
			return false;
		}
		if(!DataStore::ParseRootRegistrationContent("{\"sources\":[{\"id\":\"default-source\",\"root\":\"" + write_root +
		   "\",\"enabled\":true,\"read_only\":true}]}", source) || source.write_enabled || !source.read_only) {
			Cerr() << "FAILED: source registration parser did not preserve read-only defaults\n";
			return false;
		}
		// Explicit synthetic source capability is retained by the registration parser.
		String registration_json = "{\"sources\":[{\"id\":\"synthetic-write-source\",\"root\":\"" + write_root +
		   "\",\"enabled\":true,\"read_only\":false,\"write_enabled\":true}]}";
		if(!DataStore::ParseRootRegistrationContent(registration_json, source) || source.read_only || !source.write_enabled) {
			Cerr() << "FAILED: explicit synthetic source write capability was not parsed\n";
			return false;
		}
		DataStore writer_store, second_writer_store;
		if(!writer_store.LoadWriteFixtureForTests(write_root, "synthetic-write-source") ||
		   !second_writer_store.LoadWriteFixtureForTests(write_root, "synthetic-write-source")) {
			Cerr() << "FAILED: ManagerSkylark-only synthetic writer setup failed\n";
			return false;
		}
		ValueMap bad_identity = record;
		bad_identity.Set("id", "other-paper");
		String identity_status = writer_store.WriteRecord(auth, qualified, "1", source_revision, "key-bad-id", bad_identity).status;
		String anonymous_status = writer_store.WriteRecord(DataWriteAuthorization(), qualified, "1", source_revision, "key-anon", record).status;
		if(identity_status != "identity_mismatch" || anonymous_status != "invalid_request") {
			Cerr() << "FAILED: writer identity/auth checks returned " << identity_status << "/" << anonymous_status << "\n";
			return false;
		}
		DataWriteAuthorization unscoped = auth;
		unscoped.source_id.Clear();
		if(writer_store.WriteRecord(unscoped, qualified, "1", source_revision, "key-unscoped", record).status != "invalid_request") {
			Cerr() << "FAILED: unscoped authorization was not rejected\n";
			return false;
		}
		DataWriteAuthorization wrong_scope = auth;
		wrong_scope.workspace_id = "other-workspace";
		if(writer_store.WriteRecord(wrong_scope, qualified, "1", source_revision, "key-wrong-scope", record).status != "scope_denied") {
			Cerr() << "FAILED: authorization was not bound to the record workspace\n";
			return false;
		}
		const DataDocument* analysis_doc = exposed_store.FindDocument("synthetic-workspace:analysis-1");
		ValueMap missing_ref = analysis;
		missing_ref.Set("paper", "missing-paper");
		ValueMap wrong_workspace_ref = analysis;
		wrong_workspace_ref.Set("paper", "foreign-paper");
		for(const ValueMap* proposed_analysis : { &missing_ref, &wrong_workspace_ref }) {
			String reference_status = writer_store.WriteRecord(auth, analysis_doc->qualified_id, analysis_doc->revision,
			                                                analysis_doc->source_revision,
			                                                proposed_analysis == &missing_ref ? "key-missing-ref" : "key-wrong-workspace-ref",
			                                                *proposed_analysis).status;
			if(reference_status != "invalid_reference" || LoadFile(analysis_path) != analysis_bytes) {
				Cerr() << "FAILED: missing or cross-workspace reference returned " << reference_status
				       << "; bytes_unchanged=" << (LoadFile(analysis_path) == analysis_bytes) << "\n";
				return false;
			}
		}
		ValueMap invalid_schema = record;
		invalid_schema.Set("status", "not-a-paper-status");
		String invalid_status = writer_store.WriteRecord(auth, qualified, "1", source_revision, "key-invalid", invalid_schema).status;
		if(invalid_status != "invalid_schema") {
			Cerr() << "FAILED: writer trusted-schema validation returned " << invalid_status << "\n";
			return false;
		}
		for(DataWriteTestFailpoint failpoint : { DATA_WRITE_FAIL_TEMP_CREATE, DATA_WRITE_FAIL_TEMP_WRITE, DATA_WRITE_FAIL_REPLACE }) {
			DataWriteResult failed_write = writer_store.WriteRecord(auth, qualified, "1", source_revision,
			                                                   "key-failure-" + AsString((int)failpoint), record, failpoint);
			if((failed_write.status != "temp_write_failed" && failed_write.status != "replacement_failed") ||
			   LoadFile(record_path) != original_bytes) {
				Cerr() << "FAILED: pre-commit writer failure changed original synthetic bytes\n";
				return false;
			}
		}
		ValueMap concurrent_a = concurrent_record;
		concurrent_a.Set("title", "Concurrent writer A");
		ValueMap concurrent_b = concurrent_record;
		concurrent_b.Set("title", "Concurrent writer B");
#ifdef PLATFORM_POSIX
		int start_pipe[2], result_pipe[2];
		if(pipe(start_pipe) != 0 || pipe(result_pipe) != 0) {
			Cerr() << "FAILED: cross-process synthetic writer pipe setup\n";
			return false;
		}
		pid_t child = fork();
		if(child < 0) {
			Cerr() << "FAILED: cross-process synthetic writer fork\n";
			return false;
		}
		if(child == 0) {
			close(start_pipe[1]); close(result_pipe[0]);
			char go;
			if(read(start_pipe[0], &go, 1) != 1) _exit(2);
			DataWriteResult child_result = second_writer_store.WriteRecord(auth, exposed_concurrent->qualified_id,
			                                                            exposed_concurrent->revision,
			                                                            exposed_concurrent->source_revision,
			                                                            "concurrent-child", concurrent_b);
			String child_status_text = child_result.status;
		std::string child_status_std = child_status_text.ToStd();
			const char* text = child_status_std.c_str();
			write(result_pipe[1], text, strlen(text) + 1);
			_exit(0);
		}
		close(start_pipe[0]); close(result_pipe[1]);
		char go = 'x';
		if(write(start_pipe[1], &go, 1) != 1) {
			kill(child, SIGTERM); waitpid(child, NULL, 0);
			Cerr() << "FAILED: cross-process writer start signal\n";
			return false;
		}
		DataWriteResult parent_result = writer_store.WriteRecord(auth, exposed_concurrent->qualified_id,
		                                                       exposed_concurrent->revision,
		                                                       exposed_concurrent->source_revision,
		                                                       "concurrent-parent", concurrent_a);
		char child_status_buf[64] = {};
		ssize_t child_status_len = read(result_pipe[0], child_status_buf, sizeof(child_status_buf) - 1);
		int child_status = 0;
		waitpid(child, &child_status, 0);
		String child_result = child_status_len > 0 ? String(child_status_buf) : String();
		bool parent_committed = parent_result.status == "committed" || parent_result.status == "committed_durability_uncertain";
		bool child_committed = child_result == "committed" || child_result == "committed_durability_uncertain";
		if(!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 ||
		   !((parent_committed && child_result == "conflict") || (child_committed && parent_result.status == "conflict"))) {
			Cerr() << "FAILED: cross-process handles did not serialize to one commit and one conflict ("
			       << parent_result.status << "/" << child_result << ")\n";
			return false;
		}
		String outside_dir = workspace.FixtureRoot("outside-marker-root");
		String linked_dir = AppendFileName(write_root, "linked-papers");
		if(!CreatePrivateDirectory(outside_dir)) {
			Cerr() << "FAILED: isolated symlink-marker fixture creation\n";
			return false;
		}
		ValueMap redirect_record = record;
		redirect_record.Set("id", "redirect-paper");
		ValueMap redirect_envelope = MapGet(redirect_record, "envelope");
		redirect_envelope.Set("stable_id", "redirect-paper");
		redirect_record.Set("envelope", redirect_envelope);
		String outside_record_path = AppendFileName(outside_dir, "redirect-paper.json");
		String outside_bytes = AsJSON(redirect_record, true);
		SaveFile(outside_record_path, outside_bytes);
		if(symlink(outside_dir.ToStd().c_str(), linked_dir.ToStd().c_str()) != 0) {
			Cerr() << "FAILED: could not create synthetic symlink ancestor fixture\n";
			return false;
		}
		DataWriteResult symlink_attempt = writer_store.WriteRecord(auth, "synthetic-workspace:redirect-paper", "1",
		                                                         DataStore::RecordSourceRevision("linked-papers/redirect-paper.json", outside_bytes),
		                                                         "symlink-escape", redirect_record);
		if(symlink_attempt.status == "committed" || symlink_attempt.status == "committed_durability_uncertain" ||
		   LoadFile(outside_record_path) != outside_bytes) {
			Cerr() << "FAILED: symlink-parent write escaped the synthetic source root\n";
			return false;
		}
#endif
		ValueMap changed = record;
		changed.Set("title", "Synthetic updated");
		DataWriteResult committed = writer_store.WriteRecord(auth, qualified, "1", source_revision, "key-success", changed);
		if(committed.status != "committed" || committed.revision != "2" || committed.source_revision.IsEmpty()) {
			Cerr() << "FAILED: synthetic canonical write did not commit revision 2\n";
			return false;
		}
		DataWriteResult retry = writer_store.WriteRecord(auth, qualified, "1", source_revision, "key-success", changed);
		ValueMap altered_retry = changed;
		altered_retry.Set("title", "Different payload");
		DataWriteResult retry_conflict = writer_store.WriteRecord(auth, qualified, "1", source_revision, "key-success", altered_retry);
		DataWriteResult conflict = writer_store.WriteRecord(auth, qualified, "1", source_revision, "key-new", changed);
		String updated_bytes = LoadFile(record_path);
		if(retry.status != "already_committed" || retry.revision != "2" || retry_conflict.status != "idempotency_conflict" || conflict.status != "conflict" ||
		   updated_bytes == original_bytes || DataStore::RecordSourceRevision(exposed_doc->rel_path, updated_bytes) != committed.source_revision) {
			Cerr() << "FAILED: idempotent retry, stale conflict or source revision update\n";
			return false;
		}
		DataStore reloaded_store;
		if(!reloaded_store.LoadFixtureForTests(write_root)) {
			Cerr() << "FAILED: synthetic post-write catalog reload failed\n";
			return false;
		}
		const DataDocument* reloaded_doc = reloaded_store.FindDocument(qualified);
		if(!reloaded_doc || reloaded_doc->revision != committed.revision || reloaded_doc->source_revision != committed.source_revision) {
			Cerr() << "FAILED: writer result revision disagreed with reloaded catalog source revision\n";
			return false;
		}
		String current_bytes = LoadFile(record_path);
		JournalStageRecord stage;
		stage.qualified_id = qualified;
		stage.rel_path = "papers/write-paper.json";
		stage.expected_revision = committed.revision;
		stage.new_revision = "3";
		stage.old_payload = current_bytes;
		ValueMap staged_record = changed;
		ValueMap staged_envelope = MapGet(staged_record, "envelope");
		staged_envelope.Set("revision", "3");
		staged_record.Set("envelope", staged_envelope);
		stage.new_payload = AsJSON(staged_record, true);
		Vector<JournalStageRecord> stages; stages.Add(stage);
		DataStore journal_store;
		if(!journal_store.LoadWriteFixtureForTests(write_root, "synthetic-write-source")) return false;
		JournalStageResult staged = journal_store.PrepareJournal(auth, "synthetic-op", stages);
		String op_dir = AppendFileName(AppendFileName(write_root, ".mgr-journal"), "synthetic-op");
		if(staged.status != "prepared" || staged.operation_id != "synthetic-op" || LoadFile(record_path) != current_bytes ||
		   LoadFile(AppendFileName(op_dir, "manifest.json")).IsEmpty() ||
		   DataStore::ContentDigest(LoadFile(AppendFileName(op_dir, "backup-0.bin"))) != DataStore::ContentDigest(current_bytes) ||
		   LoadFile(AppendFileName(op_dir, "payload-0.bin")) != stage.new_payload) {
			Cerr() << "FAILED: journal prepare was not complete, hashed, or non-mutating\n";
			return false;
		}
		JournalStageResult duplicate_operation = journal_store.PrepareJournal(auth, "synthetic-op", stages);
		if(duplicate_operation.status != "journal_recovery_required" || LoadFile(record_path) != current_bytes ||
		   LoadFile(AppendFileName(op_dir, "manifest.json")).IsEmpty()) {
			Cerr() << "FAILED: duplicate operation ID started a second journal operation\n";
			return false;
		}
		JournalManifest staged_manifest;
		if(!DataStore::ParseJournalManifest(LoadFile(AppendFileName(op_dir, "manifest.json")), staged_manifest) ||
		   staged_manifest.workspace_id != auth.workspace_id || staged_manifest.records.GetCount() != 1 || staged_manifest.records[0].old_hash != DataStore::ContentDigest(current_bytes) ||
		   staged_manifest.records[0].new_hash != DataStore::ContentDigest(stage.new_payload)) {
			Cerr() << "FAILED: staged journal manifest hashes or schema\n";
			return false;
		}
	#ifdef PLATFORM_POSIX
		struct stat journal_stat, operation_stat;
		if(stat(AppendFileName(write_root, ".mgr-journal").ToStd().c_str(), &journal_stat) != 0 || (journal_stat.st_mode & 077) != 0 ||
		   stat(op_dir.ToStd().c_str(), &operation_stat) != 0 || (operation_stat.st_mode & 077) != 0) {
			Cerr() << "FAILED: journal staging permissions are not owner-only\n";
			return false;
		}
	#endif
		DataWriteAuthorization wrong_recovery_scope = auth;
		wrong_recovery_scope.workspace_id = "other-workspace";
		DataStore read_only_recovery;
		JournalRecoveryResult denied_recovery = read_only_recovery.RecoverPendingBatches(auth);
		JournalRecoveryResult scope_denied_recovery = journal_store.RecoverPendingBatches(wrong_recovery_scope);
		if(denied_recovery.status != "invalid_request" || scope_denied_recovery.status != "scope_denied" ||
		   LoadFile(record_path) != current_bytes || !FileExists(AppendFileName(op_dir, "manifest.json"))) {
			Cerr() << "FAILED: recovery allowed a read-only or wrong-workspace caller\n";
			return false;
		}
		JournalRecoveryResult interrupted_recovery = journal_store.RecoverPendingBatches(auth, DATA_WRITE_FAIL_RECOVERY_BEFORE_CLEANUP);
		if(interrupted_recovery.status != "recovery_conflict" || LoadFile(record_path) != current_bytes ||
		   !FileExists(AppendFileName(op_dir, "manifest.json"))) {
			Cerr() << "FAILED: interrupted prepared recovery changed canonical bytes or cleared pending state\n";
			return false;
		}
		JournalRecoveryResult prepared_recovery = journal_store.RecoverPendingBatches(auth);
		DataStore recovered_catalog;
		if(prepared_recovery.status != "recovered" || LoadFile(record_path) != current_bytes ||
		   DirectoryExists(AppendFileName(write_root, ".mgr-journal")) || !recovered_catalog.LoadFixtureForTests(write_root)) {
			Cerr() << "FAILED: prepared batch was not safely discarded and made readable (status=" << prepared_recovery.status
			       << ", bytes=" << (LoadFile(record_path) == current_bytes) << ", journal="
			       << DirectoryExists(AppendFileName(write_root, ".mgr-journal")) << ", reload="
			       << recovered_catalog.GetLoadStatus() << ")\n";
			return false;
		}
		if(journal_store.RecoverPendingBatches(auth).status != "clean") {
			Cerr() << "FAILED: already recovered retry was not idempotently clean\n";
			return false;
		}
		Cout() << "ok    T45 prepared discard, explicit auth/read-only denial, recovery failpoint and idempotent retry\n";
		auto exercise_recovery_state = [&](const String& name, const String& state, bool preapply_new,
		                                  bool inject_before_cleanup, bool restart_recovery) {
			String root = workspace.FixtureRoot(name), papers = AppendFileName(root, "papers");
			if(!CreatePrivateDirectory(root) || !CreatePrivateDirectory(papers)) return false;
			String path = AppendFileName(papers, "write-paper.json");
			if(!SaveFile(path, current_bytes)) return false;
			DataStore store;
			if(!store.LoadWriteFixtureForTests(root, "synthetic-write-source")) return false;
			JournalStageResult prepared = store.PrepareJournal(auth, name + "-op", stages);
			String op = AppendFileName(AppendFileName(root, ".mgr-journal"), name + "-op");
			Value manifest_value = ParseJSON(LoadFile(AppendFileName(op, "manifest.json")));
			ValueMap manifest_map = manifest_value;
			manifest_map.Set("state", state);
			if(state != "prepared") { String digest; for(int i = 0; i < 64; i++) digest.Cat('a'); manifest_map.Set("request_digest", digest); }
			if(prepared.status != "prepared" || !SaveFile(AppendFileName(op, "manifest.json"), AsJSON(manifest_map, true))) return false;
			if(preapply_new && !SaveFile(path, stage.new_payload)) return false;
			if(state != "prepared") {
				JournalManifest check_manifest;
				if(!DataStore::ParseJournalManifest(LoadFile(AppendFileName(op, "manifest.json")), check_manifest)) {
					Cerr() << "synthetic recovery manifest rejected: " << name << "\n";
					return false;
				}
			}
			DataStore restarted_store;
			DataStore *recovery_store = &store;
			if(restart_recovery) {
				if(restarted_store.LoadWriteFixtureForTests(root, "synthetic-write-source") ||
				   !restarted_store.GetDocuments().IsEmpty() || restarted_store.GetSourceDiagnostics().IsEmpty() ||
				   restarted_store.GetSourceDiagnostics()[0] != "journal:recovery_required") return false;
				recovery_store = &restarted_store;
			}
			DataWriteTestFailpoint fail = inject_before_cleanup ? DATA_WRITE_FAIL_RECOVERY_BEFORE_CLEANUP : DATA_WRITE_FAIL_NONE;
			JournalRecoveryResult recovery = recovery_store->RecoverPendingBatches(auth, fail);
			if(inject_before_cleanup) {
				JournalManifest persisted_state;
				if(recovery.status != "recovery_conflict" || LoadFile(path) != current_bytes ||
				   !DataStore::ParseJournalManifest(LoadFile(AppendFileName(op, "manifest.json")), persisted_state) ||
				   persisted_state.state != "rolled-back") return false;
				recovery = recovery_store->RecoverPendingBatches(auth);
			}
			String expected = state == "committed" ? stage.new_payload : current_bytes;
			bool ok = recovery.status == "recovered" && LoadFile(path) == expected && !DirectoryExists(AppendFileName(root, ".mgr-journal"));
			if(!ok) Cerr() << "recovery state diagnostic: " << name << " status=" << recovery.status << " content_match=" << (LoadFile(path) == expected) << " journal=" << DirectoryExists(AppendFileName(root, ".mgr-journal")) << "\n";
			return ok;
		};
		if(!exercise_recovery_state("recovery-committed", "committed", false, false, true) ||
		   !exercise_recovery_state("recovery-committing", "committing", true, true, true)) {
			Cerr() << "FAILED: committed replay or committing partial rollback/retry\n";
			return false;
		}
		String unknown_root = workspace.FixtureRoot("recovery-unknown"), unknown_papers = AppendFileName(unknown_root, "papers");
		if(!CreatePrivateDirectory(unknown_root) || !CreatePrivateDirectory(unknown_papers)) return false;
		String unknown_path = AppendFileName(unknown_papers, "write-paper.json");
		SaveFile(unknown_path, current_bytes);
		DataStore unknown_store;
		if(!unknown_store.LoadWriteFixtureForTests(unknown_root, "synthetic-write-source") ||
		   unknown_store.PrepareJournal(auth, "unknown-op", stages).status != "prepared") return false;
		String unknown_op = AppendFileName(AppendFileName(unknown_root, ".mgr-journal"), "unknown-op");
		Value unknown_manifest = ParseJSON(LoadFile(AppendFileName(unknown_op, "manifest.json")));
		ValueMap unknown_map = unknown_manifest; unknown_map.Set("state", "committed");
		String unknown_digest; for(int i = 0; i < 64; i++) unknown_digest.Cat('a');
		unknown_map.Set("request_digest", unknown_digest);
		SaveFile(AppendFileName(unknown_op, "manifest.json"), AsJSON(unknown_map, true));
		String unknown_bytes = "unrecognized conflicting bytes";
		SaveFile(unknown_path, unknown_bytes);
		JournalRecoveryResult unknown_result = unknown_store.RecoverPendingBatches(auth);
		if(unknown_result.status != "recovery_conflict" || LoadFile(unknown_path) != unknown_bytes ||
		   !FileExists(AppendFileName(unknown_op, "manifest.json"))) {
			Cerr() << "FAILED: unknown canonical bytes were overwritten or pending state cleared\n";
			return false;
		}
		Cout() << "ok    T45 fresh-store restart recovery: committing rollback/retry, committed replay, and unknown-byte conflict\n";
		// T56 create-aware journal coverage; fixtures are synthetic and isolated.
		{
			auto create_payload = [&](const String& workspace_id) {
				ValueMap created = record; created.Set("id", "new-journal-paper");
				ValueMap env = MapGet(created, "envelope"); env.Set("stable_id", "new-journal-paper"); env.Set("revision", "1"); env.Set("workspace_id", workspace_id);
				created.Set("envelope", env); return AsJSON(created, true);
			};
			auto create_target = [&](const String& root, const String& workspace_id) {
				return AppendFileName(AppendFileName(root, "records"), DataStore::ContentDigest(workspace_id + ":new-journal-paper") + ".json");
			};
			auto prepare_create = [&](const String& name, String& root, String& operation, String& payload, const String& workspace_id) {
				root = workspace.FixtureRoot(name);
				if(!CreatePrivateDirectory(root) || !CreatePrivateDirectory(AppendFileName(root, "records"))) return false;
				DataStore store; if(!store.LoadWriteFixtureForTests(root, "synthetic-write-source")) return false;
				JournalStageRecord create; create.action = "create"; create.qualified_id = workspace_id + ":new-journal-paper";
				create.rel_path = "caller-path-is-ignored.json"; create.new_revision = "1"; payload = create.new_payload = create_payload(workspace_id);
				Vector<JournalStageRecord> create_stages; create_stages.Add(create);
				String op_id = name + "-op";
				DataWriteAuthorization scoped_auth = auth; scoped_auth.workspace_id = workspace_id;
				JournalStageResult staged_create = store.PrepareJournal(scoped_auth, op_id, create_stages);
				if(staged_create.status != "prepared") { Cerr() << "T56 create stage status=" << staged_create.status << "\n"; return false; }
				operation = AppendFileName(AppendFileName(root, ".mgr-journal"), op_id);
				JournalManifest parsed_create;
				return DataStore::ParseJournalManifest(LoadFile(AppendFileName(operation, "manifest.json")), parsed_create) &&
				       parsed_create.version == 2 && parsed_create.records.GetCount() == 1 &&
			       parsed_create.records[0].action == "create" && parsed_create.records[0].rel_path == "records/" + DataStore::ContentDigest(create.qualified_id) + ".json" &&
				       parsed_create.records[0].old_hash.IsEmpty();
			};
			String prepared_root, prepared_op, create_bytes;
			if(!prepare_create("journal-create-prepared", prepared_root, prepared_op, create_bytes, "synthetic-workspace")) {
				Cerr() << "FAILED: T56 create manifest staging/path derivation\n"; return false;
			}
			String created_path = create_target(prepared_root, "synthetic-workspace");
			DataStore prepared_store; prepared_store.LoadWriteFixtureForTests(prepared_root, "synthetic-write-source");
			if(prepared_store.RecoverPendingBatches(auth).status != "recovered" || FileExists(created_path)) {
				Cerr() << "FAILED: T56 prepared create discard\n"; return false;
			}
			auto set_create_state = [&](const String& op, const String& state) {
				ValueMap m = ParseJSON(LoadFile(AppendFileName(op, "manifest.json")));
				m.Set("state", state); if(state != "prepared") { String digest; for(int i = 0; i < 64; i++) digest.Cat('a'); m.Set("request_digest", digest); }
				return SaveFile(AppendFileName(op, "manifest.json"), AsJSON(m, true));
			};
			String workspace_two_root, workspace_two_op, workspace_two_payload;
			if(!prepare_create("journal-create-workspace-two", workspace_two_root, workspace_two_op, workspace_two_payload, "other-synthetic-workspace")) return false;
			JournalManifest workspace_two_manifest;
			if(!DataStore::ParseJournalManifest(LoadFile(AppendFileName(workspace_two_op, "manifest.json")), workspace_two_manifest) ||
			   workspace_two_manifest.records[0].rel_path == "records/" + DataStore::ContentDigest("synthetic-workspace:new-journal-paper") + ".json") {
				Cerr() << "FAILED: T56 workspace-qualified create destinations collided\n"; return false;
			}
			{
				String duplicate_root = workspace.FixtureRoot("journal-create-duplicate");
				if(!CreatePrivateDirectory(duplicate_root) || !CreatePrivateDirectory(AppendFileName(duplicate_root, "papers")) ||
				   !SaveFile(AppendFileName(AppendFileName(duplicate_root, "papers"), "same-id-different-path.json"), create_payload("synthetic-workspace"))) return false;
				DataStore duplicate_store; if(!duplicate_store.LoadWriteFixtureForTests(duplicate_root, "synthetic-write-source")) return false;
				JournalStageRecord duplicate; duplicate.action = "create"; duplicate.qualified_id = "synthetic-workspace:new-journal-paper";
				duplicate.new_revision = "1"; duplicate.new_payload = create_payload("synthetic-workspace");
				Vector<JournalStageRecord> duplicate_stages; duplicate_stages.Add(duplicate); duplicate_stages.Add(duplicate);
				if(duplicate_store.PrepareJournal(auth, "duplicate-create-op", duplicate_stages).status != "identity_conflict") {
					Cerr() << "FAILED: T56 indexed duplicate identity at another path was staged\n"; return false;
				}
			}
			{
				String revision_root = workspace.FixtureRoot("journal-create-bad-revision");
				if(!CreatePrivateDirectory(revision_root) || !CreatePrivateDirectory(AppendFileName(revision_root, "records"))) return false;
				DataStore revision_store; if(!revision_store.LoadWriteFixtureForTests(revision_root, "synthetic-write-source")) return false;
				ValueMap invalid_first = ParseJSON(create_payload("synthetic-workspace"));
				ValueMap invalid_env = MapGet(invalid_first, "envelope"); invalid_env.Set("revision", "2"); invalid_first.Set("envelope", invalid_env);
				JournalStageRecord invalid_stage; invalid_stage.action = "create"; invalid_stage.qualified_id = "synthetic-workspace:new-journal-paper";
				invalid_stage.new_revision = "2"; invalid_stage.new_payload = AsJSON(invalid_first, true);
				Vector<JournalStageRecord> invalid_stages; invalid_stages.Add(invalid_stage);
				if(revision_store.PrepareJournal(auth, "bad-initial-revision-op", invalid_stages).status != "identity_or_schema_invalid") {
					Cerr() << "FAILED: T56 create accepted an initial revision other than 1\n"; return false;
				}
			}
			{
				String depth_root = workspace.FixtureRoot("journal-create-scan-depth");
				if(!CreatePrivateDirectory(depth_root) || !CreatePrivateDirectory(AppendFileName(depth_root, "records"))) return false;
				String deep = depth_root;
				for(int i = 0; i <= DataStore::MAX_DEPTH; i++) {
					deep = AppendFileName(deep, "nested-" + AsString(i));
					if(!CreatePrivateDirectory(deep)) return false;
				}
				DataStore depth_store; if(!depth_store.LoadWriteFixtureForTests(depth_root, "synthetic-write-source")) return false;
				JournalStageRecord depth_create; depth_create.action = "create";
				depth_create.qualified_id = "synthetic-workspace:new-journal-paper"; depth_create.new_revision = "1";
				depth_create.new_payload = create_payload("synthetic-workspace");
				Vector<JournalStageRecord> depth_stages; depth_stages.Add(depth_create);
				if(depth_store.PrepareJournal(auth, "depth-scan-create-op", depth_stages).status != "identity_scan_unavailable") {
					Cerr() << "FAILED: T56 over-depth identity scan did not fail closed\n"; return false;
				}
			}
			{
				String malformed_root, malformed_op, malformed_payload;
				if(!prepare_create("journal-create-malformed", malformed_root, malformed_op, malformed_payload, "synthetic-workspace")) return false;
				String malformed_path = AppendFileName(malformed_op, "manifest.json");
				ValueMap malformed = ParseJSON(LoadFile(malformed_path)); malformed.Set("unexpected", true);
				if(!SaveFile(malformed_path, AsJSON(malformed, true))) return false;
				DataStore malformed_store; if(malformed_store.LoadWriteFixtureForTests(malformed_root, "synthetic-write-source")) return false;
				if(malformed_store.RecoverPendingBatches(auth).status != "journal_orphan_unrecognized" || FileExists(create_target(malformed_root, "synthetic-workspace"))) {
					Cerr() << "FAILED: T56 malformed create manifest was not preserved fail-closed\n"; return false;
				}
			}
			{
				String manifest_bytes = LoadFile(AppendFileName(workspace_two_op, "manifest.json"));
				int value_at = manifest_bytes.Find(':', manifest_bytes.Find("\"version\"") + 9) + 1;
				while(value_at < manifest_bytes.GetCount() && IsSpace(manifest_bytes[value_at])) value_at++;
				String fractional = manifest_bytes.Mid(0, value_at) + "2.5" + manifest_bytes.Mid(value_at + 1);
				Value root_value = ParseJSON(manifest_bytes); ValueMap root_map = root_value; root_map.Set("unknown_root", true);
				ValueArray rows = root_map.Get("records"); ValueMap row = rows[0]; row.Set("unknown_record", true); rows.Set(0, row); root_map.Set("records", rows);
				JournalManifest ignored;
				ValueMap unknown_root = ParseJSON(manifest_bytes); unknown_root.Set("unknown_root", true);
				bool fractional_accepted = DataStore::ParseJournalManifest(fractional, ignored);
				bool root_field_accepted = DataStore::ParseJournalManifest(AsJSON(unknown_root, true), ignored);
				bool record_field_accepted = DataStore::ParseJournalManifest(AsJSON(root_map, true), ignored);
				if(fractional_accepted || root_field_accepted || record_field_accepted) {
					Cerr() << "FAILED: T56 fractional version or unknown manifest fields accepted " << fractional_accepted << "/" << root_field_accepted << "/" << record_field_accepted << "\n"; return false;
				}
				String legacy_update = "{\"version\":1,\"operation_id\":\"legacy-op\",\"source_id\":\"synthetic\",\"workspace_id\":\"synthetic-workspace\",\"state\":\"prepared\",\"records\":[{\"source_id\":\"synthetic\",\"record_id\":\"legacy-id\",\"path\":\"papers/legacy-\\\"version\\\":2.json\",\"expected_revision\":\"1\",\"new_revision\":\"2\",\"backup_ref\":\"backup-0.bin\",\"payload_ref\":\"payload-0.bin\",\"old_hash\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"new_hash\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"state\":\"pending\"}]}";
				ValueMap legacy_root = ParseJSON(legacy_update); legacy_root.Set("unknown", true);
				Value legacy_value = ParseJSON(legacy_update); ValueMap legacy_map = legacy_value;
				ValueArray legacy_rows = legacy_map.Get("records"); ValueMap legacy_row = legacy_rows[0]; legacy_row.Set("unknown", true); legacy_rows.Set(0, legacy_row); legacy_map.Set("records", legacy_rows);
				String legacy_fractional = legacy_update; legacy_fractional.Replace("\"version\":1", "\"version\":1.0");
				String legacy_two_fractional = legacy_update; legacy_two_fractional.Replace("\"version\":1", "\"version\":2.5");
				bool legacy_ok = DataStore::ParseJournalManifest(legacy_update, ignored);
				String parsed_legacy_id = legacy_ok && ignored.records.GetCount() ? ignored.records[0].record_id : String();
				bool legacy_decimal_one = DataStore::ParseJournalManifest(legacy_fractional, ignored);
				bool legacy_decimal_two = DataStore::ParseJournalManifest(legacy_two_fractional, ignored);
				bool legacy_extra_root = DataStore::ParseJournalManifest(AsJSON(legacy_root, true), ignored);
				bool legacy_extra_record = DataStore::ParseJournalManifest(AsJSON(legacy_map, true), ignored);
				if(!legacy_ok || parsed_legacy_id != "legacy-id" || ignored.version != 1 || legacy_decimal_one || legacy_decimal_two ||
				   legacy_extra_root || legacy_extra_record) {
					Cerr() << "FAILED: T56 exact version-1 field set or integer version regression\n"; return false;
				}
			}
			String rollback_root, rollback_op, rollback_payload;
			if(!prepare_create("journal-create-rollback", rollback_root, rollback_op, rollback_payload, "synthetic-workspace") ||
			   !set_create_state(rollback_op, "committing") ||
			   !SaveFile(create_target(rollback_root, "synthetic-workspace"), rollback_payload)) {
				Cerr() << "FAILED: T56 interrupted create fixture\n"; return false;
			}
			DataStore fresh_rollback; if(fresh_rollback.LoadWriteFixtureForTests(rollback_root, "synthetic-write-source")) return false;
			String rollback_path = create_target(rollback_root, "synthetic-workspace");
			JournalManifest rollback_check;
			if(!DataStore::ParseJournalManifest(LoadFile(AppendFileName(rollback_op, "manifest.json")), rollback_check)) {
				Cerr() << "T56 committing create manifest failed to parse after state update\n"; return false;
			}
			JournalRecoveryResult rollback_result = fresh_rollback.RecoverPendingBatches(auth);
			JournalRecoveryResult rollback_retry = fresh_rollback.RecoverPendingBatches(auth);
			if(rollback_result.status != "recovered" || FileExists(rollback_path) || rollback_retry.status != "clean") {
				Cerr() << "FAILED: T56 fresh-store create rollback/retry status=" << rollback_result.status << "/" << rollback_retry.status << " exists=" << FileExists(rollback_path) << "\n"; return false;
			}
			String rolled_root, rolled_op, rolled_payload;
			if(!prepare_create("journal-create-rolled-back", rolled_root, rolled_op, rolled_payload, "synthetic-workspace") ||
			   !set_create_state(rolled_op, "rolled-back")) return false;
			DataStore rolled_store; if(rolled_store.LoadWriteFixtureForTests(rolled_root, "synthetic-write-source")) return false;
			if(rolled_store.RecoverPendingBatches(auth).status != "recovered" || FileExists(create_target(rolled_root, "synthetic-workspace"))) {
				Cerr() << "FAILED: T56 explicit rolled-back create state\n"; return false;
			}
			String replay_root, replay_op, replay_payload;
			if(!prepare_create("journal-create-replay", replay_root, replay_op, replay_payload, "synthetic-workspace") ||
			   !set_create_state(replay_op, "committed")) return false;
			String replay_path = create_target(replay_root, "synthetic-workspace");
			DataStore fresh_replay; if(fresh_replay.LoadWriteFixtureForTests(replay_root, "synthetic-write-source")) return false;
			if(fresh_replay.RecoverPendingBatches(auth, DATA_WRITE_FAIL_RECOVERY_BEFORE_RECORD).status != "recovery_conflict" || FileExists(replay_path) ||
			   fresh_replay.RecoverPendingBatches(auth, DATA_WRITE_FAIL_RECOVERY_AFTER_RECORD).status != "recovery_conflict" ||
			   LoadFile(replay_path) != replay_payload || fresh_replay.RecoverPendingBatches(auth).status != "recovered" || LoadFile(replay_path) != replay_payload) {
				Cerr() << "FAILED: T56 fresh-store committed create replay\n"; return false;
			}
			String conflict_root, conflict_op, conflict_payload;
			if(!prepare_create("journal-create-conflict", conflict_root, conflict_op, conflict_payload, "synthetic-workspace") ||
			   !set_create_state(conflict_op, "committed")) return false;
			String conflict_path = create_target(conflict_root, "synthetic-workspace");
			String unknown = "synthetic unknown bytes"; if(!SaveFile(conflict_path, unknown)) return false;
			DataStore conflict_store; if(conflict_store.LoadWriteFixtureForTests(conflict_root, "synthetic-write-source")) return false;
			if(conflict_store.RecoverPendingBatches(auth).status != "recovery_conflict" || LoadFile(conflict_path) != unknown ||
			   !FileExists(AppendFileName(conflict_op, "manifest.json"))) {
				Cerr() << "FAILED: T56 unknown-byte create conflict\n"; return false;
			}
			{
				String collision_root, collision_op, collision_payload;
				if(!prepare_create("journal-create-identity-collision", collision_root, collision_op, collision_payload, "synthetic-workspace") ||
				   !set_create_state(collision_op, "committed") ||
				   !CreatePrivateDirectory(AppendFileName(collision_root, "papers")) ||
				   !SaveFile(AppendFileName(AppendFileName(collision_root, "papers"), "duplicate-identity.json"), collision_payload)) return false;
				DataStore collision_store; if(collision_store.LoadWriteFixtureForTests(collision_root, "synthetic-write-source")) return false;
				if(collision_store.RecoverPendingBatches(auth).status != "recovery_conflict" || FileExists(create_target(collision_root, "synthetic-workspace"))) {
					Cerr() << "FAILED: T56 fresh catalog identity/path collision was replayed\n"; return false;
				}
			}
			{
				String symlink_root, symlink_op, symlink_payload, outside = workspace.FixtureRoot("journal-create-symlink-target");
				if(!prepare_create("journal-create-symlink", symlink_root, symlink_op, symlink_payload, "synthetic-workspace") ||
				   !set_create_state(symlink_op, "committed") || !SaveFile(outside, "outside synthetic bytes") ||
				   symlink(outside.ToStd().c_str(), create_target(symlink_root, "synthetic-workspace").ToStd().c_str()) != 0) return false;
				DataStore symlink_store; if(symlink_store.LoadWriteFixtureForTests(symlink_root, "synthetic-write-source")) return false;
				if(symlink_store.RecoverPendingBatches(auth).status != "recovery_conflict" || LoadFile(outside) != "outside synthetic bytes") {
					Cerr() << "FAILED: T56 create target symlink was followed or overwritten\n"; return false;
				}
			}
			Cout() << "ok    T56 generic workspace-qualified creates, bounded identity checks, initial revision, rollback/replay and conflict cases\n";
		}
		// T55 high-level canonical creation coverage. This block uses only isolated
		// synthetic roots and the existing friend-authorized test capability.
		{
			auto make_paper = [&](const String& id, const String& workspace_id) {
				ValueMap value; value.Add("id", id); value.Add("kind", "paper"); value.Add("title", "Synthetic created paper");
				value.Add("source", "synthetic"); value.Add("status", "draft");
				ValueMap envelope = record_envelope("paper", "paper", id); envelope.Set("workspace_id", workspace_id);
				value.Add("envelope", envelope);
				return value;
			};
			auto make_analysis = [&](const String& id, const String& paper_id, const String& workspace_id) {
				ValueMap value; value.Add("id", id); value.Add("kind", "paper-analysis"); value.Add("paper", paper_id);
				value.Add("repo", "synthetic"); value.Add("version", "v1"); value.Add("tasks", ValueArray()); value.Add("findings", ValueArray());
				ValueMap envelope = record_envelope("paper-analysis", "paper-analysis", id); envelope.Set("workspace_id", workspace_id);
				value.Add("envelope", envelope);
				return value;
			};
			auto make_create = [&](const String& id, const ValueMap& value) {
				DataBatchChange change; change.action = DATA_BATCH_CREATE; change.qualified_id = auth.workspace_id + ":" + id; change.proposed = value; return change;
			};
			String create_root = workspace.FixtureRoot("t55-publish-create");
			if(!CreatePrivateDirectory(create_root)) return false;
			DataStore create_store;
			if(!create_store.LoadWriteFixtureForTests(create_root, "synthetic-write-source")) return false;
			ValueMap created_paper = make_paper("t55-paper", auth.workspace_id);
			ValueMap created_analysis = make_analysis("t55-analysis", "t55-paper", auth.workspace_id);
			Vector<DataBatchChange> create_changes;
			create_changes.Add(make_create("t55-paper", created_paper));
			create_changes.Add(make_create("t55-analysis", created_analysis));
			DataWriteAuthorization no_auth;
			DataBatchResult denied = create_store.PublishBatch(no_auth, "t55-denied", create_changes);
			DataStore readonly_store; readonly_store.LoadFixtureForTests(create_root);
			DataBatchResult readonly_denied = readonly_store.PublishBatch(auth, "t55-readonly", create_changes);
			DataWriteAuthorization wrong_workspace = auth; wrong_workspace.workspace_id = "other-workspace";
			DataBatchResult scope_denied = create_store.PublishBatch(wrong_workspace, "t55-wrong-scope", create_changes);
			DataWriteAuthorization wrong_source = auth; wrong_source.source_id = "other-source";
			DataBatchResult source_denied = create_store.PublishBatch(wrong_source, "t55-wrong-source", create_changes);
			DataBatchChange implicit_update; implicit_update.qualified_id = auth.workspace_id + ":implicit-update";
			implicit_update.proposed = make_paper("implicit-update", auth.workspace_id);
			DataBatchResult implicit_update_denied = create_store.PublishBatch(auth, "t55-implicit-update", Vector<DataBatchChange>{ implicit_update });
			DataBatchChange invalid_action = make_create("t55-invalid-action", make_paper("t55-invalid-action", auth.workspace_id));
			invalid_action.action = (DataBatchAction)99;
			Vector<DataBatchChange> invalid_action_changes; invalid_action_changes.Add(invalid_action);
			DataBatchResult invalid_action_denied = create_store.PublishBatch(auth, "t55-invalid-action-op", invalid_action_changes);
			if(denied.status != "invalid_request" || readonly_denied.status != "invalid_request" ||
			   scope_denied.status != "identity_conflict" || source_denied.status != "invalid_request" ||
			   implicit_update_denied.status != "invalid_request" || invalid_action_denied.status != "invalid_request" ||
			   DirectoryExists(AppendFileName(create_root, "records")) ||
			   DirectoryExists(AppendFileName(create_root, ".mgr-commits")) || DirectoryExists(AppendFileName(create_root, ".mgr-journal"))) {
				Cerr() << "FAILED: T55 create authorization/read-only/scope or implicit-update denial status=" << denied.status << "/" << readonly_denied.status << "/" << scope_denied.status << "/" << source_denied.status << "/" << implicit_update_denied.status << "\n";
				return false;
			}
			String incomplete_root = workspace.FixtureRoot("t55-incomplete-source");
			if(!CreatePrivateDirectory(incomplete_root)) return false;
			DataStore incomplete_store; if(!incomplete_store.LoadWriteFixtureForTests(incomplete_root, "synthetic-write-source")) return false;
			String malformed_dir = AppendFileName(incomplete_root, "papers");
			if(!CreatePrivateDirectory(malformed_dir) || !SaveFile(AppendFileName(malformed_dir, "late-malformed.json"), "{ malformed synthetic input")) return false;
			Vector<DataBatchChange> incomplete_changes; incomplete_changes.Add(make_create("t55-incomplete", make_paper("t55-incomplete", auth.workspace_id)));
			if(incomplete_store.PublishBatch(auth, "t55-incomplete-op", incomplete_changes).status != "source_incomplete" ||
			   DirectoryExists(AppendFileName(incomplete_root, "records")) || DirectoryExists(AppendFileName(incomplete_root, ".mgr-commits")) ||
			   DirectoryExists(AppendFileName(incomplete_root, ".mgr-journal"))) {
				Cerr() << "FAILED: T55 fresh source scan did not reject a newly incomplete source before artifacts\n"; return false;
			}
			for(DataWriteTestFailpoint failpoint : { DATA_WRITE_FAIL_BATCH_TEST_REVOKE_BEFORE_RECEIPT,
			                                          DATA_WRITE_FAIL_BATCH_TEST_REDIRECT_BEFORE_RECEIPT }) {
				String revoked_root = workspace.FixtureRoot("t55-registration-before-receipt-" + AsString((int)failpoint));
				if(!CreatePrivateDirectory(revoked_root)) return false;
				DataStore revoked_store; if(!revoked_store.LoadWriteFixtureForTests(revoked_root, "synthetic-write-source")) return false;
				if(revoked_store.PublishBatch(auth, "t55-registration-denied", create_changes, failpoint).status != "source_capability_changed" ||
				   DirectoryExists(AppendFileName(revoked_root, "records")) || DirectoryExists(AppendFileName(revoked_root, ".mgr-commits")) ||
				   DirectoryExists(AppendFileName(revoked_root, ".mgr-journal"))) {
					Cerr() << "FAILED: T55 revoked/redirected registration reached create receipt or artifact publication\n"; return false;
				}
			}
			String revoked_commit_root = workspace.FixtureRoot("t55-registration-before-commit");
			if(!CreatePrivateDirectory(revoked_commit_root)) return false;
			DataStore revoked_commit_store; if(!revoked_commit_store.LoadWriteFixtureForTests(revoked_commit_root, "synthetic-write-source")) return false;
			Vector<DataBatchChange> revoked_commit_changes; revoked_commit_changes.Add(make_create("t55-revoked-commit", make_paper("t55-revoked-commit", auth.workspace_id)));
			DataBatchResult revoked_commit = revoked_commit_store.PublishBatch(auth, "t55-revoked-commit-op", revoked_commit_changes, DATA_WRITE_FAIL_BATCH_TEST_REGISTRATION_CHANGE);
			String revoked_target = AppendFileName(revoked_commit_root, "records/" + DataStore::ContentDigest(auth.workspace_id + ":t55-revoked-commit") + ".json");
			if(revoked_commit.status != "source_capability_changed_recovery_required" || FileExists(revoked_target) ||
			   !DirectoryExists(AppendFileName(revoked_commit_root, ".mgr-journal")) ||
			   revoked_commit_store.RecoverPendingBatches(auth).status != "recovered") {
				Cerr() << "FAILED: T55 registration redirect at commit boundary did not preserve absent create and recoverable journal\n"; return false;
			}
		#ifdef PLATFORM_POSIX
			String symlink_root = workspace.FixtureRoot("t55-records-symlink");
			String symlink_target = workspace.FixtureRoot("t55-records-symlink-target");
			if(!CreatePrivateDirectory(symlink_root) || !CreatePrivateDirectory(symlink_target)) return false;
			String marker_path = AppendFileName(symlink_target, "marker.txt");
			String records_link = AppendFileName(symlink_root, "records");
			String symlink_id = auth.workspace_id + ":t55-symlink-records";
			String symlink_hash_path = AppendFileName(symlink_target, DataStore::ContentDigest(symlink_id) + ".json");
			if(!SaveFile(marker_path, "untouched synthetic symlink target") ||
			   symlink(symlink_target.ToStd().c_str(), records_link.ToStd().c_str()) != 0) return false;
			DataStore symlink_records_store;
			if(!symlink_records_store.LoadWriteFixtureForTests(symlink_root, "synthetic-write-source")) return false;
			Vector<DataBatchChange> symlink_records_changes;
			symlink_records_changes.Add(make_create("t55-symlink-records", make_paper("t55-symlink-records", auth.workspace_id)));
			DataBatchResult symlink_records_result = symlink_records_store.PublishBatch(auth, "t55-symlink-records-op", symlink_records_changes);
			FindFile records_link_stat(records_link);
			if(symlink_records_result.status == "committed" || !records_link_stat || !records_link_stat.IsSymLink() ||
			   FileExists(symlink_hash_path) || LoadFile(marker_path) != "untouched synthetic symlink target") {
				Cerr() << "FAILED: T55 records symlink was followed or target changed (status=" << symlink_records_result.status << ")\n";
				return false;
			}
			String permissive_root = workspace.FixtureRoot("t55-records-permissive");
			if(!CreatePrivateDirectory(permissive_root)) return false;
			String permissive_records = AppendFileName(permissive_root, "records");
			if(!CreatePrivateDirectory(permissive_records) || chmod(permissive_records.ToStd().c_str(), 0777) != 0) return false;
			DataStore permissive_records_store;
			if(!permissive_records_store.LoadWriteFixtureForTests(permissive_root, "synthetic-write-source")) {
				chmod(permissive_records.ToStd().c_str(), 0700);
				return false;
			}
			Vector<DataBatchChange> permissive_records_changes;
			permissive_records_changes.Add(make_create("t55-permissive-records", make_paper("t55-permissive-records", auth.workspace_id)));
			DataBatchResult permissive_records_result = permissive_records_store.PublishBatch(auth, "t55-permissive-records-op", permissive_records_changes);
			String permissive_hash_path = AppendFileName(permissive_records, DataStore::ContentDigest(auth.workspace_id + ":t55-permissive-records") + ".json");
			bool permissive_denied = permissive_records_result.status != "committed" && !FileExists(permissive_hash_path);
			bool permissions_restored = chmod(permissive_records.ToStd().c_str(), 0700) == 0;
			if(!permissive_denied || !permissions_restored) {
				Cerr() << "FAILED: T55 permissive records directory accepted creation or was not restored (status=" << permissive_records_result.status << ")\n";
				return false;
			}
			Cout() << "ok    T55 rejects symlinked and group/world-permissive records directories without creating canonical files\n";
		#endif
			ValueMap invalid = make_paper("t55-invalid", auth.workspace_id); invalid.Set("status", "unknown");
			Vector<DataBatchChange> invalid_changes; invalid_changes.Add(make_create("t55-invalid", invalid));
			Vector<DataBatchChange> invalid_ref_changes; invalid_ref_changes.Add(make_create("t55-bad-analysis", make_analysis("t55-bad-analysis", "missing", auth.workspace_id)));
			if(create_store.PublishBatch(auth, "t55-invalid-schema", invalid_changes).status != "invalid_schema" ||
			   create_store.PublishBatch(auth, "t55-invalid-reference", invalid_ref_changes).status != "invalid_reference" ||
			   DirectoryExists(AppendFileName(create_root, "records"))) {
				Cerr() << "FAILED: T55 schema/reference rejection created a destination artifact\n";
				return false;
			}
			DataBatchResult created = create_store.PublishBatch(auth, "t55-create-pair", create_changes);
			String paper_path = AppendFileName(create_root, "records/" + DataStore::ContentDigest(auth.workspace_id + ":t55-paper") + ".json");
			String analysis_path = AppendFileName(create_root, "records/" + DataStore::ContentDigest(auth.workspace_id + ":t55-analysis") + ".json");
			JournalRecoveryResult create_cleanup = create_store.RecoverPendingBatches(auth);
			DataBatchResult retry = create_store.PublishBatch(auth, "t55-create-pair", create_changes);
			ValueMap altered_paper = created_paper; altered_paper.Set("title", "Altered retry");
			Vector<DataBatchChange> altered_changes; altered_changes.Add(make_create("t55-paper", altered_paper)); altered_changes.Add(make_create("t55-analysis", created_analysis));
			if(created.status != "committed" || create_cleanup.status != "recovered" || created.changed_ids.GetCount() != 2 || created.revisions[0] != "1" ||
			   !FileExists(paper_path) || !FileExists(analysis_path) || retry.status != "already_committed" ||
			   create_store.PublishBatch(auth, "t55-create-pair", altered_changes).status != "idempotency_conflict") {
				Cerr() << "FAILED: T55 committed create, scoped reference, or idempotent receipt behavior status=" << created.status
				       << " cleanup=" << create_cleanup.status
				       << " ids=" << created.changed_ids.GetCount() << " revs=" << created.revisions.GetCount()
				       << " paper=" << FileExists(paper_path) << " analysis=" << FileExists(analysis_path)
				       << " retry=" << retry.status << " altered=" << create_store.PublishBatch(auth, "t55-create-pair", altered_changes).status << "\n";
				return false;
			}
			DataBatchChange duplicate = make_create("t55-paper", created_paper);
			Vector<DataBatchChange> duplicate_changes; duplicate_changes.Add(duplicate);
			String duplicate_status = create_store.PublishBatch(auth, "t55-duplicate-identity", duplicate_changes).status;
			if(duplicate_status != "identity_conflict") {
				Cerr() << "FAILED: T55 duplicate identity was not rejected, status=" << duplicate_status << "\n"; return false;
			}
			String alternate_dir = AppendFileName(create_root, "papers");
			if(!CreatePrivateDirectory(alternate_dir)) return false;
			ValueMap colliding_id_record = make_paper("t55-path-collision", auth.workspace_id);
			if(!SaveFile(AppendFileName(alternate_dir, "path-collision.json"), AsJSON(colliding_id_record, true))) return false;
			Vector<DataBatchChange> other_path_changes;
			other_path_changes.Add(make_create("t55-path-collision", make_paper("t55-path-collision", auth.workspace_id)));
			if(create_store.PublishBatch(auth, "t55-duplicate-other-path", other_path_changes).status != "identity_conflict") {
				Cerr() << "FAILED: T55 same workspace identity at another path was not rejected\n"; return false;
			}
			String mixed_root = workspace.FixtureRoot("t55-mixed-batch");
			String mixed_papers = AppendFileName(mixed_root, "papers");
			if(!CreatePrivateDirectory(mixed_root) || !CreatePrivateDirectory(mixed_papers)) return false;
			String mixed_record_path = AppendFileName(mixed_papers, "mixed-existing.json");
			ValueMap mixed_original = make_paper("mixed-existing", auth.workspace_id);
			if(!SaveFile(mixed_record_path, AsJSON(mixed_original, true))) return false;
			DataStore mixed_store; if(!mixed_store.LoadWriteFixtureForTests(mixed_root, "synthetic-write-source")) return false;
			const DataDocument* mixed_doc = mixed_store.FindDocument(auth.workspace_id + ":mixed-existing");
			if(!mixed_doc || !mixed_doc->valid) { Cerr() << "FAILED: T55 mixed existing fixture did not validate\n"; return false; }
			DataBatchChange mixed_update;
			mixed_update.qualified_id = mixed_doc->qualified_id; mixed_update.expected_revision = mixed_doc->revision;
			mixed_update.expected_source_revision = mixed_doc->source_revision; mixed_update.proposed = mixed_doc->data;
			mixed_update.proposed.Set("title", "Updated in mixed batch");
			DataBatchChange mixed_create = make_create("mixed-analysis", make_analysis("mixed-analysis", "mixed-existing", auth.workspace_id));
			Vector<DataBatchChange> mixed_changes; mixed_changes.Add(mixed_update); mixed_changes.Add(mixed_create);
			DataBatchResult mixed_result = mixed_store.PublishBatch(auth, "t55-mixed-operation", mixed_changes);
			String mixed_create_path = AppendFileName(mixed_root, "records/" + DataStore::ContentDigest(auth.workspace_id + ":mixed-analysis") + ".json");
			if(mixed_result.status != "committed" || !FileExists(mixed_create_path) ||
			   ParseJSON(LoadFile(mixed_record_path))["title"] != "Updated in mixed batch") {
				Cerr() << "FAILED: T55 mixed create/update batch status=" << mixed_result.status << "\n"; return false;
			}
			String rollback_root = workspace.FixtureRoot("t55-publish-rollback");
			if(!CreatePrivateDirectory(rollback_root)) return false;
			DataStore rollback_store; if(!rollback_store.LoadWriteFixtureForTests(rollback_root, "synthetic-write-source")) return false;
			Vector<DataBatchChange> rollback_changes; rollback_changes.Add(make_create("t55-rollback", make_paper("t55-rollback", auth.workspace_id)));
			DataBatchResult interrupted = rollback_store.PublishBatch(auth, "t55-rollback-op", rollback_changes, DATA_WRITE_FAIL_BATCH_AFTER_REPLACE);
			String rollback_path = AppendFileName(rollback_root, "records/" + DataStore::ContentDigest(auth.workspace_id + ":t55-rollback") + ".json");
			DataStore fresh_rollback;
			if(interrupted.status != "uncommitted" || !FileExists(rollback_path) || fresh_rollback.LoadWriteFixtureForTests(rollback_root, "synthetic-write-source") ||
			   fresh_rollback.RecoverPendingBatches(auth).status != "recovered" || FileExists(rollback_path)) {
				Cerr() << "FAILED: T55 fresh-store rollback after create publication\n"; return false;
			}
			String before_root = workspace.FixtureRoot("t55-publish-before");
			if(!CreatePrivateDirectory(before_root)) return false;
			DataStore before_store; if(!before_store.LoadWriteFixtureForTests(before_root, "synthetic-write-source")) return false;
			Vector<DataBatchChange> before_changes; before_changes.Add(make_create("t55-before", make_paper("t55-before", auth.workspace_id)));
			DataBatchResult before_publish = before_store.PublishBatch(auth, "t55-before-op", before_changes, DATA_WRITE_FAIL_BATCH_BEFORE_REPLACE);
			String before_path = AppendFileName(before_root, "records/" + DataStore::ContentDigest(auth.workspace_id + ":t55-before") + ".json");
			if(before_publish.status != "uncommitted" || FileExists(before_path) || before_store.RecoverPendingBatches(auth).status != "recovered" || FileExists(before_path)) {
				Cerr() << "FAILED: T55 fail-before-publish did not leave an absent canonical record\n"; return false;
			}
			String replay_root = workspace.FixtureRoot("t55-publish-replay");
			if(!CreatePrivateDirectory(replay_root)) return false;
			DataStore replay_store; if(!replay_store.LoadWriteFixtureForTests(replay_root, "synthetic-write-source")) return false;
			Vector<DataBatchChange> replay_changes; replay_changes.Add(make_create("t55-replay", make_paper("t55-replay", auth.workspace_id)));
			DataBatchResult committed_pending = replay_store.PublishBatch(auth, "t55-replay-op", replay_changes, DATA_WRITE_FAIL_BATCH_COMMIT_MANIFEST);
			String replay_path = AppendFileName(replay_root, "records/" + DataStore::ContentDigest(auth.workspace_id + ":t55-replay") + ".json");
			if(committed_pending.status != "uncertain" || !FileExists(replay_path)) { Cerr() << "FAILED: T55 committed create replay fixture\n"; return false; }
			String replay_op = AppendFileName(AppendFileName(replay_root, ".mgr-journal"), "t55-replay-op");
			Value replay_manifest_value = ParseJSON(LoadFile(AppendFileName(replay_op, "manifest.json")));
			ValueMap replay_manifest = replay_manifest_value; replay_manifest.Set("state", "committed");
			String digest; for(int i = 0; i < 64; i++) digest.Cat('a'); replay_manifest.Set("request_digest", digest);
			if(!SaveFile(AppendFileName(replay_op, "manifest.json"), AsJSON(replay_manifest, true))) return false;
			DataStore fresh_replay; if(fresh_replay.LoadWriteFixtureForTests(replay_root, "synthetic-write-source")) return false;
			if(fresh_replay.RecoverPendingBatches(auth).status != "recovered" || LoadFile(replay_path).IsEmpty()) {
				Cerr() << "FAILED: T55 fresh-store committed create replay\n"; return false;
			}
			Cout() << "ok    T55 PublishBatch creates and mixed update, trusted references, hashed paths, collision checks, receipts and restart recovery\n";
		}
		// T58 synthetic import writer coverage; all roots and credentials are test-only.
		{
			String root = workspace.FixtureRoot("t58-evidence-writer");
			if(!CreatePrivateDirectory(root)) return false;
			DataStore writer;
			if(!writer.LoadWriteFixtureForTests(root, "synthetic-write-source")) return false;
			DataWriteAuthorization t58_auth;
			t58_auth.authenticated = true; t58_auth.action_scope = "record:write";
			t58_auth.principal = "synthetic-t58-principal"; t58_auth.source_id = "synthetic-write-source";
			t58_auth.workspace_id = "synthetic-workspace";
			TrustedPolicyRegistry policy; policy.configured = true;
			PolicyWorkspaceFact workspace_fact; workspace_fact.identity.workspace_id = "synthetic-workspace"; workspace_fact.identity.object_id = "workspace"; policy.workspaces.Add(workspace_fact);
			PolicySourceFact source_fact; source_fact.workspace = workspace_fact.identity; source_fact.identity.workspace_id = "synthetic-workspace"; source_fact.identity.object_id = "source"; source_fact.classification = POLICY_PUBLIC; source_fact.visible = true; policy.sources.Add(source_fact);
			auto capture = [&](const String& revision, const String& predecessor, const String& body, const String& locator_id, const String& captured, bool retracted = false, bool complete = false, const String& selected_id = String()) {
				EvidencePrReviewValue value; EvidenceCapture& c = value.capture;
				c.kind = EVIDENCE_PR_REVIEW; c.workspace_id = "synthetic-workspace"; c.source_identity = "source";
				c.upstream_revision = revision; c.previous_revision = predecessor; c.source_at = "2026-01-02T03:04:05Z"; c.captured_at = captured;
				c.collector = "synthetic"; c.policy_source_ids.Add("source"); c.retracted = retracted;
				c.locator.type = "pr-review"; c.locator.id = selected_id.IsEmpty() ? locator_id : selected_id; c.locator.section = "review";
				EvidencePrReviewEntry entry; entry.id = locator_id; entry.revision = "review"; entry.content = body; entry.source_identity = "source"; entry.upstream_revision = revision; value.reviews.Add(entry);
				if(complete) { EvidencePrReviewEntry other=entry; other.id=locator_id+"-alt"; other.content=body+" alternate"; value.reviews.Add(other); }
				return value;
			};
			auto count_kind = [](const DataStore& s, const String& kind) { int count=0; for(const DataDocument& d:s.GetDocuments()) if(d.kind==kind) count++; return count; };
			EvidencePrReviewValue first = capture("rev-1", "", "first body", "review-1", "2026-01-02T03:04:05Z", false, true);
			DataBatchResult first_result = EvidenceIngestion::Import(writer, policy, first, t58_auth);
			EvidencePreview first_preview = EvidenceIngestion::PrepareImport(writer, policy, first, t58_auth);
			if(first_result.status != "committed" || first_preview.snapshot_id.IsEmpty()) { Cerr() << "FAILED: T58 first import status=" << first_result.status << " preview=" << first_preview.status << "\n"; return false; }
			String recovery1 = writer.RecoverPendingBatches(t58_auth).status;
			if(recovery1 != "recovered") { Cerr() << "FAILED: T58 first recovery=" << recovery1 << "\n"; return false; }
			DataStore refreshed; if(!refreshed.LoadWriteFixtureForTests(root, "synthetic-write-source")) return false; writer = refreshed;
			EvidencePrReviewValue second_locator = capture("rev-1", "", "first body", "review-1", "2026-01-02T03:04:06Z", false, true, "review-1-alt");
			DataBatchResult second_locator_result = EvidenceIngestion::Import(writer, policy, second_locator, t58_auth);
			if(second_locator_result.status!="committed" || writer.RecoverPendingBatches(t58_auth).status!="recovered") { Cerr() << "FAILED: T58 second locator commit status=" << second_locator_result.status << "\n"; return false; }
			DataStore after_second_locator; if(!after_second_locator.LoadWriteFixtureForTests(root, "synthetic-write-source")) return false; writer=after_second_locator;
			if(count_kind(writer,"evidence-snapshot")!=1 || count_kind(writer,"evidence-item")!=2) { Cerr() << "FAILED: T58 multiple locator counts snapshots=" << count_kind(writer,"evidence-snapshot") << " items=" << count_kind(writer,"evidence-item") << "\n"; return false; }
			const DataDocument *stored_snapshot=NULL; bool selected_excerpt=false, second_excerpt=false;
			for(const DataDocument& d:writer.GetDocuments()) {
				if(d.kind=="evidence-snapshot") stored_snapshot=&d;
				if(d.kind=="evidence-item" && MapGetString(MapGet(d.data,"locator"),"id")=="review-1" && MapGetString(d.data,"excerpt")=="first body") selected_excerpt=true;
				if(d.kind=="evidence-item" && MapGetString(MapGet(d.data,"locator"),"id")=="review-1-alt" && MapGetString(d.data,"excerpt")=="first body alternate") second_excerpt=true;
			}
			if(!stored_snapshot || MapGetString(stored_snapshot->data,"content").Find("review-1")<0 ||
			   MapGetString(stored_snapshot->data,"content").Find("review-1-alt")<0 ||
			   MapGetString(stored_snapshot->data,"content").Find("first body alternate")<0 || !selected_excerpt || !second_excerpt) {
				Cerr() << "FAILED: T58 full canonical snapshot content or selected evidence excerpts\n"; return false;
			}
			if(EvidenceIngestion::Import(writer, policy, second_locator, t58_auth).status!="already_committed" || count_kind(writer,"evidence-item")!=2) { Cerr() << "FAILED: T58 exact locator retry created duplicate evidence\n"; return false; }
			EvidencePrReviewValue permuted; permuted.capture=first.capture; permuted.reviews.Add(first.reviews[1]); permuted.reviews.Add(first.reviews[0]);
			EvidencePreview permuted_preview=EvidenceIngestion::PrepareImport(writer,policy,permuted,t58_auth);
			if(permuted_preview.status!="already_committed" || !permuted_preview.snapshot.IsEmpty() || !permuted_preview.evidence.IsEmpty() || !permuted_preview.content_sha256.IsEmpty()) {
				Cerr() << "FAILED: T59 duplicate preview disclosed canonical snapshot or hash\n"; return false;
			}
			EvidencePrReviewValue changed=capture("rev-1","","first body","review-1","2026-01-02T03:04:06Z",false,true);
			changed.reviews[1].content="changed non-selected alternate";
			DataBatchResult changed_result = EvidenceIngestion::Import(writer, policy, changed, t58_auth);
			if(changed_result.status != "revision_conflict" || count_kind(writer,"evidence-snapshot")!=1 || count_kind(writer,"evidence-item")!=2) { Cerr() << "FAILED: T58 changed revision status=" << changed_result.status << " docs=" << writer.GetDocuments().GetCount() << "\n"; return false; }
			EvidencePrReviewValue second = capture("rev-2", "rev-1", "second body", "review-2", "2020-01-01T00:00:00Z");
			EvidencePreview second_preview = EvidenceIngestion::PrepareImport(writer, policy, second, t58_auth);
			if(second_preview.predecessor_id != first_preview.snapshot_id) { Cerr() << "FAILED: T58 explicit predecessor resolution got=" << second_preview.predecessor_id << " expected=" << first_preview.snapshot_id << " status=" << second_preview.status << "\n"; return false; }
			DataBatchResult second_result = EvidenceIngestion::Import(writer, policy, second, t58_auth);
			if(second_result.status != "committed") { Cerr() << "FAILED: T58 second import status=" << second_result.status << "\n"; return false; }
			String recovery2 = writer.RecoverPendingBatches(t58_auth).status;
			if(recovery2 != "recovered") { Cerr() << "FAILED: T58 second recovery=" << recovery2 << "\n"; return false; }
			DataStore restarted; if(!restarted.LoadWriteFixtureForTests(root, "synthetic-write-source")) return false;
			DataBatchResult retry = EvidenceIngestion::Import(restarted, policy, first, t58_auth);
			if(retry.status != "already_committed") { Cerr() << "FAILED: T58 restart retry status=" << retry.status << "\n"; return false; }
			ValueMap prior_revision_two; int snapshots_before_tombstone=count_kind(restarted,"evidence-snapshot"), items_before_tombstone=count_kind(restarted,"evidence-item");
			for(const DataDocument& d:restarted.GetDocuments()) if(d.kind=="evidence-snapshot" && MapGetString(d.data,"upstream_revision")=="rev-2") prior_revision_two=d.data;
			if(prior_revision_two.IsEmpty() || snapshots_before_tombstone!=2 || items_before_tombstone!=3) { Cerr() << "FAILED: T58 prior revision fixture/counts before tombstone\n"; return false; }
			EvidencePrReviewValue tombstone = capture("rev-3", "rev-2", "", "review-3", "2026-01-02T03:04:07Z", true);
			DataBatchResult tombstone_result = EvidenceIngestion::Import(restarted, policy, tombstone, t58_auth);
			if(tombstone_result.status != "committed") { Cerr() << "FAILED: T58 tombstone status=" << tombstone_result.status << "\n"; return false; }
			if(restarted.RecoverPendingBatches(t58_auth).status != "recovered") return false;
			DataStore tombstone_store; if(!tombstone_store.LoadWriteFixtureForTests(root,"synthetic-write-source")) return false;
			EvidencePreview tombstone_repeat=EvidenceIngestion::PrepareImport(tombstone_store,policy,tombstone,t58_auth);
			bool stored_tombstone=false, prior_unchanged=false;
			for(const DataDocument& d:tombstone_store.GetDocuments()) if(d.kind=="evidence-snapshot") {
				if(MapGetString(d.data,"upstream_revision")=="rev-3") stored_tombstone=MapGetString(d.data,"content")=="[retracted]";
				if(MapGetString(d.data,"upstream_revision")=="rev-2") prior_unchanged=AsJSON(d.data,true)==AsJSON(prior_revision_two,true);
			}
			if(!stored_tombstone || !prior_unchanged || count_kind(tombstone_store,"evidence-snapshot")!=snapshots_before_tombstone+1 || count_kind(tombstone_store,"evidence-item")!=items_before_tombstone+1 ||
			   tombstone_repeat.status!="already_committed" || !tombstone_repeat.snapshot.IsEmpty() || !tombstone_repeat.evidence.IsEmpty() || !tombstone_repeat.content_sha256.IsEmpty()) { Cerr() << "FAILED: T58 tombstone marker, immutable predecessor, or T59 duplicate redaction\n"; return false; }
			EvidencePrReviewValue altered_event=capture("rev-2","rev-1","second body","review-2","2026-01-02T03:04:06Z",true);
			if(EvidenceIngestion::Import(tombstone_store,policy,altered_event,t58_auth).status!="revision_conflict" || count_kind(tombstone_store,"evidence-snapshot")!=snapshots_before_tombstone+1 || count_kind(tombstone_store,"evidence-item")!=items_before_tombstone+1) {
				Cerr() << "FAILED: T58 same-revision event-state conflict wrote records\n"; return false;
			}
			EvidencePrReviewValue first_retraction=capture("rev-first-retraction","","", "review-first-retraction","2026-01-02T03:04:08Z",true);
			if(EvidenceIngestion::Import(tombstone_store,policy,first_retraction,t58_auth).status!="lineage_conflict") { Cerr() << "FAILED: T58 first-time retraction without predecessor was accepted\n"; return false; }
			EvidencePrReviewValue missing = capture("rev-4", "missing", "not written", "review-4", "2026-01-02T03:04:08Z");
			DataBatchResult missing_result = EvidenceIngestion::Import(restarted, policy, missing, t58_auth);
			if(missing_result.status != "lineage_conflict") { Cerr() << "FAILED: T58 missing predecessor status=" << missing_result.status << "\n"; return false; }
			DataWriteAuthorization wrong = t58_auth; wrong.workspace_id = "wrong-workspace";
			EvidencePrReviewValue wrong_capture = capture("wrong-1", "", "not written", "review-wrong", "2026-01-02T03:04:09Z");
			DataBatchResult wrong_result = EvidenceIngestion::Import(restarted, policy, wrong_capture, wrong);
			if(wrong_result.status != "denied") { Cerr() << "FAILED: T58 wrong workspace status=" << wrong_result.status << "\n"; return false; }
			TrustedPolicyRegistry denied_policy;
			String policy_denial = EvidenceIngestion::Import(restarted, denied_policy, wrong_capture, t58_auth).status;
			if(policy_denial != "denied") { Cerr() << "FAILED: T58 default policy status=" << policy_denial << "\n"; return false; }
			DataStore readonly; if(!readonly.LoadFixtureForTests(root)) { Cerr() << "FAILED: T58 readonly fixture load\n"; return false; }
			DataWriteAuthorization readonly_auth=t58_auth; readonly_auth.source_id="self-test-fixture";
			DataBatchResult writer_denial = EvidenceIngestion::Import(readonly, policy, capture("rev-readonly", "rev-3", "not written", "review-readonly", "2026-01-02T03:04:10Z"), readonly_auth);
			if(writer_denial.status != "invalid_request" || !writer_denial.operation_id.IsEmpty() || writer_denial.changed_ids.GetCount() || writer_denial.revisions.GetCount()) { Cerr() << "FAILED: T59 writer denial leaked operation or changed identities\n"; return false; }
			String oversized_body; for(int i=0;i<140000;i++) oversized_body.Cat('"');
			EvidencePrReviewValue oversized_capture=capture("oversized-1","","oversized_body_unused","review-oversized","2026-01-02T03:04:10Z");
			oversized_capture.reviews[0].content=oversized_body;
			if(EvidenceIngestion::PrepareImport(writer,policy,oversized_capture,t58_auth).status!="invalid" ||
			   EvidenceIngestion::Import(writer,policy,oversized_capture,t58_auth).status!="invalid" ||
			   count_kind(writer,"evidence-snapshot")!=1 || count_kind(writer,"evidence-item")!=2) {
				Cerr() << "FAILED: T58 expanded canonical snapshot escaped content bound\n"; return false;
			}
			// A stored evidence ID with altered payload must conflict, never duplicate.
			String corrupt_root=workspace.FixtureRoot("t58-corrupt-evidence-item"); if(!CreatePrivateDirectory(corrupt_root)) return false;
			DataStore corrupt_writer; if(!corrupt_writer.LoadWriteFixtureForTests(corrupt_root,"synthetic-write-source")) return false;
			EvidencePrReviewValue corrupt_capture=capture("corrupt-1","","corrupt fixture body","review-corrupt","2026-01-02T03:04:05Z");
			if(EvidenceIngestion::Import(corrupt_writer,policy,corrupt_capture,t58_auth).status!="committed" || corrupt_writer.RecoverPendingBatches(t58_auth).status!="recovered") return false;
			DataStore loaded_corrupt; if(!loaded_corrupt.LoadWriteFixtureForTests(corrupt_root,"synthetic-write-source")) return false;
			String evidence_path; ValueMap tampered_item;
			for(const DataDocument& d:loaded_corrupt.GetDocuments()) if(d.kind=="evidence-item") { evidence_path=d.path; tampered_item=d.data; break; }
			if(evidence_path.IsEmpty()) return false;
			tampered_item.Set("excerpt","changed corrupt payload");
			if(!SaveFile(evidence_path,AsJSON(tampered_item,true))) return false;
			DataStore reread_corrupt; if(!reread_corrupt.LoadWriteFixtureForTests(corrupt_root,"synthetic-write-source")) return false;
			EvidencePreview corrupt_preview=EvidenceIngestion::PrepareImport(reread_corrupt,policy,corrupt_capture,t58_auth);
			if(EvidenceIngestion::Import(reread_corrupt,policy,corrupt_capture,t58_auth).status!="evidence_conflict" || corrupt_preview.status!="evidence_conflict" || !corrupt_preview.snapshot.IsEmpty() || !corrupt_preview.evidence.IsEmpty() || !corrupt_preview.operation_id.IsEmpty() || !corrupt_preview.snapshot_id.IsEmpty() || !corrupt_preview.predecessor_id.IsEmpty() || !corrupt_preview.content_sha256.IsEmpty() || !corrupt_preview.classification.IsEmpty() || !corrupt_preview.visibility.IsEmpty()) { Cerr() << "FAILED: T59 corrupt evidence preview was not redacted\n"; return false; }
			ValueMap corrupt_envelope=MapGet(tampered_item,"envelope"); corrupt_envelope.Set("schema_version","99"); tampered_item.Set("envelope",corrupt_envelope);
			if(!SaveFile(evidence_path,AsJSON(tampered_item,true))) return false;
			DataStore invalid_duplicate; if(!invalid_duplicate.LoadWriteFixtureForTests(corrupt_root,"synthetic-write-source")) return false;
			EvidencePreview invalid_duplicate_preview=EvidenceIngestion::PrepareImport(invalid_duplicate,policy,corrupt_capture,t58_auth);
			if(EvidenceIngestion::Import(invalid_duplicate,policy,corrupt_capture,t58_auth).status!="evidence_conflict" || invalid_duplicate_preview.status!="evidence_conflict" || !invalid_duplicate_preview.snapshot.IsEmpty() || !invalid_duplicate_preview.evidence.IsEmpty() || !invalid_duplicate_preview.content_sha256.IsEmpty()) { Cerr() << "FAILED: T59 invalid matching evidence preview was not redacted\n"; return false; }
			// Two distinct records with one source revision make predecessor selection ambiguous.
			String ambiguous_root=workspace.FixtureRoot("t58-ambiguous-predecessor"), ambiguous_records=AppendFileName(ambiguous_root,"records");
			if(!CreatePrivateDirectory(ambiguous_root) || !CreatePrivateDirectory(ambiguous_records)) return false;
			auto ambiguous_snapshot=[&](const String& id) {
				ValueMap r; r.Add("id",id); r.Add("kind","evidence-snapshot"); r.Add("workspace_id","synthetic-workspace"); r.Add("source_kind","pr-review");
				r.Add("source_identity","source"); r.Add("upstream_revision","rev-1"); r.Add("previous_revision",""); r.Add("content_sha256",DataStore::ContentDigest("ambiguous synthetic pin"));
				r.Add("captured_at","2026-01-02T03:04:05Z"); r.Add("source_at","2026-01-02T03:04:05Z"); r.Add("collector","synthetic");
				ValueMap loc; loc.Add("type","pr-review"); loc.Add("id","review-ambiguous"); loc.Add("revision","review"); r.Add("locator",loc);
				r.Add("resolution","resolved"); r.Add("content","synthetic"); r.Add("classification","public"); r.Add("visibility","visible"); r.Add("retracted",false); r.Add("predecessor","none");
				ValueArray contributors; contributors.Add("source"); r.Add("contributors",contributors);
				ValueMap env; env.Add("schema_id","skylark/evidence-snapshot"); env.Add("schema_version","2"); env.Add("workspace_id","synthetic-workspace"); env.Add("stable_id",id); env.Add("record_kind","evidence-snapshot"); env.Add("revision","1"); env.Add("visibility","private"); env.Add("provenance_ref","synthetic-t58"); r.Add("envelope",env); return r;
			};
			if(!SaveFile(AppendFileName(ambiguous_records,"one.json"),AsJSON(ambiguous_snapshot("ambiguous-one"),true)) || !SaveFile(AppendFileName(ambiguous_records,"two.json"),AsJSON(ambiguous_snapshot("ambiguous-two"),true))) return false;
			DataStore ambiguous_store; if(!ambiguous_store.LoadWriteFixtureForTests(ambiguous_root,"synthetic-write-source")) return false;
			EvidencePreview ambiguous_preview=EvidenceIngestion::PrepareImport(ambiguous_store,policy,capture("rev-2","rev-1","new body","review-new","2026-01-02T03:04:06Z"),t58_auth);
			if(ambiguous_preview.status!="lineage_conflict" || !ambiguous_preview.snapshot.IsEmpty()) { Cerr() << "FAILED: T58 ambiguous predecessor status=" << ambiguous_preview.status << "\n"; return false; }
			// An interrupted first replacement is rolled back by a fresh store before retry.
			String interrupted_root=workspace.FixtureRoot("t58-interrupted-publish"); if(!CreatePrivateDirectory(interrupted_root)) return false;
			DataStore interrupted_store; if(!interrupted_store.LoadWriteFixtureForTests(interrupted_root,"synthetic-write-source")) return false;
			EvidencePrReviewValue interrupted_capture=capture("interrupt-1","","interrupted body","review-interrupt","2026-01-02T03:04:05Z");
			EvidencePreview interrupted_preview=EvidenceIngestion::PrepareImport(interrupted_store,policy,interrupted_capture,t58_auth);
			DataBatchChange snapshot_change,evidence_change; snapshot_change.action=evidence_change.action=DATA_BATCH_CREATE;
			snapshot_change.qualified_id=t58_auth.workspace_id+":"+MapGetString(interrupted_preview.snapshot,"id"); snapshot_change.proposed=interrupted_preview.snapshot;
			evidence_change.qualified_id=t58_auth.workspace_id+":"+MapGetString(interrupted_preview.evidence,"id"); evidence_change.proposed=interrupted_preview.evidence;
			Vector<DataBatchChange> interrupted_changes; interrupted_changes.Add(snapshot_change); interrupted_changes.Add(evidence_change);
			DataBatchResult interrupted=interrupted_store.PublishBatch(t58_auth,interrupted_preview.operation_id,interrupted_changes,DATA_WRITE_FAIL_BATCH_AFTER_REPLACE);
			DataStore fresh_interrupted; bool initial_load=fresh_interrupted.LoadWriteFixtureForTests(interrupted_root,"synthetic-write-source");
			JournalRecoveryResult interrupted_recovery=fresh_interrupted.RecoverPendingBatches(t58_auth);
			if(interrupted.status!="uncommitted" || initial_load || interrupted_recovery.status!="recovered") { Cerr() << "FAILED: T58 interrupted recovery status=" << interrupted.status << "/" << interrupted_recovery.status << "\n"; return false; }
			DataStore retry_store; if(!retry_store.LoadWriteFixtureForTests(interrupted_root,"synthetic-write-source") || count_kind(retry_store,"evidence-snapshot")!=0 || count_kind(retry_store,"evidence-item")!=0) return false;
			DataBatchResult interrupted_retry=EvidenceIngestion::Import(retry_store,policy,interrupted_capture,t58_auth);
			if(interrupted_retry.status!="committed" || retry_store.RecoverPendingBatches(t58_auth).status!="recovered") { Cerr() << "FAILED: T58 interrupted retry status=" << interrupted_retry.status << "\n"; return false; }
			DataStore after_retry; if(!after_retry.LoadWriteFixtureForTests(interrupted_root,"synthetic-write-source") || count_kind(after_retry,"evidence-snapshot")!=1 || count_kind(after_retry,"evidence-item")!=1) return false;
			Cout() << "ok    T58 same-revision hash conflict, one snapshot/multiple locators, exact duplicate, ambiguous lineage, explicit predecessor/tombstone, workspace denial, receipt retry, and interrupted recovery\n";
		}
		// T59: preview disclosure is gated by workspace authorization and policy.
		{
			DataStore preview_store;
			String preview_root=workspace.FixtureRoot("t59-safe-preview");
			if(!CreatePrivateDirectory(preview_root) || !preview_store.LoadWriteFixtureForTests(preview_root,"synthetic-write-source")) return false;
			DataWriteAuthorization t59_auth;
			t59_auth.authenticated=true; t59_auth.action_scope="record:write"; t59_auth.principal="synthetic-t59-principal"; t59_auth.source_id="synthetic-write-source"; t59_auth.workspace_id="synthetic-workspace";
			TrustedPolicyRegistry t59_policy; t59_policy.configured=true;
			PolicyWorkspaceFact t59_workspace; t59_workspace.identity.workspace_id="synthetic-workspace"; t59_workspace.identity.object_id="workspace"; t59_policy.workspaces.Add(t59_workspace);
			PolicySourceFact t59_source; t59_source.workspace=t59_workspace.identity; t59_source.identity.workspace_id="synthetic-workspace"; t59_source.identity.object_id="source"; t59_source.classification=POLICY_PUBLIC; t59_source.visible=true; t59_policy.sources.Add(t59_source);
			String secret="T59_SECRET_MARKER_PRIVATE_LOCATOR_HASH";
			auto safe_failure=[](const EvidencePreview& p) {
				return p.snapshot.IsEmpty() && p.evidence.IsEmpty() && p.operation_id.IsEmpty() && p.snapshot_id.IsEmpty() && p.predecessor_id.IsEmpty() && p.content_sha256.IsEmpty() && p.classification.IsEmpty() && p.visibility.IsEmpty() && p.resolution.IsEmpty();
			};
			auto mentions=[](const EvidencePreview& p,const String& marker) {
				return AsJSON(p.snapshot,true).Find(marker)>=0 || AsJSON(p.evidence,true).Find(marker)>=0 || p.operation_id.Find(marker)>=0 || p.snapshot_id.Find(marker)>=0 || p.predecessor_id.Find(marker)>=0 || p.content_sha256.Find(marker)>=0;
			};
			EvidencePrReviewValue good;
			good.capture.kind=EVIDENCE_PR_REVIEW; good.capture.workspace_id="synthetic-workspace"; good.capture.source_identity="source"; good.capture.upstream_revision="t59-rev-1"; good.capture.source_at=good.capture.captured_at="2026-01-02T03:04:05Z"; good.capture.collector="synthetic"; good.capture.policy_source_ids.Add("source");
			good.capture.locator.type="pr-review"; good.capture.locator.id="t59-locator"; good.capture.locator.section="review";
			EvidencePrReviewEntry good_entry; good_entry.id="t59-locator"; good_entry.revision="review"; good_entry.content="<script>\n**markdown** &#x68;ttp://inert.invalid/\x01"; good_entry.source_identity="source"; good_entry.upstream_revision="t59-rev-1"; good.reviews.Add(good_entry);
			EvidencePreview authorized_preview=EvidenceIngestion::PrepareImport(preview_store,t59_policy,good,t59_auth);
			if(authorized_preview.status!="ready" || MapGetString(authorized_preview.evidence,"excerpt").GetCount()>4096 || MapGetString(authorized_preview.snapshot,"content").GetCount()>EvidenceIngestion::MAX_CONTENT_BYTES || MapGetString(authorized_preview.evidence,"excerpt")!=good_entry.content) { Cerr()<<"FAILED: T59 authorized bounded inert preview\n"; return false; }
			EvidencePrReviewValue bounded; bounded.capture=good.capture; bounded.capture.upstream_revision="t59-bounded-rev"; bounded.capture.locator.id="t59-bounded-locator";
			EvidencePrReviewEntry bounded_entry=good_entry; bounded_entry.id="t59-bounded-locator"; bounded_entry.upstream_revision="t59-bounded-rev"; bounded_entry.content.Clear(); for(int i=0;i<5000;i++) bounded_entry.content.Cat('x'); bounded.reviews.Add(bounded_entry);
			EvidencePreview bounded_preview=EvidenceIngestion::PrepareImport(preview_store,t59_policy,bounded,t59_auth);
			if(bounded_preview.status!="ready" || MapGetString(bounded_preview.evidence,"excerpt").GetCount()!=4096 || MapGetString(bounded_preview.snapshot,"content").GetCount()>EvidenceIngestion::MAX_CONTENT_BYTES) return false;
			DataWriteAuthorization empty_auth;
			EvidencePreview empty_denial=EvidenceIngestion::PrepareImport(preview_store,t59_policy,good,empty_auth);
			DataWriteAuthorization forged; // Public fields cannot forge the trusted authorization.
			EvidencePreview forged_denial=EvidenceIngestion::PrepareImport(preview_store,t59_policy,good,forged);
			DataWriteAuthorization wrong_workspace=t59_auth; wrong_workspace.workspace_id="other-workspace";
			EvidencePreview scope_denial=EvidenceIngestion::PrepareImport(preview_store,t59_policy,good,wrong_workspace);
			DataWriteAuthorization wrong_source=t59_auth; wrong_source.source_id="different-synthetic-source";
			EvidencePreview source_denial=EvidenceIngestion::PrepareImport(preview_store,t59_policy,good,wrong_source);
			if(empty_denial.status!="denied" || forged_denial.status!="denied" || scope_denial.status!="denied" || source_denial.status!="denied" || !safe_failure(empty_denial) || !safe_failure(forged_denial) || !safe_failure(scope_denial) || !safe_failure(source_denial) || authorized_preview.status!="ready") return false;
			EvidencePrReviewValue duplicate; duplicate.capture=good.capture; duplicate.reviews.Add(good.reviews[0]);
			if(EvidenceIngestion::Import(preview_store,t59_policy,good,t59_auth).status!="committed" || preview_store.RecoverPendingBatches(t59_auth).status!="recovered") return false;
			DataStore duplicate_store; if(!duplicate_store.LoadWriteFixtureForTests(preview_root,"synthetic-write-source")) return false;
			EvidencePreview duplicate_preview=EvidenceIngestion::PrepareImport(duplicate_store,t59_policy,duplicate,t59_auth);
			DataBatchResult duplicate_import=EvidenceIngestion::Import(duplicate_store,t59_policy,duplicate,t59_auth);
			if(duplicate_preview.status!="already_committed" || !safe_failure(duplicate_preview) || duplicate_import.status!="already_committed" || !duplicate_import.operation_id.IsEmpty() || duplicate_import.changed_ids.GetCount() || duplicate_import.revisions.GetCount()) return false;
			TrustedPolicyRegistry hidden_policy; hidden_policy.configured=true; hidden_policy.workspaces.Add(t59_workspace); PolicySourceFact hidden_source=t59_source; hidden_source.visible=false; hidden_policy.sources.Add(hidden_source);
			EvidencePreview hidden=EvidenceIngestion::PrepareImport(preview_store,hidden_policy,good,t59_auth);
			EvidencePrReviewValue mixed; mixed.capture=good.capture; mixed.capture.policy_source_ids.Add("missing-secret-contributor"); mixed.reviews.Add(good.reviews[0]);
			EvidencePreview mixed_denied=EvidenceIngestion::PrepareImport(preview_store,t59_policy,mixed,t59_auth);
			TrustedPolicyRegistry unknown_policy;
			EvidencePreview unknown=EvidenceIngestion::PrepareImport(preview_store,unknown_policy,good,t59_auth);
			EvidencePrReviewValue invalid; invalid.capture=good.capture; invalid.reviews.Add(good.reviews[0]); invalid.reviews[0].content=secret; invalid.reviews[0].id="bad locator!";
			EvidencePreview invalid_preview=EvidenceIngestion::PrepareImport(preview_store,t59_policy,invalid,t59_auth);
			EvidencePrReviewValue conflicting; conflicting.capture=good.capture; conflicting.reviews.Add(good.reviews[0]); conflicting.reviews[0].content="T59_CHANGED_BYTES";
			EvidencePreview conflict=EvidenceIngestion::PrepareImport(duplicate_store,t59_policy,conflicting,t59_auth);
			EvidencePrReviewValue missing_predecessor; missing_predecessor.capture=good.capture; missing_predecessor.capture.upstream_revision="t59-rev-2"; missing_predecessor.capture.previous_revision="absent-secret-revision"; missing_predecessor.reviews.Add(good.reviews[0]); missing_predecessor.reviews[0].upstream_revision="t59-rev-2";
			EvidencePreview lineage=EvidenceIngestion::PrepareImport(duplicate_store,t59_policy,missing_predecessor,t59_auth);
			for(const EvidencePreview* p : {&hidden,&mixed_denied,&unknown,&invalid_preview,&conflict,&lineage}) if(!safe_failure(*p) || mentions(*p,secret)) { Cerr()<<"FAILED: T59 non-success preview disclosure\n"; return false; }
			Cout()<<"ok    T59 authorized bounded inert previews and redacted denial, invalid, conflict, lineage, and duplicate results\n";
		}
		String close_failure_root = workspace.FixtureRoot("journal-close-failure");
		String close_failure_papers = AppendFileName(close_failure_root, "papers");
		if(!CreatePrivateDirectory(close_failure_root) || !CreatePrivateDirectory(close_failure_papers)) {
			Cerr() << "FAILED: synthetic close-failure root setup\n";
			return false;
		}
		String close_failure_record = AppendFileName(close_failure_papers, "write-paper.json");
		if(!SaveFile(close_failure_record, current_bytes)) { Cerr() << "FAILED: synthetic close-failure record write\n"; return false; }
		DataStore close_failure_store;
		if(!close_failure_store.LoadWriteFixtureForTests(close_failure_root, "synthetic-write-source")) { Cerr() << "FAILED: synthetic close-failure store load\n"; return false; }
		JournalStageResult close_failure = close_failure_store.PrepareJournal(auth, "close-op", stages,
													  DATA_WRITE_FAIL_JOURNAL_DIRECTORY_CLOSE);
		String close_op_dir = AppendFileName(AppendFileName(close_failure_root, ".mgr-journal"), "close-op");
		String close_manifest_bytes = LoadFile(AppendFileName(close_op_dir, "manifest.json"));
		JournalManifest close_manifest;
		DataStore close_failure_catalog;
		if(close_failure.status != "prepared_uncertain" || close_failure.operation_id != "close-op" ||
		   LoadFile(close_failure_record) != current_bytes || close_manifest_bytes.IsEmpty() ||
		   !DataStore::ParseJournalManifest(close_manifest_bytes, close_manifest) || close_manifest.state != "prepared" ||
		   close_manifest.records.GetCount() != 1 ||
		   LoadFile(AppendFileName(close_op_dir, close_manifest.records[0].backup_ref)) != current_bytes ||
		   LoadFile(AppendFileName(close_op_dir, close_manifest.records[0].payload_ref)) != stage.new_payload ||
		   close_failure_catalog.LoadFixtureForTests(close_failure_root) || close_failure_catalog.GetDocuments().GetCount() != 0) {
			Cerr() << "FAILED: close uncertainty did not preserve durable prepared journal, record bytes, and catalog fail-closed\n";
			return false;
		}
		for(DataWriteTestFailpoint fail : { DATA_WRITE_FAIL_JOURNAL_PAYLOAD, DATA_WRITE_FAIL_JOURNAL_MANIFEST,
		                                   DATA_WRITE_FAIL_JOURNAL_ROOT_SYNC, DATA_WRITE_FAIL_JOURNAL_OPERATION_SYNC,
		                                   DATA_WRITE_FAIL_JOURNAL_PRE_MANIFEST_SYNC }) {
			String failed_root = workspace.FixtureRoot("journal-failure-" + AsString((int)fail));
			if(!CreatePrivateDirectory(failed_root) || !CreatePrivateDirectory(AppendFileName(failed_root, "papers"))) {
				Cerr() << "FAILED: could not create synthetic journal failure root\n";
				return false;
			}
			SaveFile(AppendFileName(AppendFileName(failed_root, "papers"), "write-paper.json"), current_bytes);
			DataStore failure_store;
			if(!failure_store.LoadWriteFixtureForTests(failed_root, "synthetic-write-source")) return false;
			JournalStageResult failure = failure_store.PrepareJournal(auth, "failed-op", stages, fail);
			if(failure.status != "stage_failed" || LoadFile(AppendFileName(AppendFileName(failed_root, "papers"), "write-paper.json")) != current_bytes ||
			   FileExists(AppendFileName(AppendFileName(AppendFileName(failed_root, ".mgr-journal"), "failed-op"), "manifest.json")) ||
			   DirectoryExists(AppendFileName(AppendFileName(failed_root, ".mgr-journal"), "failed-op"))) {
				Cerr() << "FAILED: failed journal stage left a partial operation\n";
				return false;
			}
			if(DirectoryExists(AppendFileName(failed_root, ".mgr-journal"))) {
				DataStore fail_closed;
				if(fail_closed.LoadFixtureForTests(failed_root) || fail_closed.GetDocuments().GetCount() != 0) {
					Cerr() << "FAILED: journal sync failure residue did not fail catalog closed\n";
					return false;
				}
			}
		}
		String residue_root = workspace.FixtureRoot("journal-cleanup-residue");
		if(!CreatePrivateDirectory(residue_root) || !CreatePrivateDirectory(AppendFileName(residue_root, "papers"))) return false;
		SaveFile(AppendFileName(AppendFileName(residue_root, "papers"), "write-paper.json"), current_bytes);
		DataStore residue_store;
		if(!residue_store.LoadWriteFixtureForTests(residue_root, "synthetic-write-source")) return false;
		JournalStageResult residue = residue_store.PrepareJournal(auth, "orphan-op", stages, DATA_WRITE_FAIL_JOURNAL_CLEANUP);
		DataStore residue_catalog;
		if(residue.status != "journal_cleanup_required" ||
		   LoadFile(AppendFileName(AppendFileName(residue_root, "papers"), "write-paper.json")) != current_bytes ||
		   !DirectoryExists(AppendFileName(AppendFileName(residue_root, ".mgr-journal"), "orphan-op")) ||
		   residue_catalog.LoadFixtureForTests(residue_root) || residue_catalog.GetDocuments().GetCount() != 0 ||
		   residue_catalog.GetSourceDiagnostics().GetCount() != 1 || residue_catalog.GetSourceDiagnostics()[0] != "journal:recovery_required") {
			Cerr() << "FAILED: cleanup residue was not reported and fail-closed without record mutation\n";
			return false;
		}
		String orphan_operation = AppendFileName(AppendFileName(residue_root, ".mgr-journal"), "orphan-op");
		String orphan_backup = AppendFileName(orphan_operation, "backup-0.bin");
		String unknown_artifact = AppendFileName(orphan_operation, "unexpected.bin");
		SaveFile(unknown_artifact, "preserve this unrecognized journal artifact");
		String orphan_backup_bytes = LoadFile(orphan_backup), unknown_artifact_bytes = LoadFile(unknown_artifact);
		JournalRecoveryResult orphan_recovery = residue_store.RecoverPendingBatches(auth);
		if(orphan_recovery.status != "journal_orphan_unrecognized" || orphan_recovery.purged_orphans != 0 ||
		   LoadFile(AppendFileName(AppendFileName(residue_root, "papers"), "write-paper.json")) != current_bytes ||
		   LoadFile(orphan_backup) != orphan_backup_bytes || LoadFile(unknown_artifact) != unknown_artifact_bytes ||
		   !DirectoryExists(AppendFileName(residue_root, ".mgr-journal"))) {
			Cerr() << "FAILED: manifest-less orphan or unknown journal bytes were removed\n";
			return false;
		}
		String extra_root = workspace.FixtureRoot("journal-unknown-artifact");
		String extra_papers = AppendFileName(extra_root, "papers");
		String extra_record_path = AppendFileName(extra_papers, "write-paper.json");
		if(!CreatePrivateDirectory(extra_root) || !CreatePrivateDirectory(extra_papers) ||
		   !SaveFile(extra_record_path, current_bytes)) { Cerr() << "FAILED: unknown-artifact fixture setup\n"; return false; }
		DataStore extra_store;
		if(!extra_store.LoadWriteFixtureForTests(extra_root, "synthetic-write-source") ||
		   extra_store.PrepareJournal(auth, "extra-op", stages).status != "prepared") { Cerr() << "FAILED: unknown-artifact journal staging\n"; return false; }
		String extra_op = AppendFileName(AppendFileName(extra_root, ".mgr-journal"), "extra-op");
		String extra_file = AppendFileName(extra_op, "unlisted.bin");
		if(!SaveFile(extra_file, "must remain untouched")) { Cerr() << "FAILED: unknown-artifact fixture file\n"; return false; }
		String extra_manifest_bytes = LoadFile(AppendFileName(extra_op, "manifest.json"));
		String extra_backup_bytes = LoadFile(AppendFileName(extra_op, "backup-0.bin"));
		String extra_payload_bytes = LoadFile(AppendFileName(extra_op, "payload-0.bin"));
		if(extra_store.RecoverPendingBatches(auth).status != "journal_orphan_unrecognized" ||
		   LoadFile(AppendFileName(extra_op, "manifest.json")) != extra_manifest_bytes ||
		   LoadFile(AppendFileName(extra_op, "backup-0.bin")) != extra_backup_bytes ||
		   LoadFile(AppendFileName(extra_op, "payload-0.bin")) != extra_payload_bytes ||
		   LoadFile(extra_file) != "must remain untouched" ||
		   LoadFile(extra_record_path) != current_bytes) {
			Cerr() << "FAILED: unknown artifact beside a valid manifest was not preserved fail-closed\n";
			return false;
		}
		String malformed_root = workspace.FixtureRoot("journal-malformed-manifest");
		String malformed_papers = AppendFileName(malformed_root, "papers");
		String malformed_record_path = AppendFileName(malformed_papers, "write-paper.json");
		String malformed_journal = AppendFileName(malformed_root, ".mgr-journal");
		String malformed_op = AppendFileName(malformed_journal, "malformed-op");
		if(!CreatePrivateDirectory(malformed_root) || !CreatePrivateDirectory(malformed_papers) ||
		   !SaveFile(malformed_record_path, current_bytes) ||
		   !CreatePrivateDirectory(malformed_journal) || !CreatePrivateDirectory(malformed_op) ||
		   !SaveFile(AppendFileName(malformed_op, "manifest.json"), "{ invalid") ||
		   !SaveFile(AppendFileName(malformed_op, "backup-0.bin"), "preserve malformed journal bytes")) return false;
		DataStore malformed_store;
		if(malformed_store.LoadWriteFixtureForTests(malformed_root, "synthetic-write-source") ||
		   !malformed_store.GetDocuments().IsEmpty() || malformed_store.GetSourceDiagnostics().IsEmpty() ||
		   malformed_store.GetSourceDiagnostics()[0] != "journal:recovery_required") return false;
		String malformed_manifest = LoadFile(AppendFileName(malformed_op, "manifest.json"));
		String malformed_backup = LoadFile(AppendFileName(malformed_op, "backup-0.bin"));
		if(malformed_store.RecoverPendingBatches(auth).status != "journal_orphan_unrecognized" ||
		   LoadFile(AppendFileName(malformed_op, "manifest.json")) != malformed_manifest ||
		   LoadFile(AppendFileName(malformed_op, "backup-0.bin")) != malformed_backup ||
		   LoadFile(malformed_record_path) != current_bytes) {
			Cerr() << "FAILED: malformed manifest artifacts were not preserved fail-closed\n";
			return false;
		}
		for(bool pending : { false, true }) {
			String existing_root = workspace.FixtureRoot(pending ? "journal-existing-pending" : "journal-existing-empty");
			if(!CreatePrivateDirectory(existing_root) || !CreatePrivateDirectory(AppendFileName(existing_root, "papers"))) return false;
			String existing_record_path = AppendFileName(AppendFileName(existing_root, "papers"), "write-paper.json");
			SaveFile(existing_record_path, current_bytes);
			DataStore existing_store;
			if(!existing_store.LoadWriteFixtureForTests(existing_root, "synthetic-write-source")) return false;
			String journal_path = AppendFileName(existing_root, ".mgr-journal");
			if(!CreatePrivateDirectory(journal_path) || (pending && !CreatePrivateDirectory(AppendFileName(journal_path, "existing-op")))) return false;
			JournalStageResult refused = existing_store.PrepareJournal(auth, "new-op", stages);
			if(refused.status != "journal_recovery_required" || LoadFile(existing_record_path) != current_bytes ||
			   DataStore::ContentDigest(LoadFile(existing_record_path)) != DataStore::ContentDigest(current_bytes)) {
				Cerr() << "FAILED: pre-existing empty/pending journal did not block a new operation\n";
				return false;
			}
			DataStore existing_catalog;
			if(existing_catalog.LoadFixtureForTests(existing_root) || existing_catalog.GetDocuments().GetCount() != 0) {
				Cerr() << "FAILED: pre-existing empty/pending journal did not fail catalog closed\n";
				return false;
			}
		}
		String invalid_id_root = workspace.FixtureRoot("journal-invalid-id");
		if(!CreatePrivateDirectory(invalid_id_root) || !CreatePrivateDirectory(AppendFileName(invalid_id_root, "papers"))) {
			Cerr() << "FAILED: could not create synthetic invalid journal id root\n";
			return false;
		}
		SaveFile(AppendFileName(AppendFileName(invalid_id_root, "papers"), "write-paper.json"), current_bytes);
		DataStore invalid_id_store;
		if(!invalid_id_store.LoadWriteFixtureForTests(invalid_id_root, "synthetic-write-source") ||
		   invalid_id_store.PrepareJournal(auth, "../escape", stages).status != "invalid_request" ||
		   DirectoryExists(AppendFileName(invalid_id_root, ".mgr-journal"))) {
			Cerr() << "FAILED: unsafe journal operation identifier was accepted\n";
			return false;
		}
		// Exercise the real batch producer on an explicitly writable synthetic root.
		String batch_root = workspace.FixtureRoot("batch-producer");
		String batch_papers = AppendFileName(batch_root, "papers");
		if(!CreatePrivateDirectory(batch_root) || !CreatePrivateDirectory(batch_papers)) {
			Cerr() << "FAILED: synthetic batch root setup\n";
			return false;
		}
		ValueMap batch_a = record, batch_b = concurrent_record;
		String batch_a_path = AppendFileName(batch_papers, "batch-a.json");
		String batch_b_path = AppendFileName(batch_papers, "batch-b.json");
		if(!SaveFile(batch_a_path, AsJSON(batch_a, true)) || !SaveFile(batch_b_path, AsJSON(batch_b, true))) {
			Cerr() << "FAILED: synthetic batch record setup\n";
			return false;
		}
		DataStore batch_snapshot, batch_writer;
		if(!batch_snapshot.LoadFixtureForTests(batch_root) || !batch_writer.LoadWriteFixtureForTests(batch_root, "synthetic-write-source")) {
			Cerr() << "FAILED: synthetic batch datastore setup\n";
			return false;
		}
		const DataDocument *batch_a_doc = batch_snapshot.FindDocument("synthetic-workspace:write-paper");
		const DataDocument *batch_b_doc = batch_snapshot.FindDocument("synthetic-workspace:concurrent-paper");
		if(!batch_a_doc || !batch_b_doc) { Cerr() << "FAILED: synthetic batch documents missing (" << batch_snapshot.GetDocuments().GetCount() << ")\n"; return false; }
		DataBatchChange ba, bb;
		ba.qualified_id = batch_a_doc->qualified_id; ba.expected_revision = batch_a_doc->revision;
		ba.expected_source_revision = batch_a_doc->source_revision; ba.proposed = batch_a; ba.proposed.Set("title", "Batch updated A");
		bb.qualified_id = batch_b_doc->qualified_id; bb.expected_revision = batch_b_doc->revision;
		bb.expected_source_revision = batch_b_doc->source_revision; bb.proposed = batch_b; bb.proposed.Set("title", "Batch updated B");
		Vector<DataBatchChange> batch_changes; batch_changes.Add(ba); batch_changes.Add(bb);
		DataBatchResult batch_result = batch_writer.PublishBatch(auth, "batch-success", batch_changes);
		JournalManifest batch_manifest;
		String batch_manifest_path = AppendFileName(AppendFileName(AppendFileName(batch_root, ".mgr-journal"), "batch-success"), "manifest.json");
		if(batch_result.status != "committed" || batch_result.changed_ids.GetCount() != 2 || batch_result.revisions.GetCount() != 2 ||
		   !DataStore::ParseJournalManifest(LoadFile(batch_manifest_path), batch_manifest) || batch_manifest.state != "committed" ||
		   MapGetString(ValueMap(ParseJSON(LoadFile(batch_a_path))), "title") != "Batch updated A" ||
		   MapGetString(ValueMap(ParseJSON(LoadFile(batch_b_path))), "title") != "Batch updated B") {
			bool parsed = DataStore::ParseJournalManifest(LoadFile(batch_manifest_path), batch_manifest);
			Cerr() << "FAILED: batch producer publication check (status=" << batch_result.status << ", changed="
			       << batch_result.changed_ids.GetCount() << ", parsed=" << parsed << ", manifest=" << batch_manifest.state
			       << ", a=" << MapGetString(ValueMap(ParseJSON(LoadFile(batch_a_path))), "title")
			       << ", b=" << MapGetString(ValueMap(ParseJSON(LoadFile(batch_b_path))), "title") << ")\n";
			return false;
		}
		DataBatchResult pending_retry = batch_writer.PublishBatch(auth, "batch-success", batch_changes);
		if(pending_retry.status != "journal_recovery_required" || !pending_retry.changed_ids.IsEmpty() || !pending_retry.revisions.IsEmpty()) {
			Cerr() << "FAILED: pending journal allowed stale receipt disclosure\n";
			return false;
		}
		DataStore pending_catalog;
		if(pending_catalog.LoadFixtureForTests(batch_root) || pending_catalog.GetDocuments().GetCount() != 0) {
			Cerr() << "FAILED: catalog exposed a committed batch before recovery/notification cleanup\n";
			return false;
		}
		String restart_root = workspace.FixtureRoot("batch-restart-rollback");
		String restart_papers = AppendFileName(restart_root, "papers");
		if(!CreatePrivateDirectory(restart_root) || !CreatePrivateDirectory(restart_papers)) return false;
		String restart_a_path = AppendFileName(restart_papers, "batch-a.json");
		String restart_b_path = AppendFileName(restart_papers, "batch-b.json");
		String restart_a_old = AsJSON(batch_a, true), restart_b_old = AsJSON(batch_b, true);
		if(!SaveFile(restart_a_path, restart_a_old) || !SaveFile(restart_b_path, restart_b_old)) return false;
		DataStore restart_snapshot, restart_writer;
		if(!restart_snapshot.LoadFixtureForTests(restart_root) ||
		   !restart_writer.LoadWriteFixtureForTests(restart_root, "synthetic-write-source")) return false;
		const DataDocument *restart_a_doc = restart_snapshot.FindDocument("synthetic-workspace:write-paper");
		const DataDocument *restart_b_doc = restart_snapshot.FindDocument("synthetic-workspace:concurrent-paper");
		if(!restart_a_doc || !restart_b_doc) return false;
		DataBatchChange restart_a = ba, restart_b = bb;
		restart_a.expected_revision = restart_a_doc->revision;
		restart_a.expected_source_revision = restart_a_doc->source_revision;
		restart_b.expected_revision = restart_b_doc->revision;
		restart_b.expected_source_revision = restart_b_doc->source_revision;
		Vector<DataBatchChange> restart_changes; restart_changes.Add(restart_a); restart_changes.Add(restart_b);
		DataBatchResult crashed_batch = restart_writer.PublishBatch(auth, "batch-crash-restart", restart_changes,
		                                                           DATA_WRITE_FAIL_BATCH_AFTER_REPLACE);
		String restart_manifest_path = AppendFileName(AppendFileName(AppendFileName(restart_root, ".mgr-journal"),
		                                                            "batch-crash-restart"), "manifest.json");
		JournalManifest restart_manifest;
		if(crashed_batch.status != "uncommitted" || LoadFile(restart_a_path) == restart_a_old ||
		   LoadFile(restart_b_path) != restart_b_old ||
		   !DataStore::ParseJournalManifest(LoadFile(restart_manifest_path), restart_manifest) ||
		   restart_manifest.state != "committing" || restart_manifest.workspace_id != auth.workspace_id) {
			Cerr() << "FAILED: T43 after-replacement failpoint did not leave a bound committing journal\n";
			return false;
		}
		DataStore fresh_recovery;
		if(fresh_recovery.LoadWriteFixtureForTests(restart_root, "synthetic-write-source") ||
		   !fresh_recovery.GetDocuments().IsEmpty() || fresh_recovery.GetSourceDiagnostics().IsEmpty() ||
		   fresh_recovery.GetSourceDiagnostics()[0] != "journal:recovery_required") {
			Cerr() << "FAILED: fresh T43 recovery store did not preserve catalog fail-closed behavior\n";
			return false;
		}
		DataWriteAuthorization wrong_workspace = auth;
		wrong_workspace.workspace_id = "other-workspace";
		if(fresh_recovery.RecoverPendingBatches(wrong_workspace).status != "scope_denied" ||
		   fresh_recovery.RecoverPendingBatches(auth, DATA_WRITE_FAIL_RECOVERY_BEFORE_CLEANUP).status != "recovery_conflict" ||
		   LoadFile(restart_a_path) != restart_a_old) {
			Cerr() << "FAILED: bound workspace denial or post-rollback recovery failpoint\n";
			return false;
		}
		JournalManifest persisted_rollback;
		if(!DataStore::ParseJournalManifest(LoadFile(restart_manifest_path), persisted_rollback) ||
		   persisted_rollback.state != "rolled-back" || LoadFile(restart_b_path) != restart_b_old) {
			Cerr() << "FAILED: committing rollback state was not durably persisted before interruption\n";
			return false;
		}
		JournalRecoveryResult restart_retry = fresh_recovery.RecoverPendingBatches(auth);
		DataStore restart_reloaded;
		if(restart_retry.status != "recovered" || !restart_reloaded.LoadFixtureForTests(restart_root) ||
		   restart_reloaded.FindDocument("synthetic-workspace:write-paper")->revision != "1" ||
		   restart_reloaded.FindDocument("synthetic-workspace:concurrent-paper")->revision != "1" ||
		   LoadFile(restart_a_path) != restart_a_old || LoadFile(restart_b_path) != restart_b_old) {
			Cerr() << "FAILED: producer-driven fresh-store rollback/retry did not reload original records\n";
			return false;
		}
		Cout() << "ok    T43 producer crash-after-replacement recovers on a fresh store, persists rolled-back, and retries\n";
		JournalRecoveryResult batch_replay = batch_writer.RecoverPendingBatches(auth);
		DataStore readable_batch;
		if(batch_replay.status != "recovered" || !readable_batch.LoadFixtureForTests(batch_root) ||
		   readable_batch.FindDocument(ba.qualified_id)->revision != "2" || readable_batch.FindDocument(bb.qualified_id)->revision != "2") {
			Cerr() << "FAILED: committed batch replay did not restore readable revisioned records\n";
			return false;
		}
		DataStore reloaded_batch_writer;
		if(!reloaded_batch_writer.LoadWriteFixtureForTests(batch_root, "synthetic-write-source")) return false;
		String committed_batch_a_bytes = LoadFile(batch_a_path), committed_batch_b_bytes = LoadFile(batch_b_path);
		DataBatchResult batch_retry = reloaded_batch_writer.PublishBatch(auth, "batch-success", batch_changes);
		DataBatchChange altered_b = bb; altered_b.proposed.Set("title", "Different retry payload");
		Vector<DataBatchChange> altered_batch; altered_batch.Add(ba); altered_batch.Add(altered_b);
		DataBatchResult batch_key_conflict = reloaded_batch_writer.PublishBatch(auth, "batch-success", altered_batch);
		if(batch_retry.status != "already_committed" || batch_retry.changed_ids.GetCount() != 2 ||
		   batch_retry.changed_ids[0] != ba.qualified_id || batch_retry.changed_ids[1] != bb.qualified_id ||
		   batch_retry.revisions.GetCount() != 2 || batch_retry.revisions[0] != "2" || batch_retry.revisions[1] != "2" ||
		   batch_key_conflict.status != "idempotency_conflict" ||
		   LoadFile(batch_a_path) != committed_batch_a_bytes || LoadFile(batch_b_path) != committed_batch_b_bytes) {
			Cerr() << "FAILED: post-recovery batch retry returned noncanonical receipt IDs/revisions or operation-ID payload conflict\n";
			return false;
		}
		Cout() << "ok    pending-journal retry withholds receipt; authorized recovery enables canonical idempotent retry\n";
		ValueMap edited_after_commit = ValueMap(ParseJSON(LoadFile(batch_a_path)));
		edited_after_commit.Set("title", "Later edit after receipt");
		if(!SaveFile(batch_a_path, AsJSON(edited_after_commit, true))) return false;
		DataStore edited_receipt_writer;
		if(!edited_receipt_writer.LoadWriteFixtureForTests(batch_root, "synthetic-write-source")) return false;
		DataBatchResult replay_after_later_edit = edited_receipt_writer.PublishBatch(auth, "batch-success", batch_changes);
		DataSourceRegistration registration_test;
		registration_test.id = "synthetic-write-source"; registration_test.root = batch_root;
		registration_test.enabled = true; registration_test.write_enabled = true; registration_test.read_only = false;
		DataSourceRegistration changed_registration = registration_test; changed_registration.write_enabled = false;
		DataSourceRegistration redirected_registration = registration_test; redirected_registration.root = batch_root + "-redirected";
		if(replay_after_later_edit.status != "already_committed" ||
		   !DataStore::RegistrationMatches(registration_test, "synthetic-write-source", NormalizePath(batch_root)) ||
		   DataStore::RegistrationMatches(changed_registration, "synthetic-write-source", NormalizePath(batch_root)) ||
		   DataStore::RegistrationMatches(redirected_registration, "synthetic-write-source", NormalizePath(batch_root))) {
			Cerr() << "FAILED: durable receipt replay depended on current record bytes or registration matcher accepted changed capability\n";
			return false;
		}
		for(DataWriteTestFailpoint failpoint : { DATA_WRITE_FAIL_BATCH_TEST_REVOKE_BEFORE_RECEIPT,
		                                       DATA_WRITE_FAIL_BATCH_TEST_REDIRECT_BEFORE_RECEIPT }) {
			DataStore denied_receipt_writer;
			if(!denied_receipt_writer.LoadWriteFixtureForTests(batch_root, "synthetic-write-source")) return false;
			DataBatchResult denied_receipt = denied_receipt_writer.PublishBatch(auth, "batch-success", batch_changes, failpoint);
			if(denied_receipt.status != "source_capability_changed" || !denied_receipt.changed_ids.IsEmpty() ||
			   !denied_receipt.revisions.IsEmpty()) {
				Cerr() << "FAILED: revoked/redirected registration exposed receipt outcome before lookup\n";
				return false;
			}
		}
		Cout() << "ok    PublishBatch revalidates revoked/redirected registration before receipt lookup without disclosure\n";
		ValueMap edited_batch_b = ValueMap(ParseJSON(LoadFile(batch_b_path)));
		ValueMap edited_envelope_b = MapGet(edited_batch_b, "envelope");
		edited_envelope_b.Set("last_batch_operation_id", "fabricated-operation");
		edited_envelope_b.Set("last_batch_request_digest", "fabricated-digest");
		edited_batch_b.Set("envelope", edited_envelope_b);
		SaveFile(batch_b_path, AsJSON(edited_batch_b, true));
		DataBatchResult fabricated_retry = edited_receipt_writer.PublishBatch(auth, "fabricated-operation", batch_changes);
		if(fabricated_retry.status == "already_committed") {
			Cerr() << "FAILED: editable source record metadata was trusted as a durable receipt\n";
			return false;
		}
		Cout() << "ok    T46 persisted batch receipt supports retry after recovery/reload and rejects key reuse\n";
		for(DataWriteTestFailpoint failpoint : { DATA_WRITE_FAIL_BATCH_TEST_CHANGE_REFERENCE,
		                                       DATA_WRITE_FAIL_BATCH_TEST_DELETE_REFERENCE }) {
			String race_root = workspace.FixtureRoot("batch-ref-race-" + AsString((int)failpoint));
			String race_papers = AppendFileName(race_root, "papers"), race_analyses = AppendFileName(race_root, "analyses");
			if(!CreatePrivateDirectory(race_root) || !CreatePrivateDirectory(race_papers) || !CreatePrivateDirectory(race_analyses)) return false;
			String race_paper_path = AppendFileName(race_papers, "write-paper.json");
			String race_analysis_path = AppendFileName(race_analyses, "analysis-1.json");
			if(!SaveFile(race_paper_path, AsJSON(record, true)) || !SaveFile(race_analysis_path, AsJSON(analysis, true))) return false;
			DataStore race_snapshot, race_writer;
			if(!race_snapshot.LoadFixtureForTests(race_root) || !race_writer.LoadWriteFixtureForTests(race_root, "synthetic-write-source")) return false;
			const DataDocument *race_doc = race_snapshot.FindDocument("synthetic-workspace:analysis-1");
			if(!race_doc || !race_doc->valid) return false;
			DataBatchChange change;
			change.qualified_id = race_doc->qualified_id; change.expected_revision = race_doc->revision;
			change.expected_source_revision = race_doc->source_revision; change.proposed = analysis;
			change.proposed.Set("repo", "race-update");
			Vector<DataBatchChange> race_changes; race_changes.Add(change);
			String old_analysis_bytes = LoadFile(race_analysis_path);
			DataBatchResult race_result = race_writer.PublishBatch(auth, "reference-race", race_changes, failpoint);
			DataStore clean_catalog;
			if(race_result.status != "conflict" || LoadFile(race_analysis_path) != old_analysis_bytes ||
			   DirectoryExists(AppendFileName(race_root, ".mgr-journal")) || !clean_catalog.LoadFixtureForTests(race_root)) {
				Cerr() << "FAILED: external reference change/deletion did not abort and clean prepared journal\n";
				return false;
			}
		}
		Cout() << "ok    T46 reference change/deletion aborts cleanly without batch mutation or catalog outage\n";
		String fail_root = workspace.FixtureRoot("batch-precommit-failure");
		String fail_papers = AppendFileName(fail_root, "papers");
		if(!CreatePrivateDirectory(fail_root) || !CreatePrivateDirectory(fail_papers)) return false;
		String fail_a_path = AppendFileName(fail_papers, "batch-a.json"), fail_b_path = AppendFileName(fail_papers, "batch-b.json");
		if(!SaveFile(fail_a_path, AsJSON(batch_a, true)) || !SaveFile(fail_b_path, AsJSON(batch_b, true))) return false;
		String fail_a_old = LoadFile(fail_a_path), fail_b_old = LoadFile(fail_b_path);
		DataStore fail_snapshot, fail_writer;
		if(!fail_snapshot.LoadFixtureForTests(fail_root) || !fail_writer.LoadWriteFixtureForTests(fail_root, "synthetic-write-source")) return false;
		const DataDocument *fail_a_doc = fail_snapshot.FindDocument("synthetic-workspace:write-paper"),
		                  *fail_b_doc = fail_snapshot.FindDocument("synthetic-workspace:concurrent-paper");
		if(!fail_a_doc || !fail_b_doc) return false;
		DataBatchChange fail_a, fail_b;
		fail_a.qualified_id = fail_a_doc->qualified_id; fail_a.expected_revision = fail_a_doc->revision;
		fail_a.expected_source_revision = fail_a_doc->source_revision; fail_a.proposed = batch_a; fail_a.proposed.Set("title", "Should roll back A");
		fail_b.qualified_id = fail_b_doc->qualified_id; fail_b.expected_revision = fail_b_doc->revision;
		fail_b.expected_source_revision = fail_b_doc->source_revision; fail_b.proposed = batch_b; fail_b.proposed.Set("title", "Should roll back B");
		Vector<DataBatchChange> fail_changes; fail_changes.Add(fail_a); fail_changes.Add(fail_b);
		DataBatchResult precommit_failure = fail_writer.PublishBatch(auth, "batch-before-replace", fail_changes, DATA_WRITE_FAIL_BATCH_BEFORE_REPLACE);
		JournalRecoveryResult precommit_recovery = fail_writer.RecoverPendingBatches(auth);
		if(precommit_failure.status != "uncommitted" || precommit_recovery.status != "recovered" ||
		   LoadFile(fail_a_path) != fail_a_old || LoadFile(fail_b_path) != fail_b_old || DirectoryExists(AppendFileName(fail_root, ".mgr-journal"))) {
			Cerr() << "FAILED: batch pre-commit failure mutated records or left prepared/committing journal unresolved\n";
			return false;
		}
		DataBatchResult registration_changed = fail_writer.PublishBatch(auth, "registration-changed", fail_changes, DATA_WRITE_FAIL_BATCH_TEST_REGISTRATION_CHANGE);
		String pending_manifest = AppendFileName(AppendFileName(AppendFileName(fail_root, ".mgr-journal"), "registration-changed"), "manifest.json");
		JournalManifest registration_manifest;
		DataSourceRegistration fail_registration;
		fail_registration.id = "synthetic-write-source"; fail_registration.root = fail_root;
		fail_registration.enabled = true; fail_registration.write_enabled = true; fail_registration.read_only = false;
		if(registration_changed.status != "source_capability_changed_recovery_required" ||
		   !registration_changed.changed_ids.IsEmpty() || !registration_changed.revisions.IsEmpty() ||
		   LoadFile(fail_a_path) != fail_a_old || LoadFile(fail_b_path) != fail_b_old ||
		   !DataStore::ParseJournalManifest(LoadFile(pending_manifest), registration_manifest) || registration_manifest.state != "prepared" ||
		   DataStore::RegistrationMatches(fail_writer.synthetic_registration, "synthetic-write-source", NormalizePath(fail_root))) {
			Cerr() << "FAILED: registration change at the final lock boundary replaced records or discarded its fail-closed journal\n";
			return false;
		}
		fail_writer.SetSyntheticRegistrationForTests(fail_registration);
		if(fail_writer.RecoverPendingBatches(auth).status != "recovered") {
			Cerr() << "FAILED: authorized recovery after restoring synthetic registration\n";
			return false;
		}
		DataBatchResult stale_record = fail_writer.PublishBatch(auth, "batch-stale-record", fail_changes, DATA_WRITE_FAIL_BATCH_TEST_CHANGE_RECORD);
		String external_bytes = fail_a_old + " ";
		DataStore stale_clean_catalog;
		if(stale_record.status != "conflict" || LoadFile(fail_a_path) != external_bytes || LoadFile(fail_b_path) != fail_b_old ||
		   DirectoryExists(AppendFileName(fail_root, ".mgr-journal")) || !stale_clean_catalog.LoadFixtureForTests(fail_root)) {
			Cerr() << "FAILED: stale changed-record conflict did not preserve external bytes and clean its prepared journal\n";
			return false;
		}
		Cout() << "ok    T46 pre-commit failure is mutation-free; stale conflict preserves external bytes and cleans journal\n";
		String bounded_root = workspace.FixtureRoot("batch-receipt-bounds");
		String bounded_papers = AppendFileName(bounded_root, "papers");
		if(!CreatePrivateDirectory(bounded_root) || !CreatePrivateDirectory(bounded_papers)) return false;
		String bounded_record_path = AppendFileName(bounded_papers, "write-paper.json");
		if(!SaveFile(bounded_record_path, AsJSON(batch_a, true))) return false;
		String commits_dir = AppendFileName(bounded_root, ".mgr-commits");
		if(!CreatePrivateDirectory(commits_dir)) return false;
		ValueArray full_rows;
		for(int i = 0; i < 128; i++) {
			String digest; for(int n = 0; n < 64; n++) digest.Cat('b');
			ValueArray ids, revisions; ids.Add("synthetic-workspace:write-paper"); revisions.Add("2");
			ValueMap row; row.Add("operation_id", "reserved-" + AsString(i)); row.Add("source_id", "synthetic-write-source");
			row.Add("workspace_id", "synthetic-workspace"); row.Add("request_digest", digest);
			row.Add("changed_ids", ids); row.Add("revisions", revisions); full_rows.Add(row);
		}
		ValueMap full_ledger; full_ledger.Add("version", 1); full_ledger.Add("receipts", full_rows);
		String receipt_file = AppendFileName(commits_dir, "receipts.json");
		if(!SaveFile(receipt_file, AsJSON(full_ledger, true)) || chmod(receipt_file.ToStd().c_str(), 0600) != 0) return false;
		String bounded_old = LoadFile(bounded_record_path);
		DataStore bounded_writer;
		if(!bounded_writer.LoadWriteFixtureForTests(bounded_root, "synthetic-write-source")) return false;
		const DataDocument *bounded_doc = bounded_writer.FindDocument("synthetic-workspace:write-paper");
		if(!bounded_doc) return false;
		DataBatchChange bounded_change;
		bounded_change.qualified_id = bounded_doc->qualified_id; bounded_change.expected_revision = bounded_doc->revision;
		bounded_change.expected_source_revision = bounded_doc->source_revision; bounded_change.proposed = batch_a; bounded_change.proposed.Set("title", "Capacity test");
		Vector<DataBatchChange> bounded_changes; bounded_changes.Add(bounded_change);
		DataBatchResult full_result = bounded_writer.PublishBatch(auth, "capacity-operation", bounded_changes);
		if(full_result.status != "receipt_store_full" || LoadFile(bounded_record_path) != bounded_old ||
		   DirectoryExists(AppendFileName(bounded_root, ".mgr-journal"))) {
			Cerr() << "FAILED: receipt capacity did not reject before journal/record mutation\n";
			return false;
		}
		if(!SaveFile(receipt_file, "{ malformed receipt ledger") || chmod(receipt_file.ToStd().c_str(), 0600) != 0) return false;
		DataBatchResult corrupt_result = bounded_writer.PublishBatch(auth, "corrupt-operation", bounded_changes);
		if(corrupt_result.status != "receipt_store_corrupt" || LoadFile(bounded_record_path) != bounded_old ||
		   DirectoryExists(AppendFileName(bounded_root, ".mgr-journal"))) {
			Cerr() << "FAILED: malformed receipt ledger did not fail closed before source mutation\n";
			return false;
		}
		Cout() << "ok    T46 receipt ledger rejects its 129th operation and corruption before journal or record mutation\n";
		Cout() << "ok    T43 producer stages, commits two records, and T45 replays the committed batch\n";
		String concurrent_root = workspace.FixtureRoot("batch-same-id-concurrent");
		String concurrent_papers = AppendFileName(concurrent_root, "papers");
		if(!CreatePrivateDirectory(concurrent_root) || !CreatePrivateDirectory(concurrent_papers) ||
		   !SaveFile(AppendFileName(concurrent_papers, "batch-a.json"), AsJSON(batch_a, true)) ||
		   !SaveFile(AppendFileName(concurrent_papers, "batch-b.json"), AsJSON(batch_b, true))) return false;
		DataStore concurrent_snapshot, concurrent_writer_a, concurrent_writer_b;
		if(!concurrent_snapshot.LoadFixtureForTests(concurrent_root) ||
		   !concurrent_writer_a.LoadWriteFixtureForTests(concurrent_root, "synthetic-write-source") ||
		   !concurrent_writer_b.LoadWriteFixtureForTests(concurrent_root, "synthetic-write-source")) return false;
		const DataDocument *concurrent_doc_a = concurrent_snapshot.FindDocument("synthetic-workspace:write-paper");
		const DataDocument *concurrent_doc_b = concurrent_snapshot.FindDocument("synthetic-workspace:concurrent-paper");
		if(!concurrent_doc_a || !concurrent_doc_b) return false;
		DataBatchChange concurrent_change_a, concurrent_change_b;
		concurrent_change_a.qualified_id = concurrent_doc_a->qualified_id;
		concurrent_change_a.expected_revision = concurrent_doc_a->revision;
		concurrent_change_a.expected_source_revision = concurrent_doc_a->source_revision;
		concurrent_change_a.proposed = batch_a; concurrent_change_a.proposed.Set("title", "Concurrent A");
		concurrent_change_b.qualified_id = concurrent_doc_b->qualified_id;
		concurrent_change_b.expected_revision = concurrent_doc_b->revision;
		concurrent_change_b.expected_source_revision = concurrent_doc_b->source_revision;
		concurrent_change_b.proposed = batch_b; concurrent_change_b.proposed.Set("title", "Concurrent B");
		Vector<DataBatchChange> concurrent_changes;
		concurrent_changes.Add(concurrent_change_a); concurrent_changes.Add(concurrent_change_b);
		DataBatchResult concurrent_result_a, concurrent_result_b;
		Thread concurrent_thread_a, concurrent_thread_b;
		concurrent_thread_a.Run([&] { concurrent_result_a = concurrent_writer_a.PublishBatch(auth, "same-operation-id", concurrent_changes); });
		concurrent_thread_b.Run([&] { concurrent_result_b = concurrent_writer_b.PublishBatch(auth, "same-operation-id", concurrent_changes); });
		concurrent_thread_a.Wait(); concurrent_thread_b.Wait();
		bool serialized_pair = (concurrent_result_a.status == "committed" && concurrent_result_b.status == "journal_recovery_required") ||
		                       (concurrent_result_b.status == "committed" && concurrent_result_a.status == "journal_recovery_required");
		JournalRecoveryResult concurrent_recovery = concurrent_writer_a.RecoverPendingBatches(auth);
		DataStore& concurrent_retry_writer = concurrent_result_a.status == "committed" ? concurrent_writer_b : concurrent_writer_a;
		DataBatchResult concurrent_retry = concurrent_retry_writer.PublishBatch(auth, "same-operation-id", concurrent_changes);
		DataStore concurrent_after;
		if(!serialized_pair || concurrent_retry.status != "already_committed" || concurrent_recovery.status != "recovered" || !concurrent_after.LoadFixtureForTests(concurrent_root) ||
		   concurrent_after.FindDocument(concurrent_change_a.qualified_id)->revision != "2" ||
		   concurrent_after.FindDocument(concurrent_change_b.qualified_id)->revision != "2") {
			Cerr() << "FAILED: concurrent same-ID attempt/receipt did not serialize safely ("
			       << concurrent_result_a.status << ", " << concurrent_result_b.status << ", retry=" << concurrent_retry.status << ")\n";
			return false;
		}
		Cout() << "ok    single source lock serializes concurrent same-ID attempts; pending journal suppresses receipt disclosure\n";
		String final_root = workspace.FixtureRoot("batch-finalization");
		String final_data = AppendFileName(final_root, "papers");
		if(!CreatePrivateDirectory(final_root) || !CreatePrivateDirectory(final_data) ||
		   !CreatePrivateDirectory(AppendFileName(final_root, "goals"))) return false;
		String final_a_path = AppendFileName(final_data, "batch-a.json");
		String final_b_path = AppendFileName(final_data, "batch-b.json");
		ValueMap foreign_record = batch_a;
		foreign_record.Set("id", "foreign-paper");
		ValueMap final_foreign_envelope = MapGet(foreign_record, "envelope");
		final_foreign_envelope.Set("workspace_id", "other-workspace");
		final_foreign_envelope.Set("stable_id", "foreign-paper");
		foreign_record.Set("envelope", final_foreign_envelope);
		if(!SaveFile(final_a_path, AsJSON(batch_a, true)) || !SaveFile(final_b_path, AsJSON(batch_b, true)) ||
		   !SaveFile(AppendFileName(final_data, "foreign.json"), AsJSON(foreign_record, true))) return false;
		DataSourceRegistration prior_registration = app.test_registration;
		bool prior_provider = app.test_registration_provider;
		app.test_registration_provider = true;
		app.test_registration.id = "synthetic-write-source";
		app.test_registration.root = NormalizePath(final_root);
		app.test_registration.enabled = true;
		app.test_registration.read_only = false;
		app.test_registration.write_enabled = true;
		app.test_registration_change_point = 0;
		if(!app.RefreshCatalog()) return false;
		CatalogReadHandle old_final_pin = app.PinLatestCatalog();
		const DataDocument* final_a = old_final_pin->data.FindDocument("synthetic-workspace:write-paper");
		const DataDocument* final_b = old_final_pin->data.FindDocument("synthetic-workspace:concurrent-paper");
		const DataDocument* foreign_doc = old_final_pin->data.FindDocument("other-workspace:foreign-paper");
		if(!final_a || !final_b || !foreign_doc || !foreign_doc->valid || !foreign_doc->validated) return false;
		DataBatchChange final_change_a, final_change_b;
		final_change_a.qualified_id = final_a->qualified_id; final_change_a.expected_revision = final_a->revision;
		final_change_a.expected_source_revision = final_a->source_revision; final_change_a.proposed = batch_a;
		final_change_a.proposed.Set("title", "Finalized A");
		final_change_b.qualified_id = final_b->qualified_id; final_change_b.expected_revision = final_b->revision;
		final_change_b.expected_source_revision = final_b->source_revision; final_change_b.proposed = batch_b;
		final_change_b.proposed.Set("title", "Finalized B");
		Vector<DataBatchChange> final_changes; final_changes.Add(final_change_b); final_changes.Add(final_change_a);
		app.test_mode = false;
		UserSession outside_test_session = SystemAuthService::CreateTestSessionForTests();
		app.test_mode = true;
		UserSession live_write_session = SystemAuthService::CreateTestSessionForTests();
		if(!live_write_session.is_authenticated || live_write_session.token.IsEmpty()) return false;
		String final_a_before_auth = LoadFile(final_a_path), final_b_before_auth = LoadFile(final_b_path);
		UserSession forged_session = live_write_session;
		forged_session.username = "forged-operator";
		forged_session.is_admin = false;
		UserSession missing_session = SystemAuthService::CreateTestSessionForTests();
		SystemAuthService::ExpireSessionForTest(missing_session.token);
		UserSession unauthenticated_session;
		BatchFinalizationResult forged_denied = SystemAuthService::FinalizeBatchForSession(app, forged_session,
			"synthetic-write-source", "synthetic-workspace", "finalize-forged", final_changes);
		BatchFinalizationResult expired_denied = SystemAuthService::FinalizeBatchForSession(app, missing_session,
			"synthetic-write-source", "synthetic-workspace", "finalize-expired", final_changes);
		BatchFinalizationResult unauthenticated_denied = SystemAuthService::FinalizeBatchForSession(app, unauthenticated_session,
			"synthetic-write-source", "synthetic-workspace", "finalize-anonymous", final_changes);
		BatchFinalizationResult wrong_source_denied = SystemAuthService::FinalizeBatchForSession(app, live_write_session,
			"wrong-synthetic-source", "synthetic-workspace", "finalize-wrong-source", final_changes);
		BatchFinalizationResult wrong_workspace_denied = SystemAuthService::FinalizeBatchForSession(app, live_write_session,
			"synthetic-write-source", "wrong-synthetic-workspace", "finalize-wrong-workspace", final_changes);
		DataSourceRegistration write_registration = app.test_registration;
		app.test_registration.read_only = true;
		if(!app.RefreshCatalog()) return false;
		BatchFinalizationResult readonly_denied = SystemAuthService::FinalizeBatchForSession(app, live_write_session,
			"synthetic-write-source", "synthetic-workspace", "finalize-readonly", final_changes);
		app.test_registration = write_registration;
		if(!outside_test_session.token.IsEmpty() || !app.RefreshCatalog() || forged_denied.committed || expired_denied.committed ||
		   unauthenticated_denied.committed || wrong_source_denied.committed || wrong_workspace_denied.committed ||
		   readonly_denied.committed || LoadFile(final_a_path) != final_a_before_auth ||
		   LoadFile(final_b_path) != final_b_before_auth) {
			Cerr() << "FAILED: authenticated batch service accepted an invalid principal/scope/source or mutated on denial\n";
			return false;
		}
		BatchFinalizationResult finalized = SystemAuthService::FinalizeBatchForSession(app, live_write_session,
			"synthetic-write-source", "synthetic-workspace", "finalize-success", final_changes);
		Cout() << "ok    T49 authenticated live-session service gates batch finalization; invalid scope/session denials are mutation-free\n";
		DataWriteAuthorization final_auth;
		final_auth.authenticated = true; final_auth.action_scope = "record:write";
		final_auth.principal = "synthetic-operator"; final_auth.source_id = "synthetic-write-source";
		final_auth.workspace_id = "synthetic-workspace";
		CatalogReadHandle new_final_pin = app.PinLatestCatalog();
		if(!finalized.committed || finalized.status != "committed" || finalized.notification_status != "published" ||
		   finalized.event.GetCount() == 0 || finalized.changed_ids.GetCount() != 2 ||
		   finalized.changed_ids[0] > finalized.changed_ids[1] || finalized.generation != new_final_pin->generation ||
		   old_final_pin->generation == new_final_pin->generation ||
		   old_final_pin->data.FindDocument(final_change_a.qualified_id)->revision != "1" ||
		   new_final_pin->data.FindDocument(final_change_a.qualified_id)->revision != "2") {
			Cerr() << "FAILED: app-level batch finalization did not publish one stable new generation and event\n";
			return false;
		}
		ValueArray event_ids = finalized.event.Get("changed_ids");
		ValueArray event_revisions = finalized.event.Get("revisions");
		if(event_ids.GetCount() != finalized.changed_ids.GetCount() ||
		   event_revisions.GetCount() != finalized.revisions.GetCount() ||
		   MapGetString(finalized.event, "source_revision") != finalized.source_revision) return false;
		for(int i = 0; i < finalized.changed_ids.GetCount(); i++) {
			const DataDocument* document = new_final_pin->data.FindDocument(finalized.changed_ids[i]);
			if((String)event_ids[i] != finalized.changed_ids[i] || (String)event_revisions[i] != finalized.revisions[i] ||
			   !document || document->revision != finalized.revisions[i]) {
				Cerr() << "FAILED: returned event ID/revision pairing differs from durable result/catalog\n";
				return false;
			}
		}
		BatchFinalizationResult already_finalized = app.FinalizeBatch(final_auth, "finalize-success", final_changes);
		if(!already_finalized.committed || already_finalized.status != "already_committed" ||
		   already_finalized.changed_ids != finalized.changed_ids || already_finalized.revisions != finalized.revisions) {
			Cerr() << "FAILED: app-level already-committed retry was not deterministic\n";
			return false;
		}
		String final_a_before_scope = LoadFile(final_a_path), final_b_before_scope = LoadFile(final_b_path);
		DataWriteAuthorization wrong_workspace_auth = final_auth;
		wrong_workspace_auth.workspace_id = "other-synthetic-workspace";
		DataWriteAuthorization wrong_source_auth = final_auth;
		wrong_source_auth.source_id = "other-synthetic-source";
		BatchFinalizationResult workspace_denied = app.FinalizeBatch(wrong_workspace_auth, "scope-workspace", final_changes);
		BatchFinalizationResult source_denied = app.FinalizeBatch(wrong_source_auth, "scope-source", final_changes);
		if(workspace_denied.committed || source_denied.committed || workspace_denied.status != "scope_denied" ||
		   source_denied.status != "scope_denied" || LoadFile(final_a_path) != final_a_before_scope ||
		   LoadFile(final_b_path) != final_b_before_scope) {
			Cerr() << "FAILED: FinalizeBatch accepted workspace/source scope mismatch or mutated records\n";
			return false;
		}
		DataBatchChange foreign_change;
		foreign_change.qualified_id = foreign_doc->qualified_id;
		foreign_change.expected_revision = foreign_doc->revision;
		foreign_change.expected_source_revision = foreign_doc->source_revision;
		foreign_change.proposed = foreign_doc->data;
		foreign_change.proposed.Set("title", "Must not cross workspace scope");
		Vector<DataBatchChange> mixed_workspace_changes = clone(final_changes);
		mixed_workspace_changes.Add(foreign_change);
		BatchFinalizationResult mixed_workspace = app.FinalizeBatch(final_auth, "scope-mixed-workspace", mixed_workspace_changes);
		if(mixed_workspace.committed || mixed_workspace.status != "prevalidation_failed" ||
		   LoadFile(final_a_path) != final_a_before_scope || LoadFile(final_b_path) != final_b_before_scope ||
		   LoadFile(AppendFileName(final_data, "foreign.json")) != AsJSON(foreign_record, true)) {
			Cerr() << "FAILED: mixed-workspace batch through FinalizeBatch was not denied without mutation\n";
			return false;
		}
		app.test_batch_notification_failpoint = 1;
		CatalogReadHandle before_refresh_failure = app.PinLatestCatalog();
		final_change_a.expected_revision = before_refresh_failure->data.FindDocument(final_change_a.qualified_id)->revision;
		final_change_a.expected_source_revision = before_refresh_failure->data.FindDocument(final_change_a.qualified_id)->source_revision;
		final_change_b.expected_revision = before_refresh_failure->data.FindDocument(final_change_b.qualified_id)->revision;
		final_change_b.expected_source_revision = before_refresh_failure->data.FindDocument(final_change_b.qualified_id)->source_revision;
		ValueMap final_env_a = MapGet(final_change_a.proposed, "envelope"); final_env_a.Set("revision", final_change_a.expected_revision); final_change_a.proposed.Set("envelope", final_env_a);
		ValueMap final_env_b = MapGet(final_change_b.proposed, "envelope"); final_env_b.Set("revision", final_change_b.expected_revision); final_change_b.proposed.Set("envelope", final_env_b);
		final_changes.Clear(); final_changes.Add(final_change_b); final_changes.Add(final_change_a);
		BatchFinalizationResult refresh_pending = app.FinalizeBatch(final_auth, "finalize-refresh-failure", final_changes);
		app.test_batch_notification_failpoint = 0;
		if(!refresh_pending.committed || refresh_pending.status != "committed_notification_pending" ||
		   LoadFile(final_a_path).Find("Finalized A") < 0 || LoadFile(final_b_path).Find("Finalized B") < 0) {
			Cerr() << "FAILED: injected post-commit refresh failure (status=" << refresh_pending.status
			       << ", committed=" << refresh_pending.committed << ", notify=" << refresh_pending.notification_status
			       << ", a=" << LoadFile(final_a_path).GetCount() << ", b=" << LoadFile(final_b_path).GetCount() << ")\n";
			return false;
		}
		if(!app.RefreshCatalog() || app.PinLatestCatalog()->data.FindDocument(final_change_a.qualified_id)->revision != "3") {
			Cerr() << "FAILED: subsequent refresh did not publish the committed records after injected refresh failure\n";
			return false;
		}
		app.test_batch_notification_failpoint = 2;
		CatalogReadHandle before_event_failure = app.PinLatestCatalog();
		final_change_a.expected_revision = before_event_failure->data.FindDocument(final_change_a.qualified_id)->revision;
		final_change_a.expected_source_revision = before_event_failure->data.FindDocument(final_change_a.qualified_id)->source_revision;
		final_change_b.expected_revision = before_event_failure->data.FindDocument(final_change_b.qualified_id)->revision;
		final_change_b.expected_source_revision = before_event_failure->data.FindDocument(final_change_b.qualified_id)->source_revision;
		final_env_a = MapGet(final_change_a.proposed, "envelope"); final_env_a.Set("revision", final_change_a.expected_revision); final_change_a.proposed.Set("envelope", final_env_a);
		final_env_b = MapGet(final_change_b.proposed, "envelope"); final_env_b.Set("revision", final_change_b.expected_revision); final_change_b.proposed.Set("envelope", final_env_b);
		final_changes.Clear(); final_changes.Add(final_change_b); final_changes.Add(final_change_a);
		BatchFinalizationResult event_pending = app.FinalizeBatch(final_auth, "finalize-event-failure", final_changes);
		app.test_batch_notification_failpoint = 0;
		if(!event_pending.committed || event_pending.status != "committed_notification_pending" ||
		   event_pending.event.GetCount() == 0 ||
		   LoadFile(final_a_path).Find("Finalized A") < 0 || !app.RefreshCatalog() ||
		   app.PinLatestCatalog()->data.FindDocument(final_change_a.qualified_id)->revision != "4") {
			Cerr() << "FAILED: injected event failure changed commit outcome or subsequent refresh lost records\n";
			return false;
		}
		CatalogReadHandle before_recovery_failure = app.PinLatestCatalog();
		final_change_a.expected_revision = before_recovery_failure->data.FindDocument(final_change_a.qualified_id)->revision;
		final_change_a.expected_source_revision = before_recovery_failure->data.FindDocument(final_change_a.qualified_id)->source_revision;
		final_change_b.expected_revision = before_recovery_failure->data.FindDocument(final_change_b.qualified_id)->revision;
		final_change_b.expected_source_revision = before_recovery_failure->data.FindDocument(final_change_b.qualified_id)->source_revision;
		final_env_a = MapGet(final_change_a.proposed, "envelope"); final_env_a.Set("revision", final_change_a.expected_revision); final_change_a.proposed.Set("envelope", final_env_a);
		final_env_b = MapGet(final_change_b.proposed, "envelope"); final_env_b.Set("revision", final_change_b.expected_revision); final_change_b.proposed.Set("envelope", final_env_b);
		final_changes.Clear(); final_changes.Add(final_change_b); final_changes.Add(final_change_a);
		app.test_batch_recovery_failpoint = DATA_WRITE_FAIL_RECOVERY_BEFORE_CLEANUP;
		BatchFinalizationResult recovery_pending = app.FinalizeBatch(final_auth, "finalize-recovery-failure", final_changes);
		app.test_batch_recovery_failpoint = DATA_WRITE_FAIL_NONE;
		String final_journal = AppendFileName(final_root, ".mgr-journal");
		if(!recovery_pending.committed || recovery_pending.status != "committed_notification_pending" ||
		   recovery_pending.notification_status != "recovery_pending" || recovery_pending.event.GetCount() == 0 ||
		   !DirectoryExists(final_journal) || LoadFile(final_a_path).Find("Finalized A") < 0 ||
		   LoadFile(final_b_path).Find("Finalized B") < 0) {
			Cerr() << "FAILED: post-commit T45 recovery failure did not retain journal and canonical committed files\n";
			return false;
		}
		BatchFinalizationResult recovered_retry = app.FinalizeBatch(final_auth, "finalize-recovery-failure", final_changes);
		if(!recovered_retry.committed || recovered_retry.status != "already_committed" ||
		   recovered_retry.notification_status != "published" || DirectoryExists(final_journal) ||
		   recovered_retry.changed_ids != recovery_pending.changed_ids || recovered_retry.revisions != recovery_pending.revisions ||
		   app.PinLatestCatalog()->data.FindDocument(final_change_a.qualified_id)->revision != "5") {
			Cerr() << "FAILED: FinalizeBatch retry did not recover, replay receipt and refresh committed generation\n";
			return false;
		}
		CatalogReadHandle concurrent_old_pin = app.PinLatestCatalog();
		const DataDocument* pinned_final_a = concurrent_old_pin->data.FindDocument(final_change_a.qualified_id);
		const DataDocument* pinned_final_b = concurrent_old_pin->data.FindDocument(final_change_b.qualified_id);
		if(!pinned_final_a || !pinned_final_b) return false;
		DataBatchChange concurrent_final_a = final_change_a, concurrent_final_b = final_change_b;
		concurrent_final_a.expected_revision = pinned_final_a->revision;
		concurrent_final_a.expected_source_revision = pinned_final_a->source_revision;
		concurrent_final_b.expected_revision = pinned_final_b->revision;
		concurrent_final_b.expected_source_revision = pinned_final_b->source_revision;
		ValueMap concurrent_env_a = MapGet(concurrent_final_a.proposed, "envelope"); concurrent_env_a.Set("revision", pinned_final_a->revision); concurrent_final_a.proposed.Set("envelope", concurrent_env_a);
		ValueMap concurrent_env_b = MapGet(concurrent_final_b.proposed, "envelope"); concurrent_env_b.Set("revision", pinned_final_b->revision); concurrent_final_b.proposed.Set("envelope", concurrent_env_b);
		Vector<DataBatchChange> concurrent_final_changes; concurrent_final_changes.Add(concurrent_final_a); concurrent_final_changes.Add(concurrent_final_b);
		std::atomic<bool> mixed_generation(false), refresh_failed(false), refresh_fail_closed(false);
		Thread refresh_thread;
		refresh_thread.Run([&] {
			for(int i = 0; i < 20; i++) {
				if(!app.RefreshCatalog()) {
					CatalogReadHandle unavailable = app.PinLatestCatalog();
					if(unavailable && unavailable->status == "load_error" && unavailable->data.GetDocuments().IsEmpty())
						refresh_fail_closed.store(true);
					else
						refresh_failed.store(true);
					continue;
				}
				CatalogReadHandle pin = app.PinLatestCatalog();
				const DataDocument* a = pin->data.FindDocument(final_change_a.qualified_id);
				const DataDocument* b = pin->data.FindDocument(final_change_b.qualified_id);
				if(!a || !b || a->revision != b->revision || (a->revision != "5" && a->revision != "6")) mixed_generation.store(true);
			}
		});
		BatchFinalizationResult concurrent_finalized = app.FinalizeBatch(final_auth, "finalize-concurrent-refresh", concurrent_final_changes);
		refresh_thread.Wait();
		CatalogReadHandle concurrent_new_pin = app.PinLatestCatalog();
		if(refresh_failed.load() || mixed_generation.load() || !concurrent_finalized.committed ||
		   !app.RefreshCatalog() ||
		   concurrent_new_pin->data.FindDocument(final_change_a.qualified_id)->revision != "6" ||
		   concurrent_new_pin->data.FindDocument(final_change_b.qualified_id)->revision != "6" ||
		   concurrent_old_pin->data.FindDocument(final_change_a.qualified_id)->revision != "5" ||
		   concurrent_old_pin->data.FindDocument(final_change_b.qualified_id)->revision != "5") {
			Cerr() << "FAILED: concurrent finalization (refresh_failed=" << refresh_failed.load()
			       << ", mixed=" << mixed_generation.load() << ", committed=" << concurrent_finalized.committed
			       << ", status=" << concurrent_finalized.status << ", generation_status=" << concurrent_new_pin->status
			       << ", a=" << (concurrent_new_pin->data.FindDocument(final_change_a.qualified_id) ? concurrent_new_pin->data.FindDocument(final_change_a.qualified_id)->revision : String("missing"))
			       << ", b=" << (concurrent_new_pin->data.FindDocument(final_change_b.qualified_id) ? concurrent_new_pin->data.FindDocument(final_change_b.qualified_id)->revision : String("missing")) << ")\n";
			return false;
		}
		if(refresh_fail_closed.load() && app.PinLatestCatalog()->status != "loaded") return false;
		app.test_registration = prior_registration;
		app.test_registration_provider = prior_provider;
		if(!app.RefreshCatalog()) return false;
		Cout() << "ok    T47 app finalization commits, recovers, refreshes and emits deterministic events; retries and notification failures preserve commits\n";
		Cout() << "ok    T40 synthetic writes enforce loaded capability, source/workspace scope, POSIX cross-process locking, no-follow paths, revisions, references and retries\n";
	}

	// Seed fixture: Generic structure for research tasks and scientific papers.
	// Directory structure: fixture_root/paper_eval/, fixture_root/task_gen/, fixture_root/topo/
	RealizeDirectory(AppendFileName(fixture_root, "paper_eval"));
	RealizeDirectory(AppendFileName(fixture_root, "task_gen"));
	RealizeDirectory(AppendFileName(fixture_root, "topo"));

	// File 1: Synthetic scientific-paper-shaped fixture matching the generic schema.
	// "authors" is an array-of-maps: exercises the generic table rendering path
	// on /data/doc (the classic "[col key error] / Invalid value conversion" trap).
	Value paper_value = ParseJSON("{ \"id\": \"paper-001\", \"kind\": \"paper\", \"title\": \"Research <script>alert(1)</script> Concept A\", \"status\": \"published\", \"source\": \"public\", \"abstract\": \"A generic study. <img src=x onerror=alert(2)>\", \"research_stage\": \"paper-screened\", \"authors\": [ { \"name\": \"<img src=x onerror=alert(3)>\", \"affiliation\": \"Institute A\" }, { \"name\": \"Author Two\", \"affiliation\": \"Institute B\" } ] }");
	ValueMap paper_map = paper_value;
	ValueArray untrusted_required;
	untrusted_required.Add("field-that-must-not-be-required");
	paper_map.Add("required", untrusted_required);
	paper_map.Add("envelope", record_envelope("paper", "paper", "paper-001"));
	String paper_json = AsJSON(paper_map, true);
	SaveFile(AppendFileName(fixture_root, "paper_eval/paper-001.json"), paper_json);
	SaveFile(AppendFileName(fixture_root, "paper_eval/malformed.json"), "{ malformed source fixture");

	// File 2: Task generated from a paper evaluation. "risks" is an array-of-maps.
	Value task_value = ParseJSON("{ \"id\": \"task-001\", \"kind\": \"task-proposal\", \"title\": \"Implement Schema B\", \"status\": \"proposed\", \"description\": \"Derived from generic research concept A.\", \"estimated_effort\": \"medium\", \"research_stage\": \"code-spike\", \"risks\": [ { \"risk\": \"Schema drift\", \"severity\": \"medium\", \"mitigation\": \"Pin schema versions\" }, { \"risk\": \"Unclear acceptance\", \"severity\": \"low\", \"mitigation\": \"Refine gate criteria\" } ] }");
	ValueMap task_map = task_value;
	task_map.Add("envelope", record_envelope("task-proposal", "task-proposal", "task-001"));
	String task_json = AsJSON(task_map, true);
	SaveFile(AppendFileName(fixture_root, "task_gen/task-001.json"), task_json);
	String hostile_link_id = "record-8\" onmouseover=\"alert(8)";
	ValueMap hostile_link_record;
	hostile_link_record.Add("id", hostile_link_id);
	hostile_link_record.Add("kind", "hostile-link");
	hostile_link_record.Add("title", "Hostile link record");
	hostile_link_record.Add("status", "proposed");
	RealizeDirectory(AppendFileName(fixture_root, "hostile-links"));
	SaveFile(AppendFileName(fixture_root, "hostile-links/record.json"), AsJSON(hostile_link_record, true));

	// File 3: A topology document for diagram testing.
	Value topology_value = ParseJSON("{ \"id\": \"topology/synthetic/topology-001\", \"kind\": \"topology\", \"name\": \"Fixture topology\", \"nodes\": [ { \"id\": \"node1\", \"type\": \"service\", \"name\": \"<script>alert(4)</script>\", \"url\": \"javascript:alert(5)\" }, { \"id\": \"node2\", \"type\": \"db\", \"name\": \"Database B\" } ], \"edges\": [ { \"from\": \"node1\", \"to\": \"node2\", \"role\": \"depends-on\" } ] }");
	ValueMap topology_map = topology_value;
	topology_map.Add("envelope", record_envelope("topology", "topology", "topology/synthetic/topology-001"));
	String topology_json = AsJSON(topology_map, true);
	SaveFile(AppendFileName(fixture_root, "topo/topology-001.json"), topology_json);

	{
		DataStore test_store;
		if(!test_store.LoadFixtureForTests(fixture_root)) {
			Cerr() << "FAILED: DataStore failed to load fixture root!\n";
			return false;
		}
		if(test_store.LoadFixtureForTests(GetCurrentDirectory())) {
			Cerr() << "FAILED: DataStore accepted a non-fixture source root\n";
			return false;
		}

		// Check for document count and validation
		if(test_store.GetDocuments().GetCount() != 4) {
			Cerr() << "FAILED: DataStore expected 4 documents, got " << test_store.GetDocuments().GetCount() << "\n";
			return false;
		}
		if(test_store.GetSourceId() != "self-test-fixture" || !test_store.IsReadOnly()) {
			Cerr() << "FAILED: explicit DataStore fixtures must be read-only and have a generic source id\n";
			return false;
		}
		for(const DataDocument& document : test_store.GetDocuments())
			if(document.source_id != "self-test-fixture" || !document.read_only) {
				Cerr() << "FAILED: fixture document source metadata was not propagated\n";
				return false;
			}
		if(test_store.GetInvalidCount() != 0) {
			Cerr() << "FAILED: DataStore reported invalid documents!\n";
			return false;
		}
		const DataDocument* validated_paper = test_store.FindDocument("synthetic-workspace:paper-001");
		const DataDocument* legacy_hostile = test_store.FindDocument(hostile_link_id);
		String file_digest;
		if(!test_store.GetSourceFingerprints().IsEmpty()) {
			String fingerprint = test_store.GetSourceFingerprints()[0];
			int separator = fingerprint.Find(':');
			if(separator >= 0) file_digest = fingerprint.Mid(separator + 1);
		}
		if(!validated_paper || !validated_paper->valid || !validated_paper->validated ||
		   validated_paper->source_revision != DataStore::RecordSourceRevision("paper_eval/paper-001.json", LoadFile(AppendFileName(fixture_root, "paper_eval/paper-001.json"))) ||
		   !is_sha256(file_digest) ||
		   !legacy_hostile || legacy_hostile->valid || legacy_hostile->validation_state != "quarantined") {
			Cerr() << "FAILED: envelope validation, quarantine, or SHA-256 source fingerprint contract failed\n";
			return false;
		}
		bool malicious_required_registered = false;
		for(const String& required : test_store.FindSchemaCard("paper")->required)
			if(required == "field-that-must-not-be-required") malicious_required_registered = true;
		const SchemaCard* analysis_schema = test_store.FindSchemaCard("paper-analysis");
		bool analysis_requires_status = false;
		if(analysis_schema)
			for(const String& required : analysis_schema->required) if(required == "status") analysis_requires_status = true;
		if(malicious_required_registered || !analysis_schema || analysis_schema->required.GetCount() != 7 || analysis_requires_status) {
			Cerr() << "FAILED: untrusted document required field modified the trusted schema\n";
			return false;
		}
		if(test_store.GetSourceErrorCount() != 1 || test_store.IsLoadIncomplete() || test_store.GetLoadStatus() != "loaded_with_errors") {
			Cerr() << "FAILED: DataStore should count and skip one malformed source without aborting\n";
			return false;
		}
		Cout() << "ok    DataStore loaded 4 documents, 0 invalid, 1 malformed source skipped\n";
	}

	// Exercise native JSON Value fidelity and validator negative paths with synthetic-only roots.
	{
		Value exact = ParseJSON("{\"object\":{},\"array\":[],\"nil\":null}");
		if(!exact.Is<ValueMap>()) { Cerr() << "FAILED: U++ JSON root object type was not exact\n"; return false; }
		ValueMap exact_map = exact;
		int object_at = exact_map.Find("object"), array_at = exact_map.Find("array"), null_at = exact_map.Find("nil");
		if(object_at < 0 || array_at < 0 || null_at < 0 ||
		   !exact_map.GetValue(object_at).Is<ValueMap>() || !exact_map.GetValue(array_at).Is<ValueArray>() ||
		   !IsNull(exact_map.GetValue(null_at))) {
			Cerr() << "FAILED: U++ JSON object/array/null distinctions were not preserved\n"; return false;
		}
		Value exact_array = ParseJSON("[]");
		if(!exact_array.Is<ValueArray>() || !IsNull(ParseJSON("null"))) { Cerr() << "FAILED: U++ JSON root array/null types were not exact\n"; return false; }
		Value large_number = ParseJSON("9007199254740993");
		bool exact_large_integer = large_number.Is<int64>() && (int64)large_number == 9007199254740993LL;
		String pattern_error;
		if(!DataStore::MatchIdPattern("paper-1/a", "^[A-Za-z0-9][A-Za-z0-9._/-]*$", pattern_error) ||
		   DataStore::MatchIdPattern("bad id", "^[A-Za-z0-9][A-Za-z0-9._/-]*$", pattern_error) || pattern_error != "id_pattern_mismatch" ||
		   DataStore::MatchIdPattern("anything", "^(foo|bar)$", pattern_error) || pattern_error != "unsupported_id_pattern" ||
		   (large_number.Is<int64>() && !exact_large_integer)) {
			Cerr() << "FAILED: supported ID pattern subset did not match/reject as specified\n"; return false;
		}
		Cout() << "ok    U++ JSON exact object/array/null types and ID-pattern subset\n"; Cout().Flush();

		String policy_root = workspace.FixtureRoot("policy-data");
		auto paper_record = [&](const String& id, const String& status) {
			ValueMap record;
			record.Add("id", id); record.Add("kind", "paper"); record.Add("title", "Synthetic title"); record.Add("source", "synthetic"); record.Add("status", status);
			record.Add("envelope", record_envelope("paper", "paper", id));
			return record;
		};
		auto save_record = [&](const String& rel, const String& json) {
			String filename = AppendFileName(policy_root, rel);
			RealizeDirectory(GetFileFolder(filename));
			SaveFile(filename, json);
		};
		auto save_map = [&](const String& rel, const ValueMap& record) { save_record(rel, AsJSON(record, true)); };
		auto replace = [](ValueMap& map, const String& key, const Value& value) {
			int at = map.Find(key); if(at >= 0) map.Remove(at); map.Add(key, value);
		};
		auto remove_key = [](ValueMap& map, const String& key) { int at = map.Find(key); if(at >= 0) map.Remove(at); };
		auto analysis_record = [&](const String& id) {
			ValueMap record; record.Add("id", id); record.Add("kind", "paper-analysis"); record.Add("paper", "unresolved-paper");
			record.Add("repo", "synthetic-repo"); record.Add("version", "v1"); record.Add("tasks", ValueArray()); record.Add("findings", ValueArray());
			record.Add("envelope", record_envelope("paper-analysis", "paper-analysis", id)); return record;
		};
		ValueMap missing = paper_record("missing-title", "published"); int title_at = missing.Find("title"); if(title_at >= 0) missing.Remove(title_at); save_map("required/a.json", missing);
		ValueMap null_required = paper_record("null-title", "published"); Value null_value = Null; replace(null_required, "title", null_value); save_map("required/null.json", null_required);
		ValueMap null_optional = paper_record("null-abstract", "published"); null_optional.Add("abstract", Null); save_map("type/null-optional.json", null_optional);
		ValueMap wrong_type = paper_record("wrong-type", "published"); replace(wrong_type, "title", 7); save_map("type/a.json", wrong_type);
		ValueMap wrong_enum = paper_record("wrong-enum", "never-a-status"); save_map("enum/a.json", wrong_enum);
		ValueMap analysis_missing_repo = analysis_record("analysis-missing-repo"); remove_key(analysis_missing_repo, "repo"); save_map("analysis/missing-repo.json", analysis_missing_repo);
		ValueMap analysis_missing_version = analysis_record("analysis-missing-version"); remove_key(analysis_missing_version, "version"); save_map("analysis/missing-version.json", analysis_missing_version);
		ValueMap analysis_missing_tasks = analysis_record("analysis-missing-tasks"); remove_key(analysis_missing_tasks, "tasks"); save_map("analysis/missing-tasks.json", analysis_missing_tasks);
		ValueMap analysis_missing_findings = analysis_record("analysis-missing-findings"); remove_key(analysis_missing_findings, "findings"); save_map("analysis/missing-findings.json", analysis_missing_findings);
		ValueMap analysis_wrong_repo = analysis_record("analysis-wrong-repo"); replace(analysis_wrong_repo, "repo", 1); save_map("analysis/wrong-repo.json", analysis_wrong_repo);
		ValueMap analysis_wrong_version = analysis_record("analysis-wrong-version"); replace(analysis_wrong_version, "version", 1); save_map("analysis/wrong-version.json", analysis_wrong_version);
		ValueMap analysis_wrong_tasks = analysis_record("analysis-wrong-tasks"); replace(analysis_wrong_tasks, "tasks", "not-an-array"); save_map("analysis/wrong-tasks.json", analysis_wrong_tasks);
		ValueMap analysis_wrong_findings = analysis_record("analysis-wrong-findings"); replace(analysis_wrong_findings, "findings", ValueMap()); save_map("analysis/wrong-findings.json", analysis_wrong_findings);
		ValueMap analysis_wrong_item = analysis_record("analysis-wrong-task-item"); ValueArray bad_tasks; bad_tasks.Add("not-an-object"); replace(analysis_wrong_item, "tasks", bad_tasks); save_map("analysis/wrong-task-item.json", analysis_wrong_item);
		ValueMap bad_id = paper_record("bad id", "published"); save_map("id/a.json", bad_id);
		ValueMap bad_version = paper_record("future-version", "published");
		ValueMap future_env = record_envelope("paper", "paper", "future-version");
		replace(future_env, "schema_version", "99"); replace(bad_version, "envelope", future_env); save_map("version/a.json", bad_version);
		ValueMap unknown_kind; unknown_kind.Add("id", "unknown"); unknown_kind.Add("kind", "synthetic-unknown");
		unknown_kind.Add("envelope", record_envelope("synthetic-unknown", "synthetic-unknown", "unknown")); save_map("unknown/a.json", unknown_kind);
		ValueMap nested_topology;
		nested_topology.Add("id", "nested-bad"); nested_topology.Add("kind", "topology"); nested_topology.Add("name", "Synthetic");
		ValueArray nodes; ValueMap node; node.Add("id", "node"); node.Add("type", "service"); nodes.Add(node); nested_topology.Add("nodes", nodes);
		nested_topology.Add("envelope", record_envelope("topology", "topology", "nested-bad")); save_map("nested/a.json", nested_topology);
		ValueMap wrong_nested_type;
		wrong_nested_type.Add("id", "nested-type"); wrong_nested_type.Add("kind", "topology"); wrong_nested_type.Add("name", "Synthetic");
		ValueArray scalar_nodes; scalar_nodes.Add("not-an-object"); wrong_nested_type.Add("nodes", scalar_nodes);
		wrong_nested_type.Add("envelope", record_envelope("topology", "topology", "nested-type")); save_map("nested/type.json", wrong_nested_type);
		auto topology_with_metadata = [&](const String& id, const Value& category) {
			ValueMap record; record.Add("id", id); record.Add("kind", "topology"); record.Add("name", "Synthetic");
			ValueMap metadata; metadata.Add("category", category);
			ValueMap nested_node; nested_node.Add("id", "node"); nested_node.Add("type", "service"); nested_node.Add("name", "Node");
			nested_node.Add("metadata", metadata); ValueArray nested_nodes; nested_nodes.Add(nested_node); record.Add("nodes", nested_nodes);
			record.Add("envelope", record_envelope("topology", "topology", id)); return record;
		};
		save_map("nested/property-type.json", topology_with_metadata("nested-property-type", 7));
		save_map("nested/property-enum.json", topology_with_metadata("nested-property-enum", "tertiary"));
		ValueMap nested_null = topology_with_metadata("nested-null", "primary");
		ValueArray nested_null_nodes; ValueMap nested_null_node; nested_null_node.Add("id", "node"); nested_null_node.Add("type", "service"); nested_null_node.Add("name", "Node");
		nested_null_node.Add("description", Null); nested_null_node.Add("metadata", ValueMap()); nested_null_nodes.Add(nested_null_node); replace(nested_null, "nodes", nested_null_nodes); save_map("nested/null-optional.json", nested_null);
		save_record("fidelity/duplicate.json", "{\"id\":\"dup\",\"id\":\"dup\",\"kind\":\"paper\",\"title\":\"Synthetic\",\"source\":\"synthetic\",\"status\":\"published\",\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\",\"workspace_id\":\"synthetic-workspace\",\"stable_id\":\"dup\",\"record_kind\":\"paper\",\"revision\":\"1\",\"visibility\":\"private\",\"provenance_ref\":\"synthetic-fixture\"}}");
		save_record("fidelity/nested-duplicate.json", "{\"id\":\"nested-duplicate\",\"kind\":\"topology\",\"name\":\"Synthetic\",\"nodes\":[{\"id\":\"node\",\"id\":\"node\",\"type\":\"service\",\"name\":\"Node\"}],\"envelope\":{\"schema_id\":\"skylark/topology\",\"schema_version\":\"1\",\"workspace_id\":\"synthetic-workspace\",\"stable_id\":\"nested-duplicate\",\"record_kind\":\"topology\",\"revision\":\"1\",\"visibility\":\"private\",\"provenance_ref\":\"synthetic-fixture\"}}");
		save_record("fidelity/trailing.json", AsJSON(paper_record("trailing", "published"), true) + " trailing");
		save_record("fidelity/large-number.json", "{\"id\":\"numeric\",\"kind\":\"paper\",\"title\":\"Synthetic\",\"source\":\"synthetic\",\"status\":\"published\",\"probe\":9007199254740993,\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\",\"workspace_id\":\"synthetic-workspace\",\"stable_id\":\"numeric\",\"record_kind\":\"paper\",\"revision\":\"1\",\"visibility\":\"private\",\"provenance_ref\":\"synthetic-fixture\"}}");
		save_record("fidelity/fraction.json", "{\"id\":\"fraction\",\"kind\":\"paper\",\"title\":\"Synthetic\",\"source\":\"synthetic\",\"status\":\"published\",\"probe\":0.00000000000000000001,\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\",\"workspace_id\":\"synthetic-workspace\",\"stable_id\":\"fraction\",\"record_kind\":\"paper\",\"revision\":\"1\",\"visibility\":\"private\",\"provenance_ref\":\"synthetic-fixture\"}}");
		save_record("fidelity/exponent.json", "{\"id\":\"exponent\",\"kind\":\"paper\",\"title\":\"Synthetic\",\"source\":\"synthetic\",\"status\":\"published\",\"probe\":1e+3,\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\",\"workspace_id\":\"synthetic-workspace\",\"stable_id\":\"exponent\",\"record_kind\":\"paper\",\"revision\":\"1\",\"visibility\":\"private\",\"provenance_ref\":\"synthetic-fixture\"}}");
		save_record("fidelity/escaped-key.json", "{\"id\":\"escaped\",\"kind\":\"paper\",\"title\":\"Synthetic\",\"\\u0074itle\":\"Alias\",\"source\":\"synthetic\",\"status\":\"published\",\"envelope\":{\"schema_id\":\"skylark/paper\",\"schema_version\":\"1\",\"workspace_id\":\"synthetic-workspace\",\"stable_id\":\"escaped\",\"record_kind\":\"paper\",\"revision\":\"1\",\"visibility\":\"private\",\"provenance_ref\":\"synthetic-fixture\"}}");
		save_record("fidelity/null-root.json", "null");
		save_record("fidelity/array-root.json", "[]");
		save_record("fidelity/scalar-root.json", "7");
		Cout() << "ok    synthetic validator and raw JSON fidelity fixtures seeded\n"; Cout().Flush();
		DataStore policy_store;
		if(!policy_store.LoadFixtureForTests(policy_root)) { Cerr() << "FAILED: policy fixture store failed to load\n"; return false; }
		auto has_document_diag = [&](const String& id, const String& wanted) {
			const DataDocument* d = policy_store.FindDocument(id);
			if(!d) return false;
			for(const String& diag : d->diagnostics) if(diag == wanted || diag.StartsWith(wanted + ":")) return true;
			return false;
		};
		auto has_source_diag = [&](const String& wanted) {
			for(const String& diag : policy_store.GetSourceDiagnostics()) if(diag.Find(wanted) >= 0) return true;
			return false;
		};
		auto parser_case_is_reported = [&](const String& id, const String& path_fragment,
		                                  const String& document_code, const String& source_code) {
			const DataDocument* document = policy_store.FindDocument(id);
			if(document) {
				bool has_code = false;
				for(const String& diagnostic : document->diagnostics)
					if(diagnostic == document_code) has_code = true;
				return has_code && document->validation_state == "quarantined" && !document->raw_json.IsEmpty();
			}
			for(const String& diagnostic : policy_store.GetSourceDiagnostics())
				if(diagnostic.Find(path_fragment) >= 0 && diagnostic.Find(source_code) >= 0) return true;
			return false;
		};
		if(!has_document_diag("missing-title", "required:title") || !has_document_diag("null-title", "required:title") || !has_document_diag("null-abstract", "type:abstract") || !has_document_diag("wrong-type", "type:title") ||
		   !has_document_diag("wrong-enum", "enum:status") || !has_document_diag("analysis-missing-repo", "required:repo") ||
		   !has_document_diag("analysis-missing-version", "required:version") || !has_document_diag("analysis-missing-tasks", "required:tasks") ||
		   !has_document_diag("analysis-missing-findings", "required:findings") || !has_document_diag("analysis-wrong-repo", "type:repo") ||
		   !has_document_diag("analysis-wrong-version", "type:version") || !has_document_diag("analysis-wrong-tasks", "type:tasks") ||
		   !has_document_diag("analysis-wrong-findings", "type:findings") || !has_document_diag("analysis-wrong-task-item", "nested_type:tasks") ||
		   !has_document_diag("bad id", "id_pattern_mismatch") ||
		   !has_document_diag("nested-bad", "required:nodes[0].name") || !has_document_diag("nested-type", "nested_type:nodes") ||
		   !has_document_diag("nested-property-type", "type:nodes[0].metadata.category") ||
		   !has_document_diag("nested-property-enum", "enum:nodes[0].metadata.category") ||
		   !has_document_diag("nested-null", "type:nodes[0].description") ||
		   !has_document_diag("future-version", "unsupported_schema_version") || !has_document_diag("unknown", "unsupported_kind") ||
		   policy_store.GetQuarantineCount() < 2 || policy_store.GetInvalidCount() < 5 ||
		   !parser_case_is_reported("dup", "duplicate.json", "parser_duplicate_key", "duplicate_key") ||
		   !parser_case_is_reported("nested-duplicate", "nested-duplicate.json", "parser_duplicate_key", "duplicate_key") ||
		   !parser_case_is_reported("trailing", "trailing.json", "parser_trailing_input", "trailing_input") ||
		   !parser_case_is_reported("numeric", "large-number.json", "parser_numeric_precision_unverified", "numeric_precision_unverified") ||
		   !parser_case_is_reported("fraction", "fraction.json", "parser_numeric_precision_unverified", "numeric_precision_unverified") ||
		   !parser_case_is_reported("exponent", "exponent.json", "parser_numeric_precision_unverified", "numeric_precision_unverified") ||
		   !parser_case_is_reported("escaped", "escaped-key.json", "parser_escaped_key_unverified", "escaped_key_unverified") ||
		   !has_source_diag("root_null_not_object") ||
		   !has_source_diag("root_array_not_object") || !has_source_diag("root_scalar_not_object")) {
			Cerr() << "FAILED: policy negative validation or parser-fidelity diagnostics incomplete; invalid="
			       << policy_store.GetInvalidCount() << " quarantine=" << policy_store.GetQuarantineCount() << " source-errors=" << policy_store.GetSourceErrorCount() << "\n";
			for(const DataDocument& d : policy_store.GetDocuments()) {
				Cerr() << d.id << " [" << d.validation_state << "]";
				for(const String& diag : d.diagnostics) Cerr() << " " << diag;
				Cerr() << "\n";
			}
			for(const String& diag : policy_store.GetSourceDiagnostics()) Cerr() << diag << "\n";
			return false;
		}
		Cout() << "ok    exact U++ types, parser fidelity limits, ID grammar, and negative schema diagnostics\n";
	}

	{
		auto schema_order_case = [&](const String& suffix, bool reverse) {
			String root = workspace.FixtureRoot("schema-order-" + suffix);
			auto write = [&](const String& name, const String& id, const String& required) {
				String json = "{\"kind\":\"paper\",\"id\":\"" + id + "\",\"required\":[\"" + required + "\"],\"id_pattern\":\".*\"}";
				String filename = AppendFileName(root, "records/ScopeA/" + name + ".json");
				RealizeDirectory(GetFileFolder(filename)); SaveFile(filename, json);
			};
			if(reverse) { write("y-schema-two", "fake-late", "title"); write("z-schema-one", "fake-early", "id"); }
			else { write("a-schema-one", "fake-early", "id"); write("b-schema-two", "fake-late", "title"); }
			ValueMap paper;
			paper.Add("id", "real-paper"); paper.Add("kind", "paper"); paper.Add("title", "Synthetic"); paper.Add("source", "synthetic"); paper.Add("status", "published");
			paper.Add("envelope", record_envelope("paper", "paper", "real-paper"));
			String filename = AppendFileName(root, String("records/ScopeA/") + (reverse ? "a-paper.json" : "z-paper.json"));
			RealizeDirectory(GetFileFolder(filename)); SaveFile(filename, AsJSON(paper, true));
			DataStore store;
			if(!store.LoadFixtureForTests(root)) return false;
			const SchemaCard* schema = store.FindSchemaCard("paper");
			const DataDocument* accepted = store.FindDocument("real-paper");
			return schema && schema->required.GetCount() == 5 && accepted && accepted->valid &&
			       store.GetQuarantineCount() == 2 && store.GetDocuments().GetCount() == 3;
		};
		if(!schema_order_case("forward", false) || !schema_order_case("reverse", true)) {
			Cerr() << "FAILED: late/duplicate source schema-shaped documents influenced trusted schemas or validity\n"; return false;
		}
		Cout() << "ok    schema-shaped source documents cannot alter trusted registry under filename order changes\n";
	}

	{
		auto reference_case = [&](const String& suffix, const String& scenario, bool reverse) {
			String root = workspace.FixtureRoot("reference-" + suffix);
			auto save = [&](const String& rel_path, const String& kind, const String& id, const String& status, const String& workspace_id) {
				ValueMap record; record.Add("id", id); record.Add("kind", kind);
				if(kind == "paper-analysis") {
					record.Add("paper", "target"); record.Add("repo", "synthetic-repo"); record.Add("version", "v1");
					record.Add("tasks", ValueArray()); record.Add("findings", ValueArray());
				}
				else record.Add("status", status);
				if(kind == "paper" || kind == "task-proposal") record.Add("title", "Synthetic target");
				if(kind == "paper") record.Add("source", "synthetic");
				String schema = kind;
				ValueMap envelope = record_envelope(schema, kind, id);
				int at = envelope.Find("workspace_id"); if(at >= 0) envelope.Remove(at); envelope.Add("workspace_id", workspace_id);
				record.Add("envelope", envelope);
				String path = AppendFileName(root, rel_path);
				RealizeDirectory(GetFileFolder(path)); SaveFile(path, AsJSON(record, true));
			};
			String analysis_name = reverse ? "z-analysis.json" : "a-analysis.json";
			String analysis_path = "papers/analyses/ScopeA/" + analysis_name;
			String paper_path = String("papers/ScopeA/") + (reverse ? "a-target.json" : "z-target.json");
			auto save_analysis = [&] { save(analysis_path, "paper-analysis", "analysis-" + suffix, "complete", "ref-workspace"); };
			auto save_targets = [&] {
				if(scenario == "success") save(paper_path, "paper", "target", "published", "ref-workspace");
				if(scenario == "invalid-target") save(paper_path, "paper", "target", "invalid-status", "ref-workspace");
				if(scenario == "wrong-kind") save(paper_path, "task-proposal", "target", "proposed", "ref-workspace");
				if(scenario == "cross-workspace") save(paper_path, "paper", "target", "published", "other-workspace");
				if(scenario == "duplicate") {
					String first = reverse ? "z" : "a", second = reverse ? "a" : "z";
					save("papers/ScopeA/" + first + "/first.json", "paper", "target", "published", "ref-workspace");
					save("papers/ScopeA/" + second + "/second.json", "paper", "target", "published", "ref-workspace");
				}
			};
			if(reverse) { save_targets(); save_analysis(); }
			else { save_analysis(); save_targets(); }
			DataStore store;
			if(!store.LoadFixtureForTests(root)) return false;
			const DataDocument* analysis = store.FindDocument("analysis-" + suffix);
			if(!analysis) { Cerr() << "reference fixture missing analysis " << suffix << "\n"; return false; }
			if(scenario == "success") {
				if(!analysis->valid || analysis->validation_state != "valid") {
					Cerr() << "reference success invalid " << suffix << ":";
					for(const String& diagnostic : analysis->diagnostics) Cerr() << " " << diagnostic;
					Cerr() << "\n";
					return false;
				}
				return true;
			}
			String expected = scenario == "wrong-kind" ? "reference_wrong_kind:paper" :
			                  scenario == "duplicate" ? "reference_ambiguous:paper" :
			                  scenario == "invalid-target" ? "reference_target_invalid:paper" : "reference_missing:paper";
			bool found = false;
			for(const String& diagnostic : analysis->diagnostics) if(diagnostic == expected) found = true;
			if(!found) {
				Cerr() << "reference failure diagnostic missing " << suffix << " expected " << expected << ":";
				for(const String& diagnostic : analysis->diagnostics) Cerr() << " " << diagnostic;
				Cerr() << "\n";
			}
			if(scenario == "duplicate") return found && !analysis->valid && store.GetInvalidCount() == 3;
			return found && !analysis->valid;
		};
		if(!reference_case("success-early", "success", false) || !reference_case("success-late", "success", true) ||
		   !reference_case("missing-early", "missing", false) || !reference_case("missing-late", "missing", true) ||
		   !reference_case("cross-workspace-early", "cross-workspace", false) || !reference_case("cross-workspace-late", "cross-workspace", true) ||
		   !reference_case("wrong-kind-early", "wrong-kind", false) || !reference_case("wrong-kind-late", "wrong-kind", true) ||
		   !reference_case("invalid-target-early", "invalid-target", false) || !reference_case("invalid-target-late", "invalid-target", true) ||
		   !reference_case("duplicate-early", "duplicate", false) || !reference_case("duplicate-late", "duplicate", true)) {
			Cerr() << "FAILED: workspace-scoped paper reference resolution did not enforce exact target identity\n"; return false;
		}
		Cout() << "ok    paper references resolve exactly once within workspace across folder scopes\n";
	}

	{
		auto nested_order_case = [&](const String& suffix, bool reverse) {
			String root = workspace.FixtureRoot("nested-order-" + suffix);
			auto save_topology = [&](const String& name, const String& id, bool detailed) {
				ValueMap node; node.Add("id", "node"); node.Add("type", "service"); node.Add("name", "Synthetic node");
				if(detailed) {
					node.Add("description", "Synthetic description");
					ValueMap metadata; metadata.Add("category", "primary"); metadata.Add("label", "Synthetic label");
					node.Add("metadata", metadata);
				}
				ValueArray nodes; nodes.Add(node);
				ValueMap record; record.Add("id", id); record.Add("kind", "topology"); record.Add("name", "Synthetic topology"); record.Add("nodes", nodes);
				record.Add("envelope", record_envelope("topology", "topology", id));
				String path = AppendFileName(root, "records/ScopeA/" + name + ".json");
				RealizeDirectory(GetFileFolder(path)); SaveFile(path, AsJSON(record, true));
			};
			if(reverse) { save_topology("a-plain", "plain-order", false); save_topology("z-nested", "nested-order", true); }
			else { save_topology("a-nested", "nested-order", true); save_topology("z-plain", "plain-order", false); }
			DataStore store;
			if(!store.LoadFixtureForTests(root)) return false;
			const DataDocument* nested = store.FindDocument("nested-order");
			const DataDocument* plain = store.FindDocument("plain-order");
			return nested && plain && nested->valid && plain->valid && store.GetInvalidCount() == 0;
		};
		if(!nested_order_case("nested-first", false) || !nested_order_case("nested-last", true)) {
			Cerr() << "FAILED: valid recursive property records depended on discovery order\n"; return false;
		}
		Cout() << "ok    valid nested object and array-item records independent of filename order\n";
	}

	{
		String budget_root = workspace.FixtureRoot("source-budget");
		String chunk;
		for(int i = 0; i < 65536; i++) chunk.Cat(' ');
		String oversized;
		oversized.Reserve(DataStore::MAX_SOURCE_FILE_BYTES + 1);
		for(int64 written = 0; written < DataStore::MAX_SOURCE_FILE_BYTES; written += chunk.GetCount())
			oversized.Cat(chunk);
		oversized.Cat(' ');
		String oversized_path = AppendFileName(budget_root, "oversized.json");
		RealizeDirectory(GetFileFolder(oversized_path));
		SaveFile(oversized_path, oversized);
		DataStore bounded_store;
		if(!bounded_store.LoadFixtureForTests(budget_root) || bounded_store.GetSourceErrorCount() != 1 ||
		   !bounded_store.GetDocuments().IsEmpty() || !bounded_store.IsLoadIncomplete() ||
	   bounded_store.GetLoadStatus() != "load_incomplete") {
			Cerr() << "FAILED: oversized synthetic JSON was not rejected by the per-file source bound\n";
			return false;
		}
		DataSourceRegistration registration;
		registration.id = "synthetic-per-file-budget"; registration.root = budget_root;
		One<V2Store> v2; v2.Create();
		One<DataStore> data; data.Create();
		String load_error; Vector<String> diagnostics;
		bool complete = LoadCatalogRegistration(registration, v2, data, load_error, diagnostics);
		CatalogGeneration incomplete_generation(90, pick(v2), pick(data), complete, complete, load_error, diagnostics);
		bool has_relative_limit = false;
		for(const String& diagnostic : incomplete_generation.diagnostics)
			if(diagnostic == "data:oversized.json:source_limit") has_relative_limit = true;
		if(complete || incomplete_generation.status != "load_error" || incomplete_generation.source_revision != "unavailable" ||
		   !incomplete_generation.v2.GetGoals().IsEmpty() || !incomplete_generation.data.GetDocuments().IsEmpty() || !has_relative_limit ||
		   diagnostics_contain(incomplete_generation.diagnostics, budget_root)) {
			Cerr() << "FAILED: per-file source budget did not withhold the shared catalog with relative diagnostics\n";
			return false;
		}
		Cout() << "ok    per-file source budget makes the shared catalog unavailable with relative-only diagnostics\n";

		String v2_budget_root = workspace.FixtureRoot("v2-file-budget");
		String v2_oversized_path = AppendFileName(v2_budget_root, "goals/synthetic/oversized.json");
		RealizeDirectory(GetFileFolder(v2_oversized_path));
		if(!SaveFile(v2_oversized_path, oversized)) { Cerr() << "FAILED: could not write synthetic V2 file-budget fixture\n"; return false; }
		V2Store v2_file_limited;
		if(!v2_file_limited.LoadFixtureForTests(v2_budget_root) || !v2_file_limited.IsLoadIncomplete() ||
		   v2_file_limited.GetLoadStatus() != "load_incomplete") {
			Cerr() << "FAILED: V2 per-file source limit was treated as a complete store\n";
			return false;
		}
		bool v2_limit_diagnostic = false;
		for(const String& diagnostic : v2_file_limited.GetSourceDiagnostics())
			v2_limit_diagnostic |= diagnostic == "goals/synthetic/oversized.json:source_limit";
		if(!v2_limit_diagnostic) { Cerr() << "FAILED: V2 per-file limit lacked a relative diagnostic\n"; return false; }
		Cout() << "ok    V2 per-file source byte bound reports load_incomplete with relative diagnostic\n";

		String depth_root = workspace.FixtureRoot("source-depth-budget");
		String deep_dir = depth_root;
		for(int i = 0; i < DataStore::MAX_DEPTH + 2; i++) deep_dir = AppendFileName(deep_dir, "nested" + AsString(i));
		RealizeDirectory(deep_dir);
		SaveFile(AppendFileName(deep_dir, "deep.json"), "{\"id\":\"depth-record\",\"kind\":\"paper\"}");
		DataStore depth_store;
		if(!depth_store.LoadFixtureForTests(depth_root) || !depth_store.IsLoadIncomplete() || depth_store.GetLoadStatus() != "load_incomplete") {
			Cerr() << "FAILED: recursive scan depth budget was treated as complete\n";
			return false;
		}
		bool depth_diagnostic = false;
		for(const String& diagnostic : depth_store.GetSourceDiagnostics()) depth_diagnostic |= diagnostic.EndsWith(":depth_limit");
		if(!depth_diagnostic) { Cerr() << "FAILED: recursive scan depth budget lacked a relative diagnostic\n"; return false; }
		registration.id = "synthetic-depth-budget"; registration.root = depth_root;
		v2.Create(); data.Create();
		load_error.Clear(); diagnostics.Clear();
		complete = LoadCatalogRegistration(registration, v2, data, load_error, diagnostics);
		CatalogGeneration depth_generation(92, pick(v2), pick(data), complete, complete, load_error, diagnostics);
		if(complete || depth_generation.status != "load_error" || depth_generation.source_revision != "unavailable" ||
		   diagnostics_contain(depth_generation.diagnostics, depth_root)) {
			Cerr() << "FAILED: scan-depth exhaustion published records or leaked its root\n";
			return false;
		}
		Cout() << "ok    recursive directory depth exhaustion is explicitly load_incomplete\n";

		String entry_root = workspace.FixtureRoot("source-entry-budget");
		String goals_dir = AppendFileName(entry_root, "goals/synthetic");
		RealizeDirectory(goals_dir);
		for(int i = 0; i < DataStore::MAX_FILES; i++) {
			String id = "budget-goal-" + AsString(i);
			String json = "{\"id\":\"" + id + "\",\"kind\":\"goal\",\"title\":\"Synthetic\",\"tags\":[],\"state\":{},\"tasks\":[]}";
			if(!SaveFile(AppendFileName(goals_dir, id + ".json"), json)) { Cerr() << "FAILED: could not write synthetic entry-budget fixture\n"; return false; }
		}
		V2Store v2_entries;
		DataStore data_entries;
		if(!v2_entries.LoadFixtureForTests(entry_root) || !data_entries.LoadFixtureForTests(entry_root) ||
		   !v2_entries.IsLoadIncomplete() || !data_entries.IsLoadIncomplete() ||
		   v2_entries.GetLoadStatus() != "load_incomplete" || data_entries.GetLoadStatus() != "load_incomplete") {
			Cerr() << "FAILED: V2/DataStore entry budgets did not report incomplete scans\n";
			return false;
		}
		registration.id = "synthetic-entry-budget"; registration.root = entry_root;
		v2.Create(); data.Create();
		load_error.Clear(); diagnostics.Clear();
		complete = LoadCatalogRegistration(registration, v2, data, load_error, diagnostics);
		CatalogGeneration entry_generation(91, pick(v2), pick(data), complete, complete, load_error, diagnostics);
		if(complete || entry_generation.status != "load_error" || entry_generation.source_revision != "unavailable" ||
		   !entry_generation.v2.GetGoals().IsEmpty() || !entry_generation.data.GetDocuments().IsEmpty() ||
		   diagnostics_contain(entry_generation.diagnostics, entry_root)) {
			Cerr() << "FAILED: entry-budget exhaustion published a partial shared catalog\n";
			return false;
		}
		Cout() << "ok    V2/DataStore entry exhaustion withholds the shared catalog\n";
	}

	{
		auto collision_store = [&](const String& suffix, bool reverse) {
			String root = workspace.FixtureRoot("collision-" + suffix);
			auto write = [&](const String& folder, const String& workspace_id) {
				ValueMap record;
				record.Add("id", "shared"); record.Add("kind", "paper"); record.Add("title", "Synthetic collision");
				record.Add("source", "synthetic"); record.Add("status", "published");
				ValueMap envelope = record_envelope("paper", "paper", "shared");
				int at = envelope.Find("workspace_id"); if(at >= 0) envelope.Remove(at);
				envelope.Add("workspace_id", workspace_id); record.Add("envelope", envelope);
				String filename = AppendFileName(root, folder + "/record.json");
				RealizeDirectory(GetFileFolder(filename)); SaveFile(filename, AsJSON(record, true));
			};
			if(reverse) { write("second", "dup-workspace"); write("first", "dup-workspace"); }
			else { write("first", "dup-workspace"); write("second", "dup-workspace"); }
			write("third", "other-workspace");
			DataStore store;
			if(!store.LoadFixtureForTests(root) || store.GetDocuments().GetCount() != 3) return false;
			const DataDocument* first = store.FindDocumentByRelPath("first/record.json");
			const DataDocument* second = store.FindDocumentByRelPath("second/record.json");
			const DataDocument* other = store.FindDocument("other-workspace:shared");
			return first && second && other && !first->valid && !second->valid && other->valid &&
			       first->validation_state == "invalid" && second->validation_state == "invalid" &&
			       store.GetInvalidCount() == 2 &&
			       store.FindDocument("dup-workspace:shared") == NULL && store.FindDocument("shared") == NULL;
		};
		if(!collision_store("forward", false) || !collision_store("reverse", true)) {
			Cerr() << "FAILED: duplicate qualified IDs or ambiguous unqualified lookups were order-dependent\n"; return false;
		}
		Cout() << "ok    duplicate qualified IDs invalidate all colliders independent of discovery order\n";
	}

	ValueMap legacy_cfg;
	legacy_cfg.Add("root", fixture_root);
	String valid_legacy_object = AsJSON(legacy_cfg, true);
	auto source_entry = [](const String& id, const String& root, bool enabled, bool read_only) {
		ValueMap entry;
		entry.Add("id", id);
		entry.Add("root", root);
		entry.Add("enabled", enabled);
		entry.Add("read_only", read_only);
		return entry;
	};
	auto manifest_content = [](const ValueArray& sources) {
		ValueMap manifest;
		manifest.Add("sources", sources);
		return AsJSON(manifest, true);
	};
	ValueArray one_source;
	one_source.Add(source_entry("fixture-source", fixture_root, true, true));
	ValueArray one_enabled_source;
	one_enabled_source.Add(source_entry("selected-source", fixture_root, true, true));
	one_enabled_source.Add(source_entry("inactive-source", fixture_root, false, true));
	ValueArray disabled_source;
	disabled_source.Add(source_entry("fixture-source", fixture_root, false, true));
	ValueArray multiple_enabled;
	multiple_enabled.Add(source_entry("source-one", fixture_root, true, true));
	multiple_enabled.Add(source_entry("source-two", fixture_root, true, true));
	ValueArray duplicate_ids;
	duplicate_ids.Add(source_entry("same-source", fixture_root, true, true));
	duplicate_ids.Add(source_entry("SAME-SOURCE", fixture_root, false, true));
	ValueArray unsafe_id;
	unsafe_id.Add(source_entry("../bad", fixture_root, true, true));
	ValueArray writable_source;
	writable_source.Add(source_entry("fixture-source", fixture_root, true, false));
	ValueArray relative_source;
	relative_source.Add(source_entry("fixture-source", "relative-root", true, true));
	ValueArray missing_source;
	missing_source.Add(source_entry("fixture-source", AppendFileName(fixture_root, "missing"), true, true));
	ValueMap missing_field;
	missing_field.Add("id", "fixture-source");
	missing_field.Add("enabled", true);
	missing_field.Add("read_only", true);
	ValueArray missing_field_sources;
	missing_field_sources.Add(missing_field);
	ValueArray empty_sources;
	ValueMap wrong_sources_type;
	wrong_sources_type.Add("sources", ValueMap());
	DataSourceRegistration parsed_registration;
	if(!DataStore::ParseRootRegistrationContent(fixture_root, parsed_registration) ||
	   parsed_registration.id != "configured-root" || parsed_registration.root != NormalizePath(fixture_root) || !parsed_registration.read_only ||
	   !DataStore::ParseRootRegistrationContent(valid_legacy_object, parsed_registration) ||
	   parsed_registration.id != "configured-root" || parsed_registration.root != NormalizePath(fixture_root) ||
	   !DataStore::ParseRootRegistrationContent(manifest_content(one_source), parsed_registration) ||
	   parsed_registration.id != "fixture-source" || parsed_registration.root != NormalizePath(fixture_root) || !parsed_registration.read_only ||
	   !DataStore::ParseRootRegistrationContent(manifest_content(one_enabled_source), parsed_registration) ||
	   parsed_registration.id != "selected-source" || parsed_registration.root != NormalizePath(fixture_root) || !parsed_registration.read_only ||
	   DataStore::ParseRootRegistrationContent(manifest_content(empty_sources), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(disabled_source), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(multiple_enabled), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(duplicate_ids), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(unsafe_id), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(writable_source), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(relative_source), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(missing_source), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(manifest_content(missing_field_sources), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(AsJSON(wrong_sources_type, true), parsed_registration) ||
	   DataStore::ParseRootRegistrationContent("{ malformed config", parsed_registration) ||
	   DataStore::ParseRootRegistrationContent("relative-data-root", parsed_registration) ||
	   DataStore::ParseRootRegistrationContent(AppendFileName(fixture_root, "missing"), parsed_registration) ||
	   DataStore::ParseRootConfigContent(fixture_root) != NormalizePath(fixture_root) ||
	   DataStore::ParseRootConfigContent(valid_legacy_object) != NormalizePath(fixture_root) ||
	   !DataStore::ParseRootConfigContent("{\"root\":42}").IsEmpty()) {
		Cerr() << "FAILED: DataStore root registration contract rejected a valid fixture or accepted an invalid one\n";
		return false;
	}
	Cout() << "ok    DataStore accepts one read-only source and rejects invalid registrations\n";

	{
		// Test TopologyGraph construction from fixture JSON
		TopologyGraph graph;
		Value parsed = ParseJSON(topology_json);
		if(!graph.FromJSON(parsed)) {
			Cerr() << "FAILED: TopologyGraph failed to parse fixture topology JSON!\n";
			return false;
		}
		if(graph.nodes.GetCount() != 2) {
			Cerr() << "FAILED: TopologyGraph expected 2 nodes!\n";
			return false;
		}
		if(graph.edges.GetCount() != 1) {
			Cerr() << "FAILED: TopologyGraph expected 1 edge!\n";
			return false;
		}
		Cout() << "ok    TopologyGraph parsed fixture data (2 nodes, 1 edge)\n";

		// Test DiagramRenderer
		String svg = DiagramRenderer::ToSVG(graph, false);
		if(svg.IsEmpty() || svg.Find("<svg") < 0) {
			Cerr() << "FAILED: DiagramRenderer failed to render SVG!\n";
			return false;
		}
		if(svg.Find("&lt;script&gt;alert(4)&lt;/script&gt;") < 0 ||
		   svg.Find("<script>alert(4)") >= 0 || svg.Find("javascript:") >= 0) {
			Cerr() << "FAILED: DiagramRenderer did not escape hostile labels or reject unsafe node URLs\n";
			return false;
		}
		Cout() << "ok    DiagramRenderer rendered TopologyGraph SVG\n";
	}

	// Point the running test server's generic store at the fixture root so the
	// data-view pages below exercise real documents (incl. array-of-maps).
	// The fixture stays on disk until the HTTP phase has finished.
	String v2_fixture_root = AppendFileName(workspace.root, "v2");
	RealizeDirectory(AppendFileName(v2_fixture_root, "goals/alpha"));
	RealizeDirectory(AppendFileName(v2_fixture_root, "goals/beta"));
	RealizeDirectory(AppendFileName(v2_fixture_root, "issues/alpha"));
	RealizeDirectory(AppendFileName(v2_fixture_root, "releases"));
	SaveFile(AppendFileName(v2_fixture_root, "goals/alpha/goal.json"),
		"{\"id\":\"shared-goal\",\"kind\":\"goal\",\"repo\":\"alpha\",\"title\":\"Synthetic full goal\",\"tags\":[\"one\",\"two\"],\"state\":{\"target\":\"ready\"},\"state_snapshots\":[{\"at\":\"synthetic\",\"value\":\"ready\"}],\"tasks\":[{\"kind\":\"task\",\"id\":\"t1\",\"title\":\"Task one\",\"order\":1,\"status\":\"planned\",\"gates\":{\"G1\":\"required\",\"G2\":\"review\"},\"sets\":{\"x\":\"y\"},\"children\":[\"child-a\"],\"notes\":\"synthetic note\",\"documents\":[\"doc-a\"],\"file_refs\":[\"refs/a.json\"]}],\"documents\":[\"goal-doc\"],\"file_refs\":[\"goal/files/a.txt\"]}");
	SaveFile(AppendFileName(v2_fixture_root, "goals/beta/goal.json"),
		"{\"id\":\"shared-goal\",\"kind\":\"goal\",\"repo\":\"beta\",\"title\":\"Ambiguous synthetic goal\",\"tags\":[],\"state\":{},\"tasks\":[]}");
	SaveFile(AppendFileName(v2_fixture_root, "goals/alpha/unique.json"),
		"{\"id\":\"unique-goal\",\"kind\":\"goal\",\"repo\":\"alpha\",\"title\":\"Unique synthetic goal\",\"tags\":[],\"state\":{},\"tasks\":[]}");
	SaveFile(AppendFileName(v2_fixture_root, "issues/alpha/issue.json"),
		"{\"id\":\"issue-a\",\"kind\":\"issue\",\"repo\":\"alpha\",\"title\":\"Issue\",\"status\":\"proposed\",\"notes\":\"keep this note\",\"documents\":[\"issue-doc\"],\"file_refs\":[\"issue/file\"]}");
	SaveFile(AppendFileName(v2_fixture_root, "releases/release.json"),
		"{\"id\":\"release-a\",\"kind\":\"release\",\"title\":\"Release\",\"status\":\"planned\",\"goals\":[\"alpha:goal:shared-goal\"],\"issues\":[\"alpha:issue:issue-a\"],\"documents\":[\"release-doc\"],\"file_refs\":[\"release/file\"]}");
	SaveFile(AppendFileName(v2_fixture_root, "issues/alpha/bad.json"), "{ malformed typed record");
	auto add_topology_fixture = [&](const String& id, const String& filename, const String& node_id) {
		ValueMap node; node.Add("id", node_id); node.Add("type", "service"); node.Add("name", node_id);
		ValueArray nodes; nodes.Add(node);
		ValueMap record; record.Add("id", id); record.Add("kind", "topology"); record.Add("name", "Synthetic topology " + node_id);
		record.Add("nodes", nodes); record.Add("edges", ValueArray());
		record.Add("envelope", record_envelope("topology", "topology", id));
		String path = AppendFileName(fixture_root, filename);
		RealizeDirectory(GetFileFolder(path));
		return SaveFile(path, AsJSON(record, true));
	};
	if(!add_topology_fixture("topology/synthetic/topology-002", "topo-extra/synthetic-2.json", "node-topology-two") ||
	   !add_topology_fixture("topology/other/topology-001", "topo-extra/other-1.json", "node-topology-other")) {
		Cerr() << "FAILED: could not write synthetic topology route records\n";
		return false;
	}
	{
		String shared_root = workspace.FixtureRoot("captured-registration");
		RealizeDirectory(AppendFileName(shared_root, "goals/synthetic"));
		SaveFile(AppendFileName(shared_root, "goals/synthetic/goal.json"),
			"{\"id\":\"captured\",\"kind\":\"goal\",\"repo\":\"synthetic\",\"title\":\"Captured root\",\"tags\":[],\"state\":{},\"tasks\":[],\"file_refs\":[\"/synthetic-only/private-marker\"]}");
		DataSourceRegistration registration;
		registration.id = "synthetic-captured-registration";
		registration.root = shared_root;
		registration.enabled = true;
		registration.read_only = true;
		One<V2Store> captured_v2; captured_v2.Create();
		One<DataStore> captured_data; captured_data.Create();
		String capture_error;
		Vector<String> capture_diagnostics;
		if(!LoadCatalogRegistration(registration, captured_v2, captured_data, capture_error, capture_diagnostics) ||
		   captured_v2->GetRootDir() != shared_root || captured_data->GetRootDir() != shared_root ||
		   captured_v2->GetSourceId() != registration.id || captured_data->GetSourceId() != registration.id ||
		   !captured_v2->FindGoalExact("synthetic:goal:captured")) {
			Cerr() << "FAILED: both catalog adapters did not load against one captured synthetic registration\n";
			return false;
		}
		CatalogGeneration captured_generation(10, pick(captured_v2), pick(captured_data), true, true);
		CatalogRecordRead unsafe_record = ReadCatalogRecord(captured_generation, "synthetic:goal:captured");
		if(unsafe_record.status != "unsafe_metadata" || CatalogRecordExitCode(unsafe_record) != 2 ||
		   AsJSON(unsafe_record.response, true).Find("synthetic-only") >= 0) {
			Cerr() << "FAILED: CLI record adapter exposed absolute path metadata\n";
			return false;
		}
		Cout() << "ok    both stores consume the same captured root/identity and CLI rejects absolute source metadata\n";
		String empty_source_root_a = workspace.FixtureRoot("empty-source-a");
		String empty_source_root_b = workspace.FixtureRoot("empty-source-b");
		if(!CreatePrivateDirectory(empty_source_root_a) || !CreatePrivateDirectory(empty_source_root_b)) {
			Cerr() << "FAILED: could not create empty catalog revision fixture\n";
			return false;
		}
		DataSourceRegistration empty_a;
		empty_a.id = "synthetic-empty-shared";
		empty_a.root = empty_source_root_a;
		DataSourceRegistration empty_b = empty_a;
		empty_b.root = empty_source_root_b;
		One<V2Store> empty_v2_a, empty_v2_b;
		One<DataStore> empty_data_a, empty_data_b;
		if(!LoadCatalogRegistration(empty_a, empty_v2_a, empty_data_a, capture_error, capture_diagnostics) ||
		   !LoadCatalogRegistration(empty_b, empty_v2_b, empty_data_b, capture_error, capture_diagnostics)) {
			Cerr() << "FAILED: could not load empty catalog revision fixtures\n";
			return false;
		}
		CatalogGeneration empty_generation_a(11, pick(empty_v2_a), pick(empty_data_a), true, true);
		CatalogGeneration empty_generation_b(12, pick(empty_v2_b), pick(empty_data_b), true, true);
		if(empty_generation_a.v2.GetGoals().GetCount() || empty_generation_a.data.GetDocuments().GetCount() ||
		   empty_generation_b.v2.GetGoals().GetCount() || empty_generation_b.data.GetDocuments().GetCount() ||
		   empty_generation_a.v2.GetSourceId() != empty_generation_b.v2.GetSourceId() ||
		   !is_sha256(empty_generation_a.source_revision) || !is_sha256(empty_generation_b.source_revision) ||
		   empty_generation_a.source_revision == empty_generation_b.source_revision) {
			Cerr() << "FAILED: empty source revision did not incorporate root identity with the same registration id\n";
			return false;
		}
		ValueMap revision_output;
		revision_output.Add("source_revision", empty_generation_a.source_revision);
		String revision_json = AsJSON(revision_output, true);
		if(revision_json.Find(empty_source_root_a) >= 0 || revision_json.Find(empty_source_root_b) >= 0) {
			Cerr() << "FAILED: source root path leaked through catalog revision metadata\n";
			return false;
		}
		Cout() << "ok    empty catalog revisions include hidden SHA-256 root identity and stable registration ID\n";
	}
	if(!app.RefreshCatalog()) {
		// A clean synthetic fixture is expected to publish complete source status.
		Cerr() << "FAILED: catalog refresh rejected the isolated synthetic fixture\n";
		return false;
	}
	{
		auto pinned = app.PinLatestCatalog();
		const V2Store& typed = pinned->v2;
		const V2Goal* qualified = typed.FindGoal("alpha:goal:shared-goal");
		bool parse_digest_valid = false;
		String parse_prefix = "issues/alpha/bad.json:parse_error:";
		for(const String& diagnostic : typed.GetSourceDiagnostics())
			if(diagnostic.StartsWith(parse_prefix) && is_sha256(diagnostic.Mid(parse_prefix.GetCount())))
				parse_digest_valid = true;
		if(!qualified || typed.FindGoal("shared-goal") ||
		   MapGet(qualified->source_data, "state_snapshots").IsNull() ||
		   MapGet(qualified->source_data, "documents").IsNull() ||
		   qualified->tasks.IsEmpty() || MapGet(qualified->tasks[0].source_data, "file_refs").IsNull() ||
		   typed.GetSourceErrorCount() != 1 || pinned->v2_status != "loaded_with_errors" || !parse_digest_valid) {
			Cerr() << "FAILED: typed catalog lost fields, resolved ambiguous ID, or hid source parse errors\n";
			return false;
		}
		const V2Issue* issue = typed.FindIssue("alpha:issue:issue-a");
		const V2Release* release = typed.FindRelease("release:release-a");
		if(!issue || MapGet(issue->source_data, "documents").IsNull() ||
		   !release || MapGet(release->source_data, "issues").IsNull() ||
		   MapGet(release->source_data, "file_refs").IsNull()) {
			Cerr() << "FAILED: issue/release catalog adapters dropped source fields or links\n";
			return false;
		}
		String revision = qualified->source_revision;
		String file = AppendFileName(v2_fixture_root, "goals/alpha/goal.json");
		String original = LoadFile(file);
		String edited_source = original.Left(original.GetCount() - 1) + ",\"edit_marker\":true}";
		SaveFile(file, edited_source);
		if(!app.RefreshCatalog()) { Cerr() << "FAILED: explicit catalog refresh failed after synthetic edit\n"; return false; }
		auto edited = app.PinLatestCatalog();
		const V2Goal* edited_goal = edited->v2.FindGoal("alpha:goal:shared-goal");
		if(!is_sha256(revision) || !edited_goal || !is_sha256(edited_goal->source_revision) ||
		   edited_goal->source_revision == revision || pinned->v2.FindGoal("alpha:goal:shared-goal")->source_revision != revision ||
		   edited->generation == pinned->generation) {
			Cerr() << "FAILED: catalog refresh mixed generations or missed source revision change\n";
			return false;
		}
		FileDelete(file);
		if(!app.RefreshCatalog() || app.PinLatestCatalog()->v2.FindGoal("alpha:goal:shared-goal")) {
			Cerr() << "FAILED: explicit catalog refresh did not publish source deletion\n";
			return false;
		}
		SaveFile(file, original);
		if(!app.RefreshCatalog()) { Cerr() << "FAILED: catalog restore refresh failed\n"; return false; }
		{
			auto request_generation = app.PinLatestCatalog();
			CatalogRequestPin request_pin(app);
			if(app.PinCatalog().Get() != request_generation.Get() || !app.RefreshCatalog() ||
			   app.PinCatalog().Get() != request_generation.Get() || app.PinLatestCatalog()->generation == request_generation->generation) {
				Cerr() << "FAILED: one request did not retain its pinned generation across atomic refresh\n";
				return false;
			}
		}
		{
			static_assert(std::is_same<decltype(std::declval<const CatalogReadHandle&>().Get()),
			                           const CatalogGeneration*>::value,
			              "catalog read handles must expose only const generations");
			CatalogReadHandle outer = app.PinLatestCatalog();
			uint64 outer_generation = outer->generation;
			{
				CatalogRequestPin outer_request(app);
				{
					CatalogRequestPin nested_request(app);
					if(app.PinCatalog()->generation != outer_generation) {
						Cerr() << "FAILED: nested request pin changed the outer generation\n";
						return false;
					}
				}
				if(app.PinCatalog()->generation != outer_generation) {
					Cerr() << "FAILED: nested request pin did not restore the outer generation\n";
					return false;
				}
			}
			std::atomic<bool> pin_failed(false);
			std::atomic<bool> reader_started(false);
			std::atomic<bool> finish_reader(false);
			Thread reader;
			reader.Run([&]() {
				reader_started.store(true);
				while(!finish_reader.load()) {
					CatalogReadHandle current = app.PinLatestCatalog();
					if(!current || current->generation == 0 || current->v2.GetGoals().IsEmpty())
						pin_failed.store(true);
				}
			});
			while(!reader_started.load()) Sleep(1);
			for(int i = 0; i < 8; i++)
				if(!app.RefreshCatalog()) pin_failed.store(true);
			finish_reader.store(true);
			reader.Wait();
			if(pin_failed.load() || outer->generation != outer_generation || outer->v2.GetGoals().IsEmpty()) {
				Cerr() << "FAILED: concurrent pin/refresh changed or invalidated a pinned generation\n";
				return false;
			}
		}
		{
			Ptr<CatalogGeneration> old_generation;
			{
				auto old = app.PinLatestCatalog();
				old_generation = const_cast<CatalogGeneration*>(old.Get());
				if(!app.RefreshCatalog() || !old_generation) {
					Cerr() << "FAILED: refresh did not retain an old generation while pinned\n";
					return false;
				}
				old = CatalogReadHandle();
			}
			if(old_generation) {
				Cerr() << "FAILED: retired generation stayed alive without request pins\n";
				return false;
			}
		}
		Cout() << "ok    immutable generations, source revisions/deletions, typed-field retention, ambiguity, and parse status\n";
	}
	Cout() << "ok    DataStore & Renderer fixture tests passed (store reloaded for HTTP views)\n";

	// 2. Start Skylark Server in Thread
	Thread server_thread;
	SelfTestServerGuard server_guard;
	server_guard.app = &app;
	server_thread.Run([&app]() {
		app.Run();
	});
	server_guard.thread = &server_thread;

	// Wait briefly for server to bind
	Sleep(500);

	// 3. HTTP Client Requests
	String base_url = Format("http://127.0.0.1:%d", port);
	auto is_disabled_response = [](int status, const String& body, const String& sentinel) {
		Value response = ParseJSON(body);
		return status == 503 && response.Is<ValueMap>() && response.GetCount() == 3 &&
		       response["status"] == "disabled" && response["available"] == false &&
		       response["error"] == "generic_operation_disabled" && body.Find(sentinel) < 0;
	};

	// Disabled host operations return a fixed response without reflecting inputs.
	{
		String sentinel = "synthetic_git_command_marker";
		HttpRequest req(base_url + "/api/host/remotegit");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.PostData("{\"node_id\":\"" + sentinel + "\",\"repo\":\"" + sentinel + "\",\"args\":\"" + sentinel + "\"}");
		req.Timeout(3000);
		req.Execute();
		String body = req.GetContent();
		if(!is_disabled_response(req.GetStatusCode(), body, sentinel)) {
			Cerr() << "FAILED: remote Git disabled response was not fixed/non-reflecting\n";
			return false;
		}
		Cout() << "ok    POST /api/host/remotegit (503, fixed disabled response)\n";
	}
	{
		String sentinel = "synthetic_build_flags_marker";
		HttpRequest req(base_url + "/api/host/fleet/build");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.PostData("{\"node_id\":\"" + sentinel + "\",\"package\":\"" + sentinel + "\",\"method\":\"" + sentinel + "\",\"flags\":\"" + sentinel + "\"}");
		req.Timeout(3000);
		req.Execute();
		String body = req.GetContent();
		if(!is_disabled_response(req.GetStatusCode(), body, sentinel)) {
			Cerr() << "FAILED: fleet build disabled response was not fixed/non-reflecting\n";
			return false;
		}
		Cout() << "ok    POST /api/host/fleet/build (503, fixed disabled response)\n";
	}
	{
		String sentinel = "synthetic_probe_path_marker";
		HttpRequest req(base_url + "/api/host/nodes/probe/" + sentinel);
		req.Timeout(3000);
		req.Execute();
		String body = req.GetContent();
		if(!is_disabled_response(req.GetStatusCode(), body, sentinel)) {
			Cerr() << "FAILED: node probe disabled response was not fixed/non-reflecting\n";
			return false;
		}
		Cout() << "ok    GET /api/host/nodes/probe (503, fixed disabled response)\n";
	}

	// Test GET /api/health
	{
		HttpRequest req(base_url + "/api/health");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || !res.Find("ManagerSkylark")) {
			Cerr() << "FAILED: /api/health returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    /api/health (200 OK)\n";
	}
	{
		HttpRequest write_route(base_url + "/api/data/write");
		write_route.ContentType("application/json");
		write_route.PostData("{}");
		write_route.Timeout(3000);
		write_route.Execute();
		if(write_route.GetStatusCode() != 404) {
			Cerr() << "FAILED: canonical write HTTP route unexpectedly registered (status="
			       << write_route.GetStatusCode() << ")\n";
			return false;
		}
		Cout() << "ok    no HTTP canonical write handler is registered (404)\n";
	}
	// T55 must remain a service-only capability; no canonical create URL is registered.
	{
		HttpRequest create_route(base_url + "/api/data/records/create");
		create_route.POST();
		create_route.ContentType("application/json");
		create_route.PostData("{}");
		create_route.Timeout(3000);
		create_route.Execute();
		if(create_route.GetStatusCode() != 404) {
			Cerr() << "FAILED: T55 canonical create route unexpectedly registered (status=" << create_route.GetStatusCode() << ")\n";
			return false;
		}
		Cout() << "ok    T55 canonical create HTTP route remains absent (404)\n";
	}

	// Verify protected data routes deny an anonymous request outside the
	// isolated-test principal, while the minimal health route remains public.
	{
		app.test_mode = false;
		HttpRequest private_req(base_url + "/api/data/kinds");
		private_req.Timeout(3000);
		private_req.Execute();
		bool denied = private_req.GetStatusCode() == 401;
		HttpRequest policy_req(base_url + "/api/policy/effective");
		policy_req.POST();
		policy_req.Header("Origin", base_url);
		policy_req.ContentType("application/json");
		policy_req.PostData("{}");
		policy_req.Timeout(3000);
		policy_req.Execute();
		bool policy_denied = policy_req.GetStatusCode() == 401;
		HttpRequest spec_req(base_url + "/api/data/spec");
		spec_req.Timeout(3000);
		spec_req.Execute();
		HttpRequest refresh_req(base_url + "/api/data/refresh");
		refresh_req.POST();
		refresh_req.Header("Origin", base_url);
		refresh_req.ContentType("application/json");
		refresh_req.PostData("{}");
		refresh_req.Timeout(3000);
		uint64 generation_before = app.PinLatestCatalog()->generation;
		refresh_req.Execute();
		HttpRequest assessment_api_req(base_url + "/api/assessments?workspace=synthetic-workspace&run=synthetic-workspace%3Arun");
		assessment_api_req.Timeout(3000); assessment_api_req.Execute();
		bool catalog_routes_denied = spec_req.GetStatusCode() == 401 && refresh_req.GetStatusCode() == 401 &&
		                             assessment_api_req.GetStatusCode()==401&&
		                             app.PinLatestCatalog()->generation == generation_before;
		HttpRequest health_req(base_url + "/api/health");
		health_req.Timeout(3000);
		health_req.Execute();
		bool health_ok = health_req.GetStatusCode() == 200;
		app.test_mode = true;
		if(!denied || !policy_denied || !catalog_routes_denied || !health_ok) {
			Cerr() << "FAILED: auth boundary (anonymous data status=" << private_req.GetStatusCode()
			       << ", policy=" << policy_req.GetStatusCode() << ", spec=" << spec_req.GetStatusCode() << ", refresh=" << refresh_req.GetStatusCode()
			       << ", assessment api="<<assessment_api_req.GetStatusCode()<<", health status=" << health_req.GetStatusCode() << ")\n";
			return false;
		}
		Cout() << "ok    auth boundary (anonymous policy/data denied; health public)\n";
	}
	// T71 job API is workspace-authorized and exposes only the redacted status projection.
	{
		DataSourceRegistration prior_registration=app.test_registration;
		bool prior_provider=app.test_registration_provider;
		String prior_fixture_root=app.test_fixture_root;
		String ws="t71-http-workspace",source="t71-http-source",root=workspace.FixtureRoot("t71-http-store");
		if(!CreatePrivateDirectory(root)||!RealizeDirectory(AppendFileName(root,"v2"))||!RealizeDirectory(AppendFileName(root,"data"))) return false;
		app.test_fixture_root=root; app.test_registration_provider=true; app.test_registration.id=source; app.test_registration.root=NormalizePath(root); app.test_registration.enabled=true;
		app.test_registration.read_only=false; app.test_registration.write_enabled=true;
		if(!app.RefreshCatalog()) return false;
		UserSession session=SystemAuthService::CreateTestSessionForTests(); if(!session.is_authenticated||session.token.IsEmpty()) return false;
		String job_id="durable-job-t71-http";
		ValueMap request_value; request_value.Add("inputs",ValueArray()); request_value.Add("budget_ms",1000); request_value.Add("max_attempts",1); request_value.Add("max_output_bytes",128); request_value.Add("provider_id","t71-private-provider-marker"); request_value.Add("model_config_revision","t71-private-revision-marker");
		ValueMap policy_value; policy_value.Add("fingerprint",String('b',64)); policy_value.Add("secret_state","referenced_redacted"); policy_value.Add("decisions",ValueArray());
		ValueMap job; job.Add("id",job_id); job.Add("kind","durable-job"); job.Add("workspace_id",ws); job.Add("revision","1"); job.Add("provenance","computed"); job.Add("state","queued"); job.Add("request_id","synthetic"); job.Add("request",request_value); job.Add("operation_key","t71-http"); job.Add("request_digest",String('c',64)); job.Add("inputs",ValueArray()); job.Add("contributors",ValueArray()); job.Add("provider_id","t71-private-provider-marker"); job.Add("model_config_revision","t71-private-revision-marker"); job.Add("policy_snapshot",policy_value); job.Add("attempt",0); job.Add("attempt_id",String()); job.Add("created_at","synthetic-created"); job.Add("updated_at","synthetic-updated"); job.Add("started_at",String()); job.Add("finished_at",String()); job.Add("budget_ms",1000); job.Add("max_attempts",1); job.Add("max_output_bytes",128); job.Add("output_ref",String()); job.Add("output_hash",String()); job.Add("failure_class",String()); job.Add("retryable",false); job.Add("idempotency_safe",false); job.Add("event_ids",ValueArray()); job.Add("run_ids",ValueArray());
		ValueMap env; env.Add("schema_id","skylark/durable-job"); env.Add("schema_version","2"); env.Add("workspace_id",ws); env.Add("stable_id",job_id); env.Add("record_kind","durable-job"); env.Add("revision","1"); env.Add("visibility","private"); env.Add("provenance_ref","computed"); job.Add("envelope",env);
		String job_path=AppendFileName(root,"data/t71-job.json");
		if(!SaveFile(job_path,AsJSON(job,true))||!app.RefreshCatalog()) { Cerr()<<"FAILED T71 HTTP synthetic job fixture load\n"; app.test_registration=prior_registration; app.test_registration_provider=prior_provider; app.test_fixture_root=prior_fixture_root; app.RefreshCatalog(); return false; }
		auto get=[&](const String& path,const String& cookie,int& status) { HttpRequest req(base_url+path); if(!cookie.IsEmpty()) req.Header("Cookie","wf_auth_session="+UrlEncode(cookie)); req.Timeout(3000); String body=req.Execute(); status=req.GetStatusCode(); return body; };
		int list_status=0,detail_status=0; String query="?workspace="+UrlEncode(ws);
		String list=get("/api/jobs"+query,session.token,list_status),detail=get("/api/jobs/"+UrlEncode(job_id)+query,session.token,detail_status);
		ValueMap list_json=ParseJSON(list),detail_json=ParseJSON(detail);
		ValueArray visible_jobs=MapGet(list_json,"jobs");
		bool exact_status=detail_json.GetCount()==8&&MapGetString(detail_json,"id")==job_id&&MapGetString(detail_json,"state")=="queued";
		int anonymous_status=0; String anonymous_body=get("/api/jobs"+query,String(),anonymous_status);
		int wrong_workspace_status=0; String wrong_workspace_body=get("/api/jobs?workspace=t71-unregistered-workspace",session.token,wrong_workspace_status);
		int submit_status=0; HttpRequest submit(base_url+"/api/jobs/submit"); submit.POST(); submit.Header("Cookie","wf_auth_session="+UrlEncode(session.token)); submit.Header("Origin",base_url); submit.Header("X-Workspace-ID",ws); submit.ContentType("application/json"); submit.PostData("{\"source_content\":\"T71-PROMPT-MUST-NOT-BE-PARSED\"}"); submit.Timeout(3000); String submit_body=submit.Execute(); submit_status=submit.GetStatusCode();
		String combined=list+detail+submit_body;
		bool no_sensitive=combined.Find("t71-private-provider-marker")<0&&combined.Find("t71-private-revision-marker")<0&&combined.Find("secretref:")<0&&combined.Find("T71-PROMPT-MUST-NOT-BE-PARSED")<0&&combined.Find("raw-output-marker")<0;
		CatalogReadHandle after=app.PinLatestCatalog(); int job_count=0; for(const DataDocument& d:after->data.GetDocuments()) if(d.workspace_id==ws&&d.kind=="durable-job") job_count++;
		app.test_registration=prior_registration; app.test_registration_provider=prior_provider; app.test_fixture_root=prior_fixture_root; bool restored=app.RefreshCatalog();
		bool denied_redacted=anonymous_body.Find(job_id)<0&&anonymous_body.Find("jobs")<0&&wrong_workspace_body.Find(job_id)<0&&wrong_workspace_body.Find("jobs")<0;
		if(!restored||list_status!=200||detail_status!=200||visible_jobs.GetCount()!=1||!exact_status||(anonymous_status!=401&&anonymous_status!=403)||wrong_workspace_status!=403||submit_status!=503||!denied_redacted||!no_sensitive||job_count!=1) {
			Cerr()<<"FAILED T71 job API status/redaction/disabled contract list="<<list_status<<" detail="<<detail_status<<" anonymous="<<anonymous_status<<" workspace="<<wrong_workspace_status<<" submit="<<submit_status<<"\n"; return false;
		}
		Cout()<<"ok    T71 authenticated workspace job status, anonymous/foreign-workspace denial, redaction, and disabled POST without queueing\n";
	}
	{
		UserSession session=SystemAuthService::CreateTestSessionForTests();
		HttpRequest api(base_url+"/api/assessments"); api.Header("Cookie","wf_auth_session="+UrlEncode(session.token)); api.Timeout(3000); String body=api.Execute();
		HttpRequest html(base_url+"/assessments"); html.Header("Cookie","wf_auth_session="+UrlEncode(session.token)); html.Timeout(3000); String page=html.Execute();
		ValueMap expected=AssessmentViewService::Build(app,*app.PinLatestCatalog(),String(),String(),String(),String(),String(),String(),String());
		ValueMap actual=ParseJSON(body);
		if(api.GetStatusCode()!=200||html.GetStatusCode()!=200||AsJSON(expected,true)!=AsJSON(actual,true)||page.Find("Research assessment overview")<0||page.Find("No assessment data is available")<0) {
			Cerr()<<"FAILED T17 authenticated API/HTML route parity: "<<api.GetStatusCode()<<"/"<<html.GetStatusCode()<<"\n"; return false;
		}
		Cout()<<"ok    T17 authenticated API and HTML routes consume the shared pinned view result\n";
	}
	{
		const String ws="synthetic-t17-workspace", source="synthetic-t17-source", denied_source="synthetic-t17-denied";
		String root=workspace.FixtureRoot("t17-populated");
		RealizeDirectory(AppendFileName(root,"goals")); RealizeDirectory(AppendFileName(root,"issues")); RealizeDirectory(AppendFileName(root,"releases"));
		DataSourceRegistration prior_registration=app.test_registration;
		DataSourceRegistration prior_change=app.test_registration_change;
		bool prior_provider=app.test_registration_provider;
		int prior_change_point=app.test_registration_change_point;
		String prior_scanned_root=app.test_registration_scanned_root;
		String prior_fixture_root=app.test_fixture_root;
		PolicySettings prior_instance=app.instance_policy,prior_workspace=app.policy_workspace_settings,prior_operator=app.policy_operator_settings;
		TrustedPolicyRegistry prior_registry=pick(app.trusted_policy_registry);
		CatalogReadHandle prior_catalog=app.PinLatestCatalog(); String prior_source_revision=prior_catalog?prior_catalog->source_revision:String();
		auto exercise_populated_routes=[&]() -> bool {
			app.test_registration_provider=true; app.test_registration.id="synthetic-t17-http"; app.test_registration.root=NormalizePath(root);
			app.test_registration.enabled=true; app.test_registration.read_only=true; app.test_registration.write_enabled=false;
			app.test_registration_change_point=0; app.test_registration_scanned_root.Clear();
			if(!app.RefreshCatalog()) return false;
			CatalogReadHandle pin=app.PinLatestCatalog(); if(!pin||pin->status!="loaded"||pin->data_status!="loaded") return false;
			TrustedPolicyRegistry registry; registry.configured=true; PolicyWorkspaceFact wf; wf.identity.workspace_id=ws; wf.identity.object_id=ws; registry.workspaces.Add(wf);
			for(const String& identity:{source,denied_source}) { PolicyIdentity id; id.workspace_id=ws; id.object_id=identity; PolicySourceFact sf; sf.workspace.workspace_id=ws; sf.workspace.object_id=ws; sf.identity=id; sf.classification=POLICY_PUBLIC; sf.visible=identity!=denied_source; registry.sources.Add(sf); PolicyProviderFact pf; pf.workspace.workspace_id=ws; pf.workspace.object_id=ws; pf.identity=id; pf.training_policy="eligible"; pf.enabled=true; pf.config_revision="synthetic-r1"; pf.secret_reference="secretref:synthetic-rest"; registry.providers.Add(pf); }
			PolicySettings instance; instance.configured=true; PolicySettings workspace_policy,operator_policy;
			if(!app.InstallSyntheticPolicyContextForTests(instance,workspace_policy,operator_policy,registry)) return false;
			String run_record=LoadFile(AppendFileName(AppendFileName(root,"data"),"record-0.json")); String run_id=MapGetString(ParseJSON(run_record),"id");
			String run_qid=ws+":"+run_id, query="workspace="+UrlEncode(ws)+"&run="+UrlEncode(run_qid);
			UserSession session=SystemAuthService::CreateTestSessionForTests(); if(!session.is_authenticated||session.token.IsEmpty()) return false;
			auto get=[&](const String& path,int& status) { HttpRequest request(base_url+path); request.Header("Cookie","wf_auth_session="+UrlEncode(session.token)); request.Timeout(3000); String body=request.Execute(); status=request.GetStatusCode(); return body; };
			int api_status=0,html_status=0,compare_api_status=0,compare_html_status=0;
			String api_body=get("/api/assessments?"+query,api_status),html_body=get("/assessments?"+query,html_status);
			String compare_api=get("/api/assessments/compare?"+query,compare_api_status),compare_html=get("/assessments/compare?"+query,compare_html_status);
			ValueMap api_view=ParseJSON(api_body), expected=AssessmentViewService::Build(app,*pin,ws,run_qid,"","","","","");
			ValueArray rows=MapGet(expected,"rows"); if(rows.IsEmpty()) return false;
		String detail_id;
		for(const Value& row_value:rows) if(MapGetString((ValueMap)row_value,"candidate_id")=="synthetic-candidate-0") detail_id=MapGetString((ValueMap)row_value,"id");
		if(detail_id.IsEmpty()) return false;
		String detail_path="/assessments/detail/"+UrlEncode(detail_id)+"?"+query+"&rubric="+UrlEncode("generic-rubric")+"&inputs="+UrlEncode(AsJSON(MapGet(expected,"input_revisions"),true));
			String api_detail_path="/api/assessments/detail/"+UrlEncode(detail_id)+"?"+query;
			int detail_api_status=0,detail_html_status=0; String detail_api=get(api_detail_path,detail_api_status),detail_html=get(detail_path,detail_html_status);
			ValueMap expected_detail=AssessmentViewService::Build(app,*pin,ws,run_qid,"","","","",detail_id),api_detail_view=ParseJSON(detail_api);
			String secret=denied_source;
			if(api_status!=200||html_status!=200||compare_api_status!=200||compare_html_status!=200||detail_api_status!=200||detail_html_status!=200||
			   AsJSON(api_view,true)!=AsJSON(expected,true)||AsJSON(ParseJSON(compare_api),true)!=AsJSON(expected,true)||
			   AsJSON(api_detail_view,true)!=AsJSON(expected_detail,true)||MapGet(expected,"candidate_count")!=5||MapGet(expected,"unique_visible_source_count")!=1||MapGet(expected,"unique_visible_source_revision_count")!=2||
			   html_body.Find("source-r1")<0||html_body.Find("source-r2")<0||html_body.Find("input-a")<0||api_body.Find("synthetic-candidate-5")>=0||html_body.Find("synthetic-candidate-5")>=0||compare_api.Find("synthetic-candidate-5")>=0||compare_html.Find("synthetic-candidate-5")>=0||detail_api.Find("synthetic-candidate-5")>=0||detail_html.Find("synthetic-candidate-5")>=0||
			   compare_html.Find("research-ready")<0||compare_html.Find("paper-backed")<0||compare_html.Find("interesting-risk")<0||compare_html.Find("avoid-for-now")<0||compare_html.Find("0/1 to 6/1")<0||compare_html.Find("workspace="+UrlEncode(ws))<0||compare_html.Find("source_revision_changed")<0||compare_html.Find("analysis-run-t17-replacement")<0||
			   compare_html.Find("&lt;em&gt;T17 category rationale 0&lt;/em&gt;")<0||compare_html.Find("crowding judgment</strong>: T17 crowding judgment 0")<0||compare_html.Find("opportunity gap</strong>: T17 opportunity gap 0")<0||
			   detail_html.Find("&lt;img src=x onerror=alert(1)&gt;")<0||detail_html.Find("t17-evidence-item")<0||detail_html.Find("synthetic-paper")<0||detail_html.Find("source-r2")<0||detail_html.Find("run="+UrlEncode(run_qid))<0||detail_html.Find("inputs=")<0||detail_html.Find("&lt;em&gt;T17 category rationale 0&lt;/em&gt;")<0||detail_html.Find("crowding judgment</strong>: T17 crowding judgment 0")<0||detail_html.Find("opportunity gap</strong>: T17 opportunity gap 0")<0||
			   api_body.Find(secret)>=0||html_body.Find(secret)>=0||compare_api.Find(secret)>=0||compare_html.Find(secret)>=0||detail_api.Find(secret)>=0||detail_html.Find(secret)>=0||AsJSON(api_view,true).Find("minimum_numerator")<0||AsJSON(api_view,true).Find("source-r1")<0||AsJSON(api_view,true).Find("source-r2")<0||
			   api_body.Find("<em>T17 category rationale 0</em>")<0||api_body.Find("T17 crowding judgment 0")<0||api_body.Find("T17 opportunity gap 0")<0||compare_api.Find("<em>T17 category rationale 0</em>")<0||compare_api.Find("T17 crowding judgment 0")<0||compare_api.Find("T17 opportunity gap 0")<0||detail_api.Find("<em>T17 category rationale 0</em>")<0||detail_api.Find("T17 crowding judgment 0")<0||detail_api.Find("T17 opportunity gap 0")<0) {
				Cerr()<<"T17 category route fields did not match persisted qualitative values\n";
				return false;
			}
			ValueArray stale=MapGet(expected,"stale_reasons"); if(stale.IsEmpty()||MapGet((ValueMap)stale[0],"replacement_snapshot").IsVoid()||AsJSON(MapGet((ValueMap)stale[0],"reason_flags"),true).Find("source_revision_changed")<0) return false;
			String partial_root=workspace.FixtureRoot("t17-partial-catalog"); app.test_registration.root=NormalizePath(partial_root);
			if(!app.RefreshCatalog()) return false;
			int partial_status=0; String partial_body=get("/api/assessments?"+query,partial_status); ValueMap partial_view=ParseJSON(partial_body); ValueArray partial_rows=MapGet(partial_view,"rows");
			if(partial_status!=200||MapGetString(partial_view,"view_state")!="incomplete"||!partial_rows.IsEmpty()||partial_view.Find("candidate_count")>=0) return false;
			return true;
		};
		bool routes_ok=exercise_populated_routes();
		app.instance_policy=prior_instance; app.policy_workspace_settings=prior_workspace; app.policy_operator_settings=prior_operator; app.trusted_policy_registry=pick(prior_registry);
		app.test_registration=prior_registration; app.test_registration_change=prior_change; app.test_registration_provider=prior_provider;
		app.test_registration_change_point=prior_change_point; app.test_fixture_root=prior_fixture_root;
		bool restored=app.RefreshCatalog(); app.test_registration_scanned_root=prior_scanned_root;
		CatalogReadHandle restored_catalog=app.PinLatestCatalog();
		if(!routes_ok||!restored||!restored_catalog||restored_catalog->source_revision!=prior_source_revision) {
			Cerr()<<"FAILED T17 populated authenticated routes or state restoration (routes="<<routes_ok<<", restore="<<restored<<")\n"; return false;
		}
		Cout()<<"ok    T17 populated authenticated overview/detail/compare HTTP parity, source revision pins, staleness, denied-source suppression, incomplete-catalog denial and registration/policy restoration\n";
	}
	// T66 proposal mutation integration stays inside the existing isolated T17 fixture root.
	{
		const String ws="synthetic-t17-workspace", source="synthetic-t17-source", counter_source="synthetic-t67-counter-source", write_source="synthetic-t17-http";
		String root=workspace.FixtureRoot("t17-populated");
		DataSourceRegistration prior=app.test_registration, prior_change=app.test_registration_change;
		bool prior_provider=app.test_registration_provider; int prior_change_point=app.test_registration_change_point;
		PolicySettings prior_instance=app.instance_policy, prior_workspace=app.policy_workspace_settings, prior_operator=app.policy_operator_settings;
		TrustedPolicyRegistry prior_registry=pick(app.trusted_policy_registry);
		app.test_registration_provider=true; app.test_registration.id="synthetic-t17-http"; app.test_registration.root=NormalizePath(root);
		app.test_registration.enabled=true; app.test_registration.read_only=false; app.test_registration.write_enabled=true;
		app.test_registration_change=app.test_registration; app.test_registration_change_point=0;
		TrustedPolicyRegistry registry; registry.configured=true; PolicyWorkspaceFact wf; wf.identity.workspace_id=ws; wf.identity.object_id=ws; registry.workspaces.Add(wf);
		PolicyIdentity source_identity; source_identity.workspace_id=ws; source_identity.object_id=source;
		PolicySourceFact sf; sf.workspace=wf.identity; sf.identity=source_identity; sf.classification=POLICY_PUBLIC; sf.visible=true; registry.sources.Add(sf);
		PolicyIdentity counter_source_identity; counter_source_identity.workspace_id=ws; counter_source_identity.object_id=counter_source; PolicySourceFact counter_sf; counter_sf.workspace=wf.identity; counter_sf.identity=counter_source_identity; counter_sf.classification=POLICY_PUBLIC; counter_sf.visible=true; registry.sources.Add(counter_sf);
		PolicyProviderFact pf; pf.workspace=wf.identity; pf.identity=source_identity; pf.training_policy="eligible"; pf.enabled=true; pf.config_revision="synthetic-r1"; pf.secret_reference="secretref:synthetic-rest"; registry.providers.Add(pf);
		PolicySettings instance; instance.configured=true; PolicySettings no_overrides;
		bool setup=app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry) && app.RefreshCatalog();
		CatalogReadHandle pin=setup?app.PinLatestCatalog():CatalogReadHandle();
		UserSession live=SystemAuthService::CreateTestSessionForTests();
		bool early_write_authorized=SystemAuthService::AuthorizeWriteForSession(app,live,ws);
		bool early_anonymous_denied=!SystemAuthService::AuthorizeWriteForSession(app,UserSession(),ws);
		bool early_wrong_workspace_denied=!SystemAuthService::AuthorizeWriteForSession(app,live,"other-synthetic-workspace");
		ValueMap form; form.Add("candidate_kind","proposal");
		for(const char *k:{"title","purpose","quality_contract","inputs","outputs","runtime_resources","assets","verification_plan"}) form.Add(k,String("Synthetic T66 ")+k);
		ValueArray links; links.Add("t17-evidence-item"); form.Add("evidence_links",links);
		if(pin) {
			auto invalid_form=[&](const char *field,const Value& value) { ValueMap bad=form; bad.Set(field,value); return MapGetString(CandidateWorkflow::ValidateProposal(pin->data,bad,ws,app),"status")=="invalid"; };
			ValueArray malformed; malformed.Add(7); ValueArray empty_id; empty_id.Add(""); ValueArray oversized; oversized.Add(String(257,'x')); ValueArray repeated; repeated.Add("t17-evidence-item"); repeated.Add("t17-evidence-item");
			Value wrong_type=7;
			ValueMap hypothesis=form; hypothesis.Set("hypothetical",true); ValueArray counter; counter.Add("t17-evidence-item"); hypothesis.Add("counterevidence",counter); hypothesis.Add("uncertainty","uncertain");
			ValueMap bad_hypothesis=hypothesis; bad_hypothesis.Set("counterevidence",malformed);
			ValueMap empty_counter=hypothesis; empty_counter.Set("counterevidence",empty_id);
			ValueMap long_counter=hypothesis; long_counter.Set("counterevidence",oversized);
			ValueMap duplicate_counter=hypothesis; duplicate_counter.Set("counterevidence",repeated);
			ValueMap unknown_uncertainty=hypothesis; unknown_uncertainty.Set("uncertainty","unknown");
			ValueMap na_uncertainty=hypothesis; na_uncertainty.Set("uncertainty","n/a");
			ValueMap unexpected=form; unexpected.Add("counterevidence",counter);
			if(!invalid_form("evidence_links",malformed)||!invalid_form("evidence_links",empty_id)||!invalid_form("evidence_links",oversized)||!invalid_form("evidence_links",repeated)||!invalid_form("evidence_links",wrong_type)||
			   MapGetString(CandidateWorkflow::ValidateProposal(pin->data,bad_hypothesis,ws,app),"status")!="invalid"||MapGetString(CandidateWorkflow::ValidateProposal(pin->data,empty_counter,ws,app),"status")!="invalid"||
			   MapGetString(CandidateWorkflow::ValidateProposal(pin->data,long_counter,ws,app),"status")!="invalid"||MapGetString(CandidateWorkflow::ValidateProposal(pin->data,duplicate_counter,ws,app),"status")!="invalid"||
			   MapGetString(CandidateWorkflow::ValidateProposal(pin->data,unknown_uncertainty,ws,app),"status")!="invalid"||MapGetString(CandidateWorkflow::ValidateProposal(pin->data,na_uncertainty,ws,app),"status")!="invalid"||
			   MapGetString(CandidateWorkflow::ValidateProposal(pin->data,unexpected,ws,app),"status")!="invalid") setup=false;
		}
		auto call_create=[&](const ValueMap& f,const String& op,const UserSession& user) {
			return CandidateWorkflow::CreateProposal(pin->data,app,f,ws,write_source,op,user);
		};
		ValueMap no_session=call_create(form,"t66-no-session",UserSession());
		ValueMap missing_private=form; ValueArray missing_links; missing_links.Add("deliberately-missing-private-evidence"); missing_private.Set("evidence_links",missing_links);
		ValueMap denied_missing=call_create(missing_private,"t66-denied-missing",UserSession());
		UserSession expired=SystemAuthService::CreateTestSessionForTests(); SystemAuthService::ExpireSessionForTest(expired.token);
		ValueMap expired_result=call_create(form,"t66-expired",expired);
		ValueMap wrong_workspace=CandidateWorkflow::CreateProposal(pin->data,app,missing_private,"other-synthetic-workspace",write_source,"t66-wrong-workspace",live);
		ValueMap wrong_source=CandidateWorkflow::CreateProposal(pin->data,app,missing_private,ws,"other-synthetic-source","t66-wrong-source",live);
		ValueMap placeholder_form=form; placeholder_form.Set("title","N/A");
		ValueMap placeholder=call_create(placeholder_form,"t66-placeholder",live);
		sf.visible=false; registry.sources.Set(0,sf); app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
		ValueMap hidden_source_result=call_create(form,"t66-hidden-source",live);
		sf.visible=true; registry.sources.Set(0,sf); app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
		app.test_registration.read_only=true; app.test_registration.write_enabled=false; bool readonly_loaded=app.RefreshCatalog(); CatalogReadHandle readonly_pin=app.PinLatestCatalog();
		ValueMap readonly_result=readonly_loaded&&readonly_pin?CandidateWorkflow::CreateProposal(readonly_pin->data,app,form,ws,write_source,"t66-readonly",live):ValueMap();
		app.test_registration.read_only=false; app.test_registration.write_enabled=true; bool writable_restored=app.RefreshCatalog(); pin=app.PinLatestCatalog();
		int records_before_create=pin?pin->data.GetDocuments().GetCount():-1;
		String old_bytes; const DataDocument *old_candidate=NULL;
		ValueMap created=call_create(form,"t66-create-operation",live);
		String cid=MapGetString(created,"candidate_id"), pid=MapGetString(created,"proposal_id"), r1=MapGetString(created,"revision_id");
		CatalogReadHandle after_create=app.PinLatestCatalog();
		int records_after_create=after_create->data.GetDocuments().GetCount();
		bool auth_after_create=SystemAuthService::AuthorizeWriteForSession(app,live,write_source,ws);
		ValueMap retry=CandidateWorkflow::CreateProposal(after_create->data,app,form,ws,write_source,"t66-create-operation",live);
		CatalogReadHandle after_retry=app.PinLatestCatalog();
		ValueMap create_body; create_body.Add("workspace_id",ws); create_body.Add("operation_id","t66-create-operation"); create_body.Add("form",form);
		HttpRequest create_repeat_request(base_url+"/api/candidates/proposal"); create_repeat_request.POST(); create_repeat_request.Header("Origin",base_url);
		create_repeat_request.Header("Cookie","wf_auth_session="+UrlEncode(live.token)); create_repeat_request.ContentType("application/json");
		create_repeat_request.PostData(AsJSON(create_body,true)); create_repeat_request.Timeout(3000);
		String create_repeat_body=create_repeat_request.Execute(); ValueMap create_repeat_result(ParseJSON(create_repeat_body));
		ValueMap wrong_workspace_body; wrong_workspace_body.Add("workspace_id","other-synthetic-workspace"); wrong_workspace_body.Add("operation_id","t66-route-wrong-workspace"); wrong_workspace_body.Add("form",missing_private);
		HttpRequest wrong_workspace_request(base_url+"/api/candidates/proposal"); wrong_workspace_request.POST(); wrong_workspace_request.Header("Origin",base_url);
		wrong_workspace_request.Header("Cookie","wf_auth_session="+UrlEncode(live.token)); wrong_workspace_request.ContentType("application/json");
		wrong_workspace_request.PostData(AsJSON(wrong_workspace_body,true)); wrong_workspace_request.Timeout(3000);
		String wrong_workspace_body_response=wrong_workspace_request.Execute(); ValueMap wrong_workspace_response;
		if(!wrong_workspace_body_response.IsEmpty() && ParseJSON(wrong_workspace_body_response).Is<ValueMap>()) wrong_workspace_response=ParseJSON(wrong_workspace_body_response);
		HttpRequest foreign_origin_request(base_url+"/api/candidates/proposal"); foreign_origin_request.POST(); foreign_origin_request.Header("Origin","https://attacker.invalid");
		foreign_origin_request.ContentType("application/json"); foreign_origin_request.PostData(AsJSON(create_body,true)); foreign_origin_request.Timeout(3000);
		foreign_origin_request.Execute(); int foreign_origin_status=foreign_origin_request.GetStatusCode();
		ValueMap stage_foreign_body; stage_foreign_body.Add("workspace_id",ws); stage_foreign_body.Add("candidate_id","unknown-candidate"); stage_foreign_body.Add("proposal_revision_id","unknown-revision"); stage_foreign_body.Add("expected_candidate_revision","1"); stage_foreign_body.Add("expected_head_revision","1"); stage_foreign_body.Add("expected_head_event_id","unknown-event"); stage_foreign_body.Add("next_stage","source-inspected"); stage_foreign_body.Add("reason","synthetic origin check"); stage_foreign_body.Add("evidence_ids",ValueArray()); stage_foreign_body.Add("operation_id","t67-origin-check"); stage_foreign_body.Add("review_outcome","");
		HttpRequest stage_foreign_request(base_url+"/api/candidates/stage"); stage_foreign_request.POST(); stage_foreign_request.Header("Origin","https://attacker.invalid"); stage_foreign_request.ContentType("application/json"); stage_foreign_request.PostData(AsJSON(stage_foreign_body,true)); stage_foreign_request.Timeout(3000); stage_foreign_request.Execute(); int stage_foreign_origin_status=stage_foreign_request.GetStatusCode();
		ValueMap changed_form=form; changed_form.Set("title","Changed payload under same operation");
		ValueMap changed=CandidateWorkflow::CreateProposal(after_retry->data,app,changed_form,ws,write_source,"t66-create-operation",live);
		String old_proposal_bytes, old_form_bytes, old_form_id;
		for(const DataDocument& d:after_create->data.GetDocuments()) if(d.workspace_id==ws) {
			if(d.id==cid) { old_candidate=&d; old_bytes=LoadFile(d.path); }
			if(d.kind=="proposal-revision"&&d.id==r1) old_proposal_bytes=LoadFile(d.path);
			if(d.kind=="candidate-proposal"&&MapGetString(d.data,"proposal_revision_id")==r1) { old_form_bytes=LoadFile(d.path); old_form_id=d.id; }
		}
		String head_id="candidate-proposal-head-"+DataStore::ContentDigest(ws+"\n"+pid).Left(32);
		const DataDocument *proposal_head=after_create->data.FindDocument(ws+":"+head_id);
		String r1_stage_head_id="candidate-stage-head-"+DataStore::ContentDigest(ws+"\n"+cid+"\n"+r1).Left(32);
		const DataDocument *r1_stage_head=after_create->data.FindDocument(ws+":"+r1_stage_head_id);
		const DataDocument *r1_draft_event=r1_stage_head?after_create->data.FindDocument(ws+":"+MapGetString(r1_stage_head->data,"event_id")):NULL;
		String r1_draft_event_bytes=r1_draft_event?LoadFile(r1_draft_event->path):String();
		ValueArray r1_stage_evidence; r1_stage_evidence.Add("t17-evidence-item");
		ValueMap r1_draft_projection=CandidateWorkflow::Project(after_create->data,app,ws,cid);
		ValueMap r1_source_stage=r1_stage_head?CandidateWorkflow::AdvanceStage(after_create->data,app,ws,cid,r1,old_candidate?old_candidate->revision:String(),r1_stage_head->revision,MapGetString(r1_stage_head->data,"event_id"),"source-inspected","r1 source inspection",r1_stage_evidence,write_source,"t67-r1-source",live):ValueMap();
		CatalogReadHandle after_r1_stage=app.PinLatestCatalog();
		String r1_stage_event_id="candidate-stage-event-"+DataStore::ContentDigest("t67-r1-source\n"+cid+"\n"+r1+"\nsource-inspected").Left(32);
		const DataDocument *r1_source_event=after_r1_stage?after_r1_stage->data.FindDocument(ws+":"+r1_stage_event_id):NULL;
		const DataDocument *r1_stage_head_after_advance=after_r1_stage?after_r1_stage->data.FindDocument(ws+":"+r1_stage_head_id):NULL;
		const DataDocument *r1_draft_event_after_advance=r1_draft_event&&after_r1_stage?after_r1_stage->data.FindDocument(ws+":"+r1_draft_event->id):NULL;
		String r1_source_event_bytes=r1_source_event?LoadFile(r1_source_event->path):String();
		String r1_stage_head_bytes=r1_stage_head_after_advance?LoadFile(r1_stage_head_after_advance->path):String();
		ValueMap revised; revised.Add("status","invalid");
		if(proposal_head&&after_r1_stage) revised=CandidateWorkflow::ReviseProposal(after_r1_stage->data,app,ws,cid,pid,proposal_head->revision,r1,changed_form,write_source,"t66-revision-two",live);
		CatalogReadHandle after_revision=app.PinLatestCatalog();
		ValueMap revision_retry=CandidateWorkflow::ReviseProposal(after_revision->data,app,ws,cid,pid,proposal_head?proposal_head->revision:String(),r1,changed_form,write_source,"t66-revision-two",live);
		ValueMap revision_payload_conflict=CandidateWorkflow::ReviseProposal(after_revision->data,app,ws,cid,pid,proposal_head?proposal_head->revision:String(),r1,form,write_source,"t66-revision-two",live);
		ValueMap normal_to_hypothesis=changed_form; normal_to_hypothesis.Set("hypothetical",true); ValueArray conversion_counterevidence; conversion_counterevidence.Add((String)ValueArray(MapGet(form,"evidence_links"))[0]); normal_to_hypothesis.Add("counterevidence",conversion_counterevidence); normal_to_hypothesis.Add("uncertainty","Synthetic conversion attempt");
		const DataDocument *normal_head_after_revision=after_revision->data.FindDocument(ws+":"+head_id);
		ValueMap rejected_normal_conversion=normal_head_after_revision?CandidateWorkflow::ReviseProposal(after_revision->data,app,ws,cid,pid,normal_head_after_revision->revision,MapGetString(revised,"revision_id"),normal_to_hypothesis,write_source,"t68-normal-to-hypothesis",live):ValueMap();
		ValueMap revise_body; revise_body.Add("workspace_id",ws); revise_body.Add("candidate_id",cid); revise_body.Add("proposal_id",pid); revise_body.Add("expected_head_revision",proposal_head?proposal_head->revision:String()); revise_body.Add("expected_revision_id",r1); revise_body.Add("operation_id","t66-revision-two"); revise_body.Add("form",changed_form);
		HttpRequest revision_repeat_request(base_url+"/api/candidates/proposal/revise"); revision_repeat_request.POST(); revision_repeat_request.Header("Origin",base_url);
		revision_repeat_request.Header("Cookie","wf_auth_session="+UrlEncode(live.token)); revision_repeat_request.ContentType("application/json");
		revision_repeat_request.PostData(AsJSON(revise_body,true)); revision_repeat_request.Timeout(3000);
		ValueMap revision_repeat_response(ParseJSON(revision_repeat_request.Execute()));
		ValueMap stale=CandidateWorkflow::ReviseProposal(after_revision->data,app,ws,cid,pid,proposal_head?proposal_head->revision:String(),r1,changed_form,write_source,"t66-stale-revision",live);
		ValueMap fork=CandidateWorkflow::ReviseProposal(after_create->data,app,ws,cid,pid,proposal_head?proposal_head->revision:String(),r1,form,write_source,"t66-competing-revision",live);
		bool immutable=old_candidate && LoadFile(old_candidate->path)==old_bytes;
		ValueMap projected=CandidateWorkflow::Project(after_revision->data,app,ws,cid);
		String r2=MapGetString(revised,"revision_id");
		const DataDocument *r1_stage_head_after=after_revision?after_revision->data.FindDocument(ws+":"+r1_stage_head_id):NULL;
		const DataDocument *r1_source_event_after=after_revision?after_revision->data.FindDocument(ws+":"+r1_stage_event_id):NULL;
		const DataDocument *r1_draft_event_after= r1_draft_event&&after_revision?after_revision->data.FindDocument(ws+":"+r1_draft_event->id):NULL;
		ValueMap old_revision_stage_attempt=r1_stage_head_after_advance?CandidateWorkflow::AdvanceStage(after_revision->data,app,ws,cid,r1,old_candidate?old_candidate->revision:String(),r1_stage_head_after_advance->revision,MapGetString(r1_stage_head_after_advance->data,"event_id"),"duplicate-reviewed","old revision after proposal update",r1_stage_evidence,write_source,"t67-old-r1-attempt",live,"no_duplicate"):ValueMap();
		const String stage_head_id="candidate-stage-head-"+DataStore::ContentDigest(ws+"\n"+cid+"\n"+r2).Left(32);
		const DataDocument *seed_item=after_create->data.FindDocument(ws+":t17-evidence-item"), *seed_snapshot=after_create->data.FindDocument(ws+":t17-evidence-snapshot");
		String unrelated_item_id="t67-unrelated-item", unrelated_snapshot_id="t67-unrelated-snapshot";
		ValueMap unrelated_snapshot=seed_snapshot?seed_snapshot->data:ValueMap(); unrelated_snapshot.Set("id",unrelated_snapshot_id); ValueMap unrelated_snapshot_env=MapGet(unrelated_snapshot,"envelope"); unrelated_snapshot_env.Set("stable_id",unrelated_snapshot_id); unrelated_snapshot.Set("envelope",unrelated_snapshot_env);
		ValueMap unrelated_item=seed_item?seed_item->data:ValueMap(); unrelated_item.Set("id",unrelated_item_id); unrelated_item.Set("snapshot",unrelated_snapshot_id); ValueMap unrelated_item_env=MapGet(unrelated_item,"envelope"); unrelated_item_env.Set("stable_id",unrelated_item_id); unrelated_item.Set("envelope",unrelated_item_env);
		String unrelated_diff_item_id="t67-other-candidate-diff-item", unrelated_diff_snapshot_id="t67-other-candidate-diff-snapshot";
		ValueMap unrelated_diff_snapshot=seed_snapshot?seed_snapshot->data:ValueMap(); unrelated_diff_snapshot.Set("id",unrelated_diff_snapshot_id); unrelated_diff_snapshot.Set("source_kind","commit-diff"); ValueMap diff_locator;diff_locator.Add("type","commit-line");diff_locator.Add("id","commit:t67-unrelated");diff_locator.Add("path","src/other-candidate.cpp");diff_locator.Add("first",3);diff_locator.Add("last",4);unrelated_diff_snapshot.Set("locator",diff_locator);ValueMap diff_snapshot_env=MapGet(unrelated_diff_snapshot,"envelope");diff_snapshot_env.Set("stable_id",unrelated_diff_snapshot_id);unrelated_diff_snapshot.Set("envelope",diff_snapshot_env);
		ValueMap unrelated_diff_item=seed_item?seed_item->data:ValueMap();unrelated_diff_item.Set("id",unrelated_diff_item_id);unrelated_diff_item.Set("snapshot",unrelated_diff_snapshot_id);unrelated_diff_item.Set("locator",diff_locator);ValueMap diff_item_env=MapGet(unrelated_diff_item,"envelope");diff_item_env.Set("stable_id",unrelated_diff_item_id);unrelated_diff_item.Set("envelope",diff_item_env);
		String counter_item_id="t67-hidden-counterevidence-item",counter_snapshot_id="t67-hidden-counterevidence-snapshot";
		ValueMap counter_snapshot=seed_snapshot?seed_snapshot->data:ValueMap();counter_snapshot.Set("id",counter_snapshot_id);counter_snapshot.Set("source_identity",counter_source);ValueMap counter_snapshot_env=MapGet(counter_snapshot,"envelope");counter_snapshot_env.Set("stable_id",counter_snapshot_id);counter_snapshot.Set("envelope",counter_snapshot_env);
		ValueMap counter_item=seed_item?seed_item->data:ValueMap();counter_item.Set("id",counter_item_id);counter_item.Set("snapshot",counter_snapshot_id);counter_item.Set("source_identity",counter_source);ValueMap counter_item_env=MapGet(counter_item,"envelope");counter_item_env.Set("stable_id",counter_item_id);counter_item.Set("envelope",counter_item_env);
		DataBatchChange us; us.action=DATA_BATCH_CREATE; us.qualified_id=ws+":"+unrelated_snapshot_id; us.proposed=unrelated_snapshot;
		DataBatchChange ui; ui.action=DATA_BATCH_CREATE; ui.qualified_id=ws+":"+unrelated_item_id; ui.proposed=unrelated_item;
		DataBatchChange uds; uds.action=DATA_BATCH_CREATE; uds.qualified_id=ws+":"+unrelated_diff_snapshot_id; uds.proposed=unrelated_diff_snapshot;
		DataBatchChange udi; udi.action=DATA_BATCH_CREATE; udi.qualified_id=ws+":"+unrelated_diff_item_id; udi.proposed=unrelated_diff_item;
		DataBatchChange cs; cs.action=DATA_BATCH_CREATE; cs.qualified_id=ws+":"+counter_snapshot_id; cs.proposed=counter_snapshot;
		DataBatchChange ci; ci.action=DATA_BATCH_CREATE; ci.qualified_id=ws+":"+counter_item_id; ci.proposed=counter_item;
		Vector<DataBatchChange> unrelated_changes; unrelated_changes.Add(us); unrelated_changes.Add(ui); unrelated_changes.Add(uds); unrelated_changes.Add(udi); unrelated_changes.Add(cs); unrelated_changes.Add(ci);
		ManagerSkylarkApp::BatchFinalizationResult unrelated_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-unrelated-evidence-seed",unrelated_changes);
		CatalogReadHandle after_stage_seed=app.PinLatestCatalog();
		const DataDocument *linked_item=after_stage_seed?after_stage_seed->data.FindDocument(ws+":t17-evidence-item"):NULL;
		const DataDocument *linked_snapshot=linked_item?after_stage_seed->data.FindDocument(ws+":"+MapGetString(linked_item->data,"snapshot")):NULL;
		auto cw_test_record=[&](const String& kind,const String& id,const String& provenance="computed"){ValueMap rec,env; rec.Add("id",id);rec.Add("kind",kind);rec.Add("workspace_id",ws);rec.Add("revision","1");rec.Add("provenance",provenance);env.Add("schema_id","skylark/"+kind);env.Add("schema_version","1");env.Add("workspace_id",ws);env.Add("stable_id",id);env.Add("record_kind",kind);env.Add("revision","1");env.Add("visibility","private");env.Add("provenance_ref",provenance);rec.Add("envelope",env);return rec;};
		ValueMap test_rubric=cw_test_record("assessment-rubric","t67-current-rubric","authored");test_rubric.Set("revision","r1");ValueArray rubric_dims,rubric_coverage,rubric_tiers;
		for(int i=0;i<6;i++){ValueMap dim;dim.Add("id",Format("t67-dim-%d",i));dim.Add("weight",1);dim.Add("minimum_score",0);dim.Add("maximum_score",10);dim.Add("minimum_evidence_count",1);rubric_dims.Add(dim);rubric_coverage.Add(Format("t67-dim-%d",i));}
		ValueMap high_tier;high_tier.Add("id","tier-high");high_tier.Add("minimum_numerator",0);high_tier.Add("minimum_denominator",1);high_tier.Add("maximum_numerator",10);high_tier.Add("maximum_denominator",1);rubric_tiers.Add(high_tier);
		test_rubric.Add("dimensions",rubric_dims);test_rubric.Add("score_scale","integer-0-10");test_rubric.Add("total_rule","weighted-mean");test_rubric.Add("coverage_required",rubric_coverage);test_rubric.Add("tiers",rubric_tiers);
		auto assessment_test_records=[&](const String& suffix,const String& evidence_item,const String& evidence_snapshot){
			String run_id="t67-assessment-run-"+suffix,assessment_id="t67-candidate-assessment-"+suffix;
			ValueMap item_pin,snapshot_pin;item_pin.Add("workspace_id",ws);item_pin.Add("id",evidence_item);item_pin.Add("revision",after_stage_seed->data.FindDocument(ws+":"+evidence_item)->revision);snapshot_pin.Add("workspace_id",ws);snapshot_pin.Add("id",evidence_snapshot);snapshot_pin.Add("revision",after_stage_seed->data.FindDocument(ws+":"+evidence_snapshot)->revision);ValueArray pins;pins.Add(item_pin);pins.Add(snapshot_pin);
			ValueMap run=cw_test_record("analysis-run",run_id);run.Add("status","configured");run.Add("rubric_id","t67-current-rubric");run.Add("rubric_revision","r1");run.Add("tool_config","synthetic");run.Add("model_config","none");run.Add("input_revisions",pins);run.Add("unique_source_count",1);run.Add("unique_source_revision_count",1);run.Add("unique_evidence_count",1);ValueArray evidence_keys;evidence_keys.Add(evidence_item);run.Add("evidence_keys",evidence_keys);run.Add("relationships",ValueArray());run.Add("overlap_groups",ValueArray());run.Add("uncertainty","complete");ValueArray source_provenance;ValueMap sp;sp.Add("workspace_id",ws);sp.Add("identity",MapGetString(linked_item->data,"source_identity"));sp.Add("revision",MapGetString(linked_item->data,"upstream_revision"));source_provenance.Add(sp);run.Add("source_provenance",source_provenance);
			ValueMap assessment_value;assessment_value.Add("candidate_id",cid);assessment_value.Add("provenance","computed");assessment_value.Add("uncertainty","complete");assessment_value.Add("total_numerator","48");assessment_value.Add("total_denominator","6");assessment_value.Add("tier","tier-high");ValueArray assessed_dims;for(int i=0;i<6;i++){ValueMap dim;dim.Add("dimension_id",Format("t67-dim-%d",i));dim.Add("score","8");dim.Add("score_state","known");dim.Add("coverage_state","covered");dim.Add("evidence_count",1);assessed_dims.Add(dim);}assessment_value.Add("dimensions",assessed_dims);
			ValueMap assessment=cw_test_record("candidate-assessment",assessment_id);assessment.Add("candidate_id",cid);assessment.Add("analysis_run",run_id);assessment.Add("assessment",assessment_value);Vector<ValueMap> records;records.Add(run);records.Add(assessment);return records;
		};
		Vector<ValueMap> unrelated_assessment_records=assessment_test_records("unrelated",unrelated_item_id,unrelated_snapshot_id);Vector<ValueMap> mismatched_assessment_records=assessment_test_records("revision-mismatch","t17-evidence-item",MapGetString(linked_item->data,"snapshot"));ValueArray mismatch_pins=MapGet(mismatched_assessment_records[0],"input_revisions");ValueMap mismatched_item_pin=mismatch_pins[0];mismatched_item_pin.Set("revision",linked_item->revision+"-old");mismatch_pins.Set(0,mismatched_item_pin);ValueMap mismatched_run=mismatched_assessment_records[0];mismatched_run.Set("input_revisions",mismatch_pins);mismatched_assessment_records.Set(0,mismatched_run);Vector<ValueMap> mismatched_snapshot_records=assessment_test_records("snapshot-revision-mismatch","t17-evidence-item",MapGetString(linked_item->data,"snapshot"));ValueArray mismatch_snapshot_pins=MapGet(mismatched_snapshot_records[0],"input_revisions");ValueMap mismatched_snapshot_pin=mismatch_snapshot_pins[1];mismatched_snapshot_pin.Set("revision",linked_snapshot->revision+"-old");mismatch_snapshot_pins.Set(1,mismatched_snapshot_pin);ValueMap mismatched_snapshot_run=mismatched_snapshot_records[0];mismatched_snapshot_run.Set("input_revisions",mismatch_snapshot_pins);mismatched_snapshot_records.Set(0,mismatched_snapshot_run);Vector<DataBatchChange> assessment_seed_changes;DataBatchChange rubric_change;rubric_change.action=DATA_BATCH_CREATE;rubric_change.qualified_id=ws+":t67-current-rubric";rubric_change.proposed=test_rubric;assessment_seed_changes.Add(rubric_change);for(const ValueMap& record:unrelated_assessment_records){DataBatchChange change;change.action=DATA_BATCH_CREATE;change.qualified_id=ws+":"+MapGetString(record,"id");change.proposed=record;assessment_seed_changes.Add(change);}ManagerSkylarkApp::BatchFinalizationResult unrelated_assessment_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-unrelated-assessment-seed",assessment_seed_changes);
		CatalogReadHandle after_unrelated_assessment=app.PinLatestCatalog();ValueMap current_proposal_snapshot; if(after_stage_seed){ValueMap proposal_view=CandidateWorkflow::Project(after_stage_seed->data,app,ws,cid);ValueArray proposal_heads=MapGet(proposal_view,"current_proposals");if(!proposal_heads.IsEmpty())current_proposal_snapshot=MapGet((ValueMap)proposal_heads[0],"snapshot");}
		String assessment_blocker="candidate_assessment_missing_stale_or_unpinned_to_current_proposal_evidence";
		bool unrelated_assessment_denied=unrelated_assessment_write.committed&&after_unrelated_assessment&&!CandidateWorkflow::HasCurrentProposalAssessment(after_unrelated_assessment->data,app,ws,cid,current_proposal_snapshot)&&CandidateWorkflow::ComparisonAssessmentBlocker(after_unrelated_assessment->data,app,ws,cid,current_proposal_snapshot)==assessment_blocker;
		Vector<DataBatchChange> mismatched_assessment_changes;for(const ValueMap& record:mismatched_assessment_records){DataBatchChange change;change.action=DATA_BATCH_CREATE;change.qualified_id=ws+":"+MapGetString(record,"id");change.proposed=record;mismatched_assessment_changes.Add(change);}for(const ValueMap& record:mismatched_snapshot_records){DataBatchChange change;change.action=DATA_BATCH_CREATE;change.qualified_id=ws+":"+MapGetString(record,"id");change.proposed=record;mismatched_assessment_changes.Add(change);}ManagerSkylarkApp::BatchFinalizationResult mismatched_assessment_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-mismatched-assessment-seed",mismatched_assessment_changes);CatalogReadHandle after_mismatched_assessment=app.PinLatestCatalog();bool mismatched_assessment_denied=mismatched_assessment_write.committed&&after_mismatched_assessment&&!CandidateWorkflow::HasCurrentProposalAssessment(after_mismatched_assessment->data,app,ws,cid,current_proposal_snapshot)&&CandidateWorkflow::ComparisonAssessmentBlocker(after_mismatched_assessment->data,app,ws,cid,current_proposal_snapshot)==assessment_blocker;
		Vector<ValueMap> rubric_mismatch_records=assessment_test_records("rubric-revision-mismatch","t17-evidence-item",MapGetString(linked_item->data,"snapshot"));ValueMap bad_rubric_run=rubric_mismatch_records[0];bad_rubric_run.Set("rubric_revision","r0");rubric_mismatch_records.Set(0,bad_rubric_run);
		Vector<ValueMap> total_mismatch_records=assessment_test_records("computed-total-mismatch","t17-evidence-item",MapGetString(linked_item->data,"snapshot"));ValueMap bad_total_record=total_mismatch_records[1];ValueMap bad_total=MapGet(bad_total_record,"assessment");bad_total.Set("total_numerator","47");bad_total_record.Set("assessment",bad_total);total_mismatch_records.Set(1,bad_total_record);
		Vector<DataBatchChange> semantic_mismatch_changes;for(const Vector<ValueMap>* records:{&rubric_mismatch_records,&total_mismatch_records})for(const ValueMap& record:*records){DataBatchChange change;change.action=DATA_BATCH_CREATE;change.qualified_id=ws+":"+MapGetString(record,"id");change.proposed=record;semantic_mismatch_changes.Add(change);}ManagerSkylarkApp::BatchFinalizationResult semantic_mismatch_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-semantic-assessment-mismatch-seed",semantic_mismatch_changes);CatalogReadHandle after_semantic_mismatch=app.PinLatestCatalog();bool semantic_assessment_denied=semantic_mismatch_write.committed&&after_semantic_mismatch&&!CandidateWorkflow::HasCurrentProposalAssessment(after_semantic_mismatch->data,app,ws,cid,current_proposal_snapshot)&&CandidateWorkflow::ComparisonAssessmentBlocker(after_semantic_mismatch->data,app,ws,cid,current_proposal_snapshot)==assessment_blocker;
		Vector<ValueMap> linked_assessment_records=assessment_test_records("linked","t17-evidence-item",MapGetString(linked_item->data,"snapshot"));Vector<DataBatchChange> linked_assessment_changes;for(const ValueMap& record:linked_assessment_records){DataBatchChange change;change.action=DATA_BATCH_CREATE;change.qualified_id=ws+":"+MapGetString(record,"id");change.proposed=record;linked_assessment_changes.Add(change);}ManagerSkylarkApp::BatchFinalizationResult linked_assessment_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-linked-assessment-seed",linked_assessment_changes);
		CatalogReadHandle after_linked_assessment=app.PinLatestCatalog();bool linked_assessment_ready=linked_assessment_write.committed&&after_linked_assessment&&CandidateWorkflow::HasCurrentProposalAssessment(after_linked_assessment->data,app,ws,cid,current_proposal_snapshot)&&CandidateWorkflow::ComparisonAssessmentBlocker(after_linked_assessment->data,app,ws,cid,current_proposal_snapshot).IsEmpty();
		String linked_run_id="t67-assessment-run-linked";ValueMap stale_marker=cw_test_record("assessment-staleness","t67-assessment-stale","computed");stale_marker.Add("prior_run_id",linked_run_id);stale_marker.Add("replacement_run_id","analysis-run-24337ffc571325c38f2f17d9471aad08");ValueMap stale_flags;for(const char *flag:{"source_dependency_set_changed","source_revision_changed","input_revision_changed","rubric_revision_changed","tool_config_changed","model_config_changed"})stale_flags.Add(flag,false);stale_flags.Set("input_revision_changed",true);stale_marker.Add("reason_flags",stale_flags);DataBatchChange stale_marker_change;stale_marker_change.action=DATA_BATCH_CREATE;stale_marker_change.qualified_id=ws+":t67-assessment-stale";stale_marker_change.proposed=stale_marker;Vector<DataBatchChange> stale_marker_changes;stale_marker_changes.Add(stale_marker_change);ManagerSkylarkApp::BatchFinalizationResult stale_marker_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-assessment-stale-seed",stale_marker_changes);CatalogReadHandle after_stale_assessment=app.PinLatestCatalog();bool stale_assessment_denied=stale_marker_write.committed&&after_stale_assessment&&!CandidateWorkflow::HasCurrentProposalAssessment(after_stale_assessment->data,app,ws,cid,current_proposal_snapshot)&&CandidateWorkflow::ComparisonAssessmentBlocker(after_stale_assessment->data,app,ws,cid,current_proposal_snapshot)==assessment_blocker;
		const DataDocument *draft_stage_head=after_stage_seed?after_stage_seed->data.FindDocument(ws+":"+stage_head_id):NULL;
		const DataDocument *draft_stage_event=draft_stage_head?after_stage_seed->data.FindDocument(ws+":"+MapGetString(draft_stage_head->data,"event_id")):NULL;
		ValueMap draft_projection=after_stage_seed?CandidateWorkflow::Project(after_stage_seed->data,app,ws,cid):ValueMap();
		ValueArray stage_refs; stage_refs.Add("t17-evidence-item");
		ValueArray unrelated_refs; unrelated_refs.Add(unrelated_item_id);
		ValueMap unrelated_source_stage=after_stage_seed?CandidateWorkflow::AdvanceStage(after_stage_seed->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),draft_stage_head?draft_stage_head->revision:String(),draft_stage_head?MapGetString(draft_stage_head->data,"event_id"):String(),"source-inspected","unrelated visible evidence",unrelated_refs,write_source,"t67-unrelated-source",live):ValueMap();
		ValueMap no_auth_stage=CandidateWorkflow::AdvanceStage(after_stage_seed->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),draft_stage_head?draft_stage_head->revision:String(),draft_stage_head?MapGetString(draft_stage_head->data,"event_id"):String(),"source-inspected","synthetic inspection",stage_refs,write_source,"t67-no-auth-stage",UserSession());
		ValueMap source_stage=after_stage_seed?CandidateWorkflow::AdvanceStage(after_stage_seed->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),draft_stage_head?draft_stage_head->revision:String(),draft_stage_head?MapGetString(draft_stage_head->data,"event_id"):String(),"source-inspected","synthetic source inspection",stage_refs,write_source,"t67-source-inspected",live):ValueMap();
		CatalogReadHandle after_source_stage=app.PinLatestCatalog();
		CatalogReadHandle before_stage_http_retry=app.PinCatalog();
		int stage_http_docs_before=before_stage_http_retry?before_stage_http_retry->data.GetDocuments().GetCount():-1;
		ValueMap stage_http_command;stage_http_command.Add("workspace_id",ws);stage_http_command.Add("candidate_id",cid);stage_http_command.Add("proposal_revision_id",r2);stage_http_command.Add("expected_candidate_revision",old_candidate?old_candidate->revision:String());stage_http_command.Add("expected_head_revision",draft_stage_head?draft_stage_head->revision:String());stage_http_command.Add("expected_head_event_id",draft_stage_head?MapGetString(draft_stage_head->data,"event_id"):String());stage_http_command.Add("next_stage","source-inspected");stage_http_command.Add("reason","synthetic source inspection");stage_http_command.Add("evidence_ids",stage_refs);stage_http_command.Add("operation_id","t67-source-inspected");stage_http_command.Add("review_outcome","");
		HttpRequest stage_http_retry_request(base_url+"/api/candidates/stage");stage_http_retry_request.POST();stage_http_retry_request.Header("Cookie","wf_auth_session="+UrlEncode(live.token));stage_http_retry_request.Header("Origin",base_url);stage_http_retry_request.ContentType("application/json");stage_http_retry_request.PostData(AsJSON(stage_http_command,true));stage_http_retry_request.Timeout(3000);String stage_http_retry_body=stage_http_retry_request.Execute();int stage_http_retry_status=stage_http_retry_request.GetStatusCode();Value stage_http_retry_json=ParseJSON(stage_http_retry_body);ValueMap stage_http_retry_result;if(stage_http_retry_json.Is<ValueMap>())stage_http_retry_result=stage_http_retry_json;
		CatalogReadHandle after_stage_http_retry=app.PinLatestCatalog();
		bool stage_http_retry_exact=stage_http_retry_status==200&&MapGetString(stage_http_retry_result,"status")=="already_committed"&&before_stage_http_retry&&after_stage_http_retry&&before_stage_http_retry->generation==after_stage_http_retry->generation&&stage_http_docs_before==after_stage_http_retry->data.GetDocuments().GetCount();
		ValueMap source_stage_retry=after_source_stage?CandidateWorkflow::AdvanceStage(after_source_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),draft_stage_head?draft_stage_head->revision:String(),draft_stage_head?MapGetString(draft_stage_head->data,"event_id"):String(),"source-inspected","synthetic source inspection",stage_refs,write_source,"t67-source-inspected",live):ValueMap();
		ValueMap source_stage_changed=after_source_stage?CandidateWorkflow::AdvanceStage(after_source_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),draft_stage_head?draft_stage_head->revision:String(),draft_stage_head?MapGetString(draft_stage_head->data,"event_id"):String(),"source-inspected","changed reason",stage_refs,write_source,"t67-source-inspected",live):ValueMap();
		ValueMap stale_stage=CandidateWorkflow::AdvanceStage(after_create->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),draft_stage_head?draft_stage_head->revision:String(),draft_stage_head?MapGetString(draft_stage_head->data,"event_id"):String(),"source-inspected","another operation from stale head",stage_refs,write_source,"t67-stale-stage-op",live);
		ValueArray review_refs; review_refs.Add("t17-evidence-item");
		const DataDocument *source_event=after_source_stage?after_source_stage->data.FindDocument(ws+":"+"candidate-stage-event-"+DataStore::ContentDigest("t67-source-inspected\n"+cid+"\n"+r2+"\nsource-inspected").Left(32)):NULL;
		String source_event_bytes=source_event?LoadFile(source_event->path):String();
		const DataDocument *source_head=after_source_stage?after_source_stage->data.FindDocument(ws+":"+stage_head_id):NULL;
		ValueMap unrelated_review=after_source_stage?CandidateWorkflow::AdvanceStage(after_source_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),source_head?source_head->revision:String(),source_head?MapGetString(source_head->data,"event_id"):String(),"duplicate-reviewed","unrelated visible citation",unrelated_refs,write_source,"t67-unrelated-review",live,"no_duplicate"):ValueMap();
		ValueMap review_stage=after_source_stage?CandidateWorkflow::AdvanceStage(after_source_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),source_head?source_head->revision:String(),source_head?MapGetString(source_head->data,"event_id"):String(),"duplicate-reviewed","synthetic duplicate review",review_refs,write_source,"t67-duplicate-review",live,"no_duplicate"):ValueMap();
		CatalogReadHandle after_review_stage=app.PinLatestCatalog();
		ValueMap competing_review=after_source_stage?CandidateWorkflow::AdvanceStage(after_source_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),source_head?source_head->revision:String(),source_head?MapGetString(source_head->data,"event_id"):String(),"duplicate-reviewed","competing transition",review_refs,write_source,"t67-competing-review",live,"no_duplicate"):ValueMap();
		ValueMap review_missing_outcome=after_source_stage?CandidateWorkflow::AdvanceStage(after_source_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),source_head?source_head->revision:String(),source_head?MapGetString(source_head->data,"event_id"):String(),"duplicate-reviewed","missing outcome",review_refs,write_source,"t67-review-missing-outcome",live):ValueMap();
		ValueMap review_missing_citation=after_source_stage?CandidateWorkflow::AdvanceStage(after_source_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),source_head?source_head->revision:String(),source_head?MapGetString(source_head->data,"event_id"):String(),"duplicate-reviewed","missing citation",ValueArray(),write_source,"t67-review-no-citation",live,"no_duplicate"):ValueMap();
		const DataDocument *review_event=after_review_stage?after_review_stage->data.FindDocument(ws+":"+"candidate-stage-event-"+DataStore::ContentDigest("t67-duplicate-review\n"+cid+"\n"+r2+"\nduplicate-reviewed").Left(32)):NULL;
		const DataDocument *review_head=after_review_stage?after_review_stage->data.FindDocument(ws+":"+stage_head_id):NULL;
		ValueMap blocked_spike=after_review_stage?CandidateWorkflow::AdvanceStage(after_review_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),review_head?review_head->revision:String(),review_head?MapGetString(review_head->data,"event_id"):String(),"spike-verified","no measured job exists",ValueArray(),write_source,"t67-spike-unavailable",live):ValueMap();
		ValueMap blocked_deliverable=after_review_stage?CandidateWorkflow::AdvanceStage(after_review_stage->data,app,ws,cid,r2,old_candidate?old_candidate->revision:String(),review_head?review_head->revision:String(),review_head?MapGetString(review_head->data,"event_id"):String(),"deliverables-ready","cannot skip unavailable spike",review_refs,write_source,"t67-deliverable-sequence-block",live):ValueMap();
		ValueMap review_projection=after_review_stage?CandidateWorkflow::Project(after_review_stage->data,app,ws,cid):ValueMap();
		String rendered_stage=CandidateWorkflow::Render(review_projection);
		CatalogReadHandle workflow_route_pin=app.PinCatalog();
		String workflow_query="?workspace="+UrlEncode(ws)+"&candidate="+UrlEncode(cid);
		HttpRequest workflow_api_request(base_url+"/api/candidates/workflow"+workflow_query);workflow_api_request.Header("Cookie","wf_auth_session="+UrlEncode(live.token));workflow_api_request.Timeout(3000);String workflow_api_body=workflow_api_request.Execute();int workflow_api_status=workflow_api_request.GetStatusCode();Value workflow_api_value=ParseJSON(workflow_api_body);ValueMap workflow_api_projection;if(workflow_api_value.Is<ValueMap>())workflow_api_projection=workflow_api_value;
		HttpRequest workflow_html_request(base_url+"/candidate-workflow"+workflow_query);workflow_html_request.Header("Cookie","wf_auth_session="+UrlEncode(live.token));workflow_html_request.Timeout(3000);String workflow_html_body=workflow_html_request.Execute();int workflow_html_status=workflow_html_request.GetStatusCode();
		CatalogReadHandle workflow_route_pin_after=app.PinCatalog();ValueArray expected_gates=MapGet(review_projection,"prerequisites"),api_gates=MapGet(workflow_api_projection,"prerequisites");
		auto workflow_stage_status=[](const ValueArray& rows,const String& stage){for(const Value& row:rows)if(IsValueMap(row)&&MapGetString((ValueMap)row,"stage")==stage)return MapGetString((ValueMap)row,"status");return String();};
		bool workflow_route_parity=workflow_route_pin&&after_review_stage&&workflow_route_pin->generation==after_review_stage->generation&&workflow_api_status==200&&workflow_html_status==200&&workflow_route_pin_after&&workflow_route_pin_after->generation==workflow_route_pin->generation&&
			MapGetString(workflow_api_projection,"status")=="visible"&&MapGetString(workflow_api_projection,"candidate_id")==cid&&MapGetString(workflow_api_projection,"proposal_revision_id")==r2&&MapGetString(workflow_api_projection,"stage")=="duplicate-reviewed"&&
			MapGetString(workflow_api_projection,"head_revision")==MapGetString(review_projection,"head_revision")&&AsJSON(MapGet(workflow_api_projection,"history"),true)==AsJSON(MapGet(review_projection,"history"),true)&&
			AsJSON(api_gates,true)==AsJSON(expected_gates,true)&&workflow_stage_status(api_gates,"source-inspected")=="complete"&&workflow_stage_status(api_gates,"duplicate-reviewed")=="complete"&&
			workflow_html_body.Find("Proposal revision: "+r2)>=0&&workflow_html_body.Find("current stage: duplicate-reviewed")>=0&&workflow_html_body.Find("source-inspected: complete")>=0&&workflow_html_body.Find("duplicate-reviewed: complete")>=0;
		const DataDocument *revision_two=after_revision->data.FindDocument(ws+":"+MapGetString(revised,"revision_id"));
		const DataDocument *old_revision_after=after_revision->data.FindDocument(ws+":"+r1), *old_form_after=after_revision->data.FindDocument(ws+":"+old_form_id);
		bool prior_records_unchanged=old_candidate&&LoadFile(old_candidate->path)==old_bytes&&!old_proposal_bytes.IsEmpty()&&!old_form_bytes.IsEmpty()&&old_revision_after&&old_form_after&&LoadFile(old_revision_after->path)==old_proposal_bytes&&LoadFile(old_form_after->path)==old_form_bytes;
		ValueMap hypothetical_form=form; hypothetical_form.Set("hypothetical",true); ValueArray hypothetical_links; hypothetical_links.Add(unrelated_diff_item_id); hypothetical_form.Set("evidence_links",hypothetical_links); ValueArray counterevidence; counterevidence.Add(counter_item_id); hypothetical_form.Add("counterevidence",counterevidence); hypothetical_form.Add("uncertainty","Synthetic uncertainty remains unresolved");
		CatalogReadHandle before_hypothesis=app.PinLatestCatalog();
		ValueMap hypothetical_create=CandidateWorkflow::CreateProposal(before_hypothesis->data,app,hypothetical_form,ws,write_source,"t66-hypothesis-create",live);
		String hcid=MapGetString(hypothetical_create,"candidate_id"), hpid=MapGetString(hypothetical_create,"proposal_id"), hr1=MapGetString(hypothetical_create,"revision_id");
		bool hypothesis_create_committed=(MapGetString(hypothetical_create,"status")=="committed" || MapGetString(hypothetical_create,"status")=="committed_notification_pending") && (int)MapGet(hypothetical_create,"changed_ids")>0;
		bool hypothesis_create_reloaded=hypothesis_create_committed && app.RefreshCatalog();
		CatalogReadHandle after_hypothesis_create=hypothesis_create_reloaded?app.PinLatestCatalog():CatalogReadHandle();
		bool hypothesis_create_visible=after_hypothesis_create && after_hypothesis_create->generation>before_hypothesis->generation &&
			after_hypothesis_create->data.FindDocument(ws+":"+hcid) && after_hypothesis_create->data.FindDocument(ws+":"+hr1);
		if(!hypothesis_create_reloaded || !hypothesis_create_visible) {
			CatalogReadHandle diagnostic_pin=app.PinLatestCatalog();
			Cerr()<<"T68 create reload diagnostic committed="<<hypothesis_create_committed<<" refreshed="<<hypothesis_create_reloaded<<" visible="<<hypothesis_create_visible
			     <<" registry="<<app.test_registration.id<<" root_configured="<<!app.test_registration.root.IsEmpty()<<" root_matches_fixture="<<(NormalizePath(app.test_registration.root)==NormalizePath(root))
			     <<" readonly="<<app.test_registration.read_only<<" policy="<<app.IsAssessmentSourceVisible(ws,source_identity.object_id)<<"/"<<app.IsAssessmentSourceVisible(ws,counter_source)
			     <<" failpoints="<<(int)app.test_batch_recovery_failpoint<<"/"<<(int)app.test_batch_notification_failpoint
			     <<" catalog="<<(diagnostic_pin?diagnostic_pin->status:String("missing"))<<"/"<<(diagnostic_pin?diagnostic_pin->data_status:String("missing"))<<" generation="<<(diagnostic_pin?AsString(diagnostic_pin->generation):String("none"))
			     <<" source="<<(diagnostic_pin?diagnostic_pin->data.GetSourceId():String("missing"))<<" diagnostic_count="<<(diagnostic_pin?diagnostic_pin->diagnostics.GetCount():-1)<<"\n";
			if(diagnostic_pin) for(const String& diagnostic:diagnostic_pin->diagnostics) Cerr()<<"T68 catalog diagnostic="<<diagnostic<<"\n";
		}
		ValueMap h_project;if(after_hypothesis_create)h_project=CandidateWorkflow::Project(after_hypothesis_create->data,app,ws,hcid);ValueArray h_current_proposals=MapGet(h_project,"current_proposals");ValueMap h_current_form;if(!h_current_proposals.IsEmpty())h_current_form=MapGet((ValueMap)h_current_proposals[0],"snapshot");
		bool current_proposal_has_diff=after_stage_seed&&CandidateWorkflow::HasLinkedCommitDiff(after_stage_seed->data,app,ws,current_proposal_snapshot);
		bool other_proposal_has_diff=after_hypothesis_create&&CandidateWorkflow::HasLinkedCommitDiff(after_hypothesis_create->data,app,ws,h_current_form);
		bool unrelated_candidate_diff_not_current=!current_proposal_has_diff&&other_proposal_has_diff;
		ValueMap linked_diff_gate=after_hypothesis_create?CandidateWorkflow::CheckStagePrerequisite(after_hypothesis_create->data,app,ws,"deliverables-ready",h_current_form,ValueArray(),String(),true):ValueMap();
		ValueMap unrelated_diff_gate=after_stage_seed?CandidateWorkflow::CheckStagePrerequisite(after_stage_seed->data,app,ws,"deliverables-ready",current_proposal_snapshot,ValueArray(),String(),true):ValueMap();
		ValueMap invalid_diff_form=current_proposal_snapshot;ValueArray invalid_diff_links;invalid_diff_links.Add("t17-evidence-item");invalid_diff_form.Set("evidence_links",invalid_diff_links);ValueMap invalid_diff_gate=after_stage_seed?CandidateWorkflow::CheckStagePrerequisite(after_stage_seed->data,app,ws,"deliverables-ready",invalid_diff_form,ValueArray(),String(),true):ValueMap();
		String hhead_id="candidate-proposal-head-"+DataStore::ContentDigest(ws+"\n"+hpid).Left(32);
		const DataDocument *hyp_head=after_hypothesis_create?after_hypothesis_create->data.FindDocument(ws+":"+hhead_id):NULL;
		const DataDocument *hyp_candidate=after_hypothesis_create?after_hypothesis_create->data.FindDocument(ws+":"+hcid):NULL, *hyp_snapshot=NULL;
		String hyp_snapshot_bytes, hyp_snapshot_id;
		if(after_hypothesis_create) for(const DataDocument& d:after_hypothesis_create->data.GetDocuments()) if(d.kind=="candidate-proposal"&&MapGetString(d.data,"candidate_id")==hcid&&MapGetString(d.data,"proposal_revision_id")==hr1) { hyp_snapshot=&d; hyp_snapshot_bytes=LoadFile(d.path); hyp_snapshot_id=d.id; }
		ValueMap hform2=hypothetical_form; hform2.Set("title","Synthetic T66 hypothetical revision two");
		ValueMap hypothetical_revision; hypothetical_revision.Add("status","invalid");
		if(hyp_head) hypothetical_revision=CandidateWorkflow::ReviseProposal(after_hypothesis_create->data,app,ws,hcid,hpid,hyp_head->revision,hr1,hform2,write_source,"t66-hypothesis-revision-two",live);
		bool hypothesis_revision_committed=(MapGetString(hypothetical_revision,"status")=="committed" || MapGetString(hypothetical_revision,"status")=="committed_notification_pending") && !MapGetString(hypothetical_revision,"revision_id").IsEmpty();
		bool hypothesis_revision_reloaded=hypothesis_revision_committed && app.RefreshCatalog();
		CatalogReadHandle after_hypothesis_revision=hypothesis_revision_reloaded?app.PinLatestCatalog():CatalogReadHandle();
		bool hypothesis_revision_visible=after_hypothesis_revision && after_hypothesis_revision->data.FindDocument(ws+":"+MapGetString(hypothetical_revision,"revision_id"));
		const DataDocument *hyp_r2=after_hypothesis_revision?after_hypothesis_revision->data.FindDocument(ws+":"+MapGetString(hypothetical_revision,"revision_id")):NULL;
		const DataDocument *hyp_snapshot2=NULL;
		if(after_hypothesis_revision) for(const DataDocument& d:after_hypothesis_revision->data.GetDocuments()) if(d.kind=="candidate-proposal"&&MapGetString(d.data,"candidate_id")==hcid&&MapGetString(d.data,"proposal_revision_id")==MapGetString(hypothetical_revision,"revision_id")) hyp_snapshot2=&d;
		const DataDocument *hyp_candidate_after=after_hypothesis_revision?after_hypothesis_revision->data.FindDocument(ws+":"+hcid):NULL;
		const DataDocument *hyp_snapshot1_after=after_hypothesis_revision?after_hypothesis_revision->data.FindDocument(ws+":"+hyp_snapshot_id):NULL;
		const DataDocument *hyp_head_after=after_hypothesis_revision?after_hypothesis_revision->data.FindDocument(ws+":"+hhead_id):NULL;
		ValueMap contradictory_hypothetical=hform2; contradictory_hypothetical.Set("hypothetical",false);
		ValueMap rejected_flag_clear=after_hypothesis_revision?CandidateWorkflow::ReviseProposal(after_hypothesis_revision->data,app,ws,hcid,hpid,hyp_head_after?hyp_head_after->revision:String(),MapGetString(hypothetical_revision,"revision_id"),contradictory_hypothetical,write_source,"t68-hypothesis-clear",live):ValueMap();
		ValueMap hyp_projection=after_hypothesis_revision?CandidateWorkflow::Project(after_hypothesis_revision->data,app,ws,hcid):ValueMap();
		ValueMap lineage_hyp_projection=after_hypothesis_revision?CandidateLineage::Project(after_hypothesis_revision->data,app,ws):ValueMap();
		if(after_hypothesis_revision && (MapGetString(h_project,"status")!="visible" || MapGetString(hyp_projection,"status")!="visible" || MapGetString(lineage_hyp_projection,"status")!="visible")) {
			CatalogReadHandle diagnostic_pin=app.PinLatestCatalog();
			Cerr()<<"T68 hypothesis diagnostic workflow="<<MapGetString(h_project,"status")<<" revised="<<MapGetString(hyp_projection,"status")<<" lineage="<<MapGetString(lineage_hyp_projection,"status")
			     <<" sources_visible="<<app.IsAssessmentSourceVisible(ws,source_identity.object_id)<<"/"<<app.IsAssessmentSourceVisible(ws,counter_source)
			     <<" catalog="<<(diagnostic_pin?"pinned":"missing")<<"/"<<(diagnostic_pin?AsString(diagnostic_pin->generation):String("none"))
				     <<" registration="<<app.test_registration.id<<"/"<<(app.test_registration.root.IsEmpty()?"unconfigured":"configured")<<"/"<<(NormalizePath(app.test_registration.root)==NormalizePath(root))<<"/"<<(app.test_registration.read_only?"readonly":"writable")
				     <<" load="<<(diagnostic_pin?diagnostic_pin->data.GetLoadStatus():String("none"))<<" source="<<(diagnostic_pin?diagnostic_pin->data.GetSourceId():String("none"))<<"\n";
			for(const DataDocument& d:after_hypothesis_revision->data.GetDocuments()) {
				String did=MapGetString(d.data,"id"), candidate_ref=MapGetString(d.data,"candidate_id"), proposal_ref=MapGetString(d.data,"proposal_id"), revision_ref=MapGetString(d.data,"proposal_revision_id");
				bool related=d.id==hcid || d.id==hpid || d.id==hr1 || d.id==MapGetString(hypothetical_revision,"revision_id") || d.id==hhead_id || candidate_ref==hcid || proposal_ref==hpid || revision_ref==hr1 || revision_ref==MapGetString(hypothetical_revision,"revision_id") || d.id==unrelated_diff_item_id || d.id==unrelated_diff_snapshot_id || d.id==counter_item_id || d.id==counter_snapshot_id;
				if(!related) continue;
				Cerr()<<"T68 doc kind="<<d.kind<<" id="<<d.id<<" workspace="<<d.workspace_id<<" valid="<<d.valid<<"/"<<d.validated
				     <<" origin="<<MapGetString(d.data,"origin_kind")<<" owner="<<MapGetString(d.data,"owner_kind")<<" hypothetical="<<AsString(MapGet(d.data,"hypothetical"))
				     <<" states="<<MapGetString(d.data,"submission_state")<<"/"<<MapGetString(d.data,"approval_state")<<"/"<<MapGetString(d.data,"author_state")<<"/"<<MapGetString(d.data,"intent_state")
				     <<" source="<<MapGetString(d.data,"source_identity")<<" source_visible="<<(MapGetString(d.data,"source_identity").IsEmpty()?false:app.IsAssessmentSourceVisible(ws,MapGetString(d.data,"source_identity")))
				     <<" evidence="<<AsJSON(MapGet(d.data,"evidence_links"),true)<<" counter="<<AsJSON(MapGet(d.data,"counterevidence"),true)<<" raw_evidence="<<AsJSON(MapGet(d.data,"evidence"),true)<<"\n";
			}
			for(const DataDocument& snapshot:after_hypothesis_revision->data.GetDocuments()) if(snapshot.kind=="candidate-proposal"&&MapGetString(snapshot.data,"candidate_id")==hcid) {
				const DataDocument *candidate=after_hypothesis_revision->data.FindDocument(ws+":"+hcid), *revision=after_hypothesis_revision->data.FindDocument(ws+":"+MapGetString(snapshot.data,"proposal_revision_id"));
				String candidate_error, revision_error;
				Cerr()<<"T68 consistency snapshot="<<snapshot.id<<" candidate_valid="<<(candidate&&CandidateLineage::ValidateRecord(candidate->data,candidate_error))<<"/"<<candidate_error
				     <<" revision_valid="<<(revision&&CandidateLineage::ValidateRecord(revision->data,revision_error))<<"/"<<revision_error
				     <<" candidate_kind="<<(candidate?candidate->kind:String("missing"))<<" candidate_origin="<<(candidate?MapGetString(candidate->data,"origin_kind"):String(""))
				     <<" revision_origin="<<(revision?MapGetString(revision->data,"origin_kind"):String(""))<<" revision_candidate_match="<<(revision&&MapGetString(revision->data,"candidate_id")==hcid)
				     <<" uncertainty="<<(candidate&&revision&&MapGetString(candidate->data,"uncertainty")==MapGetString(revision->data,"uncertainty")&&MapGetString(revision->data,"uncertainty")==MapGetString(snapshot.data,"uncertainty"))
				     <<" evidence="<<(candidate&&revision&&AsJSON(MapGet(candidate->data,"evidence"),true)==AsJSON(MapGet(snapshot.data,"evidence_links"),true)&&AsJSON(MapGet(revision->data,"evidence"),true)==AsJSON(MapGet(snapshot.data,"evidence_links"),true))
				     <<" counter="<<(candidate&&revision&&AsJSON(MapGet(candidate->data,"counterevidence"),true)==AsJSON(MapGet(snapshot.data,"counterevidence"),true)&&AsJSON(MapGet(revision->data,"counterevidence"),true)==AsJSON(MapGet(snapshot.data,"counterevidence"),true))
				     <<" snapshot_states="<<MapGetString(snapshot.data,"submission_state")<<"/"<<MapGetString(snapshot.data,"approval_state")<<"/"<<MapGetString(snapshot.data,"author_state")<<"/"<<MapGetString(snapshot.data,"intent_state")<<" hypothetical="<<AsString(MapGet(snapshot.data,"hypothetical"))<<"\n";
			}
		}
		ValueMap hypo_snapshot_data=hyp_snapshot2?hyp_snapshot2->data:ValueMap();
		bool hypothetical_preserved=hypothesis_create_visible&&hypothesis_revision_visible&&MapGetString(h_project,"status")=="visible"&&MapGetString(hyp_projection,"status")=="visible"&&MapGet(hyp_projection,"hypothetical")==true&&MapGetString(lineage_hyp_projection,"status")=="visible"&&MapGet(lineage_hyp_projection,"hypothetical")==true&&hyp_candidate&&hyp_candidate_after&&hyp_snapshot&&hyp_snapshot1_after&&hyp_head&&hyp_head_after&&hyp_r2&&hyp_snapshot2&&
			MapGetString(hyp_candidate->data,"kind")=="local-interpretation"&&MapGetString(hyp_candidate->data,"origin_kind")=="local-hypothesis"&&MapGetString(hyp_candidate->data,"owner_kind")=="local-unknown-author"&&
			MapGetString(hyp_candidate->data,"submission_state")=="not-a-real-submission"&&MapGetString(hyp_candidate->data,"approval_state")=="not-approved"&&MapGetString(hyp_candidate->data,"author_state")=="unknown"&&MapGetString(hyp_candidate->data,"intent_state")=="unknown"&&MapGet(hyp_candidate->data,"hypothetical")==true&&IsValueArray(MapGet(hyp_candidate->data,"evidence"))&&IsValueArray(MapGet(hyp_candidate->data,"counterevidence"))&&!MapGetString(hyp_candidate->data,"uncertainty").IsEmpty()&&
			MapGet(hyp_snapshot->data,"hypothetical")==true&&MapGetString(hyp_snapshot->data,"submission_state")=="not-a-real-submission"&&MapGetString(hyp_snapshot->data,"approval_state")=="not-approved"&&MapGetString(hyp_snapshot->data,"author_state")=="unknown"&&MapGetString(hyp_snapshot->data,"intent_state")=="unknown"&&!MapGetString(hyp_snapshot->data,"uncertainty").IsEmpty()&&IsValueArray(MapGet(hyp_snapshot->data,"evidence_links"))&&IsValueArray(MapGet(hyp_snapshot->data,"counterevidence"))&&
			MapGetString(hyp_candidate_after->data,"origin_kind")=="local-hypothesis"&&MapGetString(hyp_candidate_after->data,"owner_kind")=="local-unknown-author"&&MapGetString(hyp_candidate_after->data,"submission_state")=="not-a-real-submission"&&MapGetString(hyp_candidate_after->data,"approval_state")=="not-approved"&&MapGetString(hyp_candidate_after->data,"author_state")=="unknown"&&MapGetString(hyp_candidate_after->data,"intent_state")=="unknown"&&MapGet(hyp_candidate_after->data,"hypothetical")==true&&
			MapGet(hypo_snapshot_data,"hypothetical")==true&&MapGetString(hypo_snapshot_data,"submission_state")=="not-a-real-submission"&&MapGetString(hypo_snapshot_data,"approval_state")=="not-approved"&&MapGetString(hypo_snapshot_data,"author_state")=="unknown"&&MapGetString(hypo_snapshot_data,"intent_state")=="unknown"&&!MapGetString(hypo_snapshot_data,"uncertainty").IsEmpty()&&IsValueArray(MapGet(hypo_snapshot_data,"evidence_links"))&&IsValueArray(MapGet(hypo_snapshot_data,"counterevidence"))&&
			MapGetString(hyp_r2->data,"origin_kind")=="local-hypothesis"&&MapGetString(hyp_r2->data,"submission_state")=="not-a-real-submission"&&MapGetString(hyp_r2->data,"approval_state")=="not-approved"&&MapGetString(hyp_r2->data,"author_state")=="unknown"&&MapGetString(hyp_r2->data,"intent_state")=="unknown"&&MapGet(hyp_r2->data,"hypothetical")==true&&
			MapGetString(hyp_head_after->data,"proposal_revision_id")==MapGetString(hypothetical_revision,"revision_id")&&hyp_head_after->revision!=hyp_head->revision&&MapGetString(rejected_flag_clear,"status")=="invalid"&&
			LoadFile(hyp_snapshot1_after->path)==hyp_snapshot_bytes&&MapGetString(hyp_projection,"status")=="visible"&&MapGet(hyp_projection,"hypothetical")==true&&MapGet(hyp_projection,"proposal_revision_chain").GetCount()==2;
		String rendered_form=CandidateWorkflow::Render(projected), rendered_hypothetical_form=CandidateWorkflow::Render(hyp_projection);
		bool hypothesis_hidden_input=rendered_hypothetical_form.Find("<input type=\"hidden\" name=\"hypothetical\" value=\"true\">")>=0 && rendered_hypothetical_form.Find("d.get('hypothetical')==='true'")>=0;
		String revise_op_marker="name=\"operation_id\" value=\"revise-"; int revise_op_pos=rendered_form.Find(revise_op_marker);
		String rendered_form_again=CandidateWorkflow::Render(projected); int revise_op_pos_again=rendered_form_again.Find(revise_op_marker);
		String revise_op_value, revise_op_value_again;
		if(revise_op_pos>=0) { int start=revise_op_pos+String("name=\"operation_id\" value=\"").GetCount(); int end=rendered_form.Find('"',start); if(end>start) revise_op_value=rendered_form.Mid(start,end-start); }
		if(revise_op_pos_again>=0) { int start=revise_op_pos_again+String("name=\"operation_id\" value=\"").GetCount(); int end=rendered_form_again.Find('"',start); if(end>start) revise_op_value_again=rendered_form_again.Mid(start,end-start); }
		bool denied_mutation_free=records_before_create>=0&&created.GetCount()>0&&pin->data.GetDocuments().GetCount()==records_before_create;
		bool ok=setup&&pin&&writable_restored&&live.is_authenticated&&early_write_authorized&&early_anonymous_denied&&early_wrong_workspace_denied&&no_session.Find("status")>=0&&MapGetString(no_session,"status")=="denied"&&MapGetString(denied_missing,"status")=="denied"&&MapGetString(expired_result,"status")=="denied"&&MapGetString(wrong_workspace,"status")=="denied"&&MapGetString(wrong_source,"status")=="denied"&&MapGetString(placeholder,"status")=="invalid"&&MapGetString(hidden_source_result,"status")=="invalid"&&MapGetString(readonly_result,"status")=="denied"&&MapGetString(created,"status")=="committed"&&MapGetString(retry,"status")=="already_committed"&&MapGetString(create_repeat_result,"status")=="already_committed"&&records_after_create==after_retry->data.GetDocuments().GetCount()&&wrong_workspace_request.GetStatusCode()==403&&wrong_workspace_body_response.Find("deliberately-missing-private-evidence")<0&&foreign_origin_status==403&&MapGetString(changed,"status")=="idempotency_conflict"&&MapGetString(revised,"status")=="committed"&&MapGetString(revision_retry,"status")=="already_committed"&&MapGetString(revision_payload_conflict,"status")=="idempotency_conflict"&&MapGetString(revision_repeat_response,"status")=="already_committed"&&MapGetString(stale,"status")=="stale"&&(MapGetString(fork,"status")=="stale"||MapGetString(fork,"status")=="identity_conflict")&&immutable&&prior_records_unchanged&&revision_two&&MapGetString(revision_two->data,"parent_revision_id")==r1&&denied_mutation_free&&MapGetString(projected,"status")=="visible"&&MapGet(projected,"proposal_revision_chain").GetCount()==2&&rendered_form.Find("/api/candidates/proposal/revise")>=0&&rendered_form.Find("name=\"workspace_id\" value=\""+ws+"\"")>=0&&rendered_form.Find("name=\"candidate_id\" value=\""+cid+"\"")>=0&&rendered_form.Find("name=\"proposal_id\" value=\""+pid+"\"")>=0&&rendered_form.Find("expected_head_revision")>=0&&rendered_form.Find("expected_revision_id")>=0&&rendered_form.Find("t17-evidence-item")>=0&&rendered_form.Find("deliberately-missing-private-evidence")<0&&revise_op_value.StartsWith("revise-")&&revise_op_value==revise_op_value_again&&rendered_form.Find("/api/candidates/proposal/revise")>=0&&rendered_form.Find("operation_id:d.get('operation_id')")>=0&&MapGetString(hypothetical_create,"status")=="committed"&&MapGetString(hypothetical_revision,"status")=="committed"&&hypothetical_preserved&&rendered_hypothetical_form.Find("Hypothetical local interpretation only")>=0&&rendered_hypothetical_form.Find("name=\"counterevidence\"")>=0&&rendered_hypothetical_form.Find("name=\"uncertainty\"")>=0&&rendered_form.Find("operation_id:'proposal-'+Date.now()")<0;
		if(!hypothesis_hidden_input) ok=false;
		if(!hypothesis_create_committed || !hypothesis_create_reloaded || !hypothesis_create_visible ||
		   !hypothesis_revision_committed || !hypothesis_revision_reloaded || !hypothesis_revision_visible ||
		   !hypothetical_preserved || MapGetString(rejected_normal_conversion,"status")!="invalid") ok=false;
		bool counterevidence_redacted=false;
		bool r1_history_preserved=r1_source_event&&r1_source_event_after&&LoadFile(r1_source_event_after->path)==r1_source_event_bytes&&r1_draft_event&&r1_draft_event_after_advance&&r1_draft_event_after&&LoadFile(r1_draft_event_after_advance->path)==r1_draft_event_bytes&&LoadFile(r1_draft_event_after->path)==r1_draft_event_bytes&&r1_stage_head_after_advance&&r1_stage_head_after&&r1_stage_head_after->revision==r1_stage_head_after_advance->revision&&MapGetString(r1_stage_head_after->data,"event_id")==MapGetString(r1_stage_head_after_advance->data,"event_id")&&LoadFile(r1_stage_head_after->path)==r1_stage_head_bytes;
		ValueArray projected_history=MapGet(projected,"history");
		bool r2_draft=MapGetString(projected,"status")=="visible"&&MapGetString(projected,"stage")=="draft"&&MapGetString(projected,"proposal_revision_id")==r2&&projected_history.GetCount()==1&&MapGetString((ValueMap)projected_history[0],"proposal_revision_id")==r2;
		bool t67_ok=true;
		auto require_t67=[&](const char *name,bool passed){if(!passed){Cerr()<<"FAILED T67 assertion: "<<name<<"\n";t67_ok=false;}};
		require_t67("r1 source-inspected commit",MapGetString(r1_source_stage,"status")=="committed");
		require_t67("r1 stale after r2 proposal head",MapGetString(old_revision_stage_attempt,"status")=="stale");
		require_t67("r1 stage event/head preserved",r1_history_preserved);
		require_t67("r1 event/head revision binding",r1_source_event&&r1_stage_head_after_advance&&MapGetString(r1_source_event->data,"proposal_revision_id")==r1&&MapGetString(r1_stage_head_after_advance->data,"proposal_revision_id")==r1);
		require_t67("r2 starts at draft with only r2 draft event",r2_draft);
		require_t67("linked valid commit diff ready",MapGetString(linked_diff_gate,"status")=="ready");
		require_t67("unrelated candidate commit diff blocked",MapGetString(unrelated_diff_gate,"status")=="blocked");
		require_t67("invalid commit source blocked",MapGetString(invalid_diff_gate,"status")=="blocked");
		require_t67("unrelated evidence seed committed",unrelated_write.committed);
		require_t67("unrelated assessment blocker",unrelated_assessment_denied);
		require_t67("item/snapshot revision mismatch blocker",mismatched_assessment_denied);
		require_t67("rubric/total mismatch blocker",semantic_assessment_denied);
		require_t67("exact linked item/snapshot pins ready",linked_assessment_ready);
		require_t67("stale assessment marker blocker",stale_assessment_denied);
		require_t67("other candidate diff not current candidate evidence",unrelated_candidate_diff_not_current);
		require_t67("r2 draft stage head/event exists",draft_stage_head&&draft_stage_event&&MapGetString(draft_stage_head->data,"proposal_revision_id")==r2&&MapGetString(draft_stage_event->data,"proposal_revision_id")==r2);
		require_t67("r2 draft projection parity",MapGetString(draft_projection,"status")=="visible"&&MapGetString(draft_projection,"stage")=="draft"&&MapGetString(draft_projection,"proposal_revision_id")==r2);
		require_t67("unrelated source evidence blocked",MapGetString(unrelated_source_stage,"status")=="blocked");
		require_t67("source-inspected transition committed",MapGetString(source_stage,"status")=="committed");
		require_t67("authenticated Same-Origin stage POST exact retry has no mutation",stage_http_retry_exact);
		require_t67("exact event retry recognized",MapGetString(source_stage_retry,"status")=="already_committed");
		require_t67("changed event retry conflicts",MapGetString(source_stage_changed,"status")=="idempotency_conflict");
		require_t67("stale head CAS rejected",MapGetString(stale_stage,"status")=="stale");
		require_t67("unauthorized stage write denied",MapGetString(no_auth_stage,"status")=="denied");
		require_t67("unrelated duplicate citation blocked",MapGetString(unrelated_review,"status")=="blocked");
		require_t67("duplicate review committed",MapGetString(review_stage,"status")=="committed");
		require_t67("competing head CAS rejected",MapGetString(competing_review,"status")=="stale");
		require_t67("missing review outcome rejected",MapGetString(review_missing_outcome,"status")=="invalid");
		require_t67("missing review citation denied",MapGetString(review_missing_citation,"status")=="denied");
		require_t67("review outcome and citations persisted",review_event&&MapGetString(review_event->data,"review_outcome")=="no_duplicate"&&MapGet(review_event->data,"evidence_ids").GetCount()==1);
		require_t67("T19 spike gate explicit",MapGetString(blocked_spike,"status")=="blocked"&&MapGetString(blocked_spike,"reason")=="completed_job_run_artifact_kind_unavailable_until_t19");
		require_t67("deliverables cannot skip T19",MapGetString(blocked_deliverable,"status")=="blocked");
		require_t67("review projection status",MapGetString(review_projection,"status")=="visible"&&MapGetString(review_projection,"stage")=="duplicate-reviewed");
		require_t67("T19 rendered blocker",rendered_stage.Find("spike-verified: blocked")>=0);
		require_t67("T20 rendered blocker",rendered_stage.Find("blocked_until_trusted_t20_verification_capability_schema")>=0);
		require_t67("stage form carries proposal revision",rendered_stage.Find("name=\"proposal_revision_id\" value=\""+r2+"\"")>=0);
		require_t67("stage POST rejects foreign origin",stage_foreign_origin_status==403);
		require_t67("authenticated HTML/API workflow routes match pinned headless projection",workflow_route_parity);
		const DataDocument *source_event_after=source_event&&after_review_stage?after_review_stage->data.FindDocument(ws+":"+source_event->id):NULL;
		require_t67("r2 stage event preserved",source_event&&source_event_after&&!source_event_bytes.IsEmpty()&&LoadFile(source_event_after->path)==source_event_bytes);
		require_t67("stage head revision increments",source_head&&review_head&&review_head->revision!=source_head->revision&&MapGetString(review_head->data,"revision")==review_head->revision&&MapGetString(MapGet(review_head->data,"envelope"),"revision")==review_head->revision);
		bool counterevidence_lineage_redacted=false;
		if(sf.visible) {
			sf.visible=false; registry.sources.Set(0,sf); app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
			ValueMap hidden_history=CandidateWorkflow::Project(after_review_stage->data,app,ws,cid);
			const DataDocument *h_draft=after_hypothesis_create?after_hypothesis_create->data.FindDocument(ws+":"+"candidate-stage-head-"+DataStore::ContentDigest(ws+"\n"+hcid+"\n"+hr1).Left(32)):NULL;
			ValueMap hidden_advance=h_draft?CandidateWorkflow::AdvanceStage(after_hypothesis_create->data,app,ws,hcid,hr1,hyp_candidate?hyp_candidate->revision:String(),h_draft->revision,MapGetString(h_draft->data,"event_id"),"source-inspected","hidden source link",stage_refs,write_source,"t67-hidden-source-link",live):ValueMap();
			ValueMap hidden_counterevidence=after_hypothesis_create?CandidateWorkflow::Project(after_hypothesis_create->data,app,ws,hcid):ValueMap();
			ValueMap nonexistent=CandidateWorkflow::Project(after_review_stage->data,app,ws,"t65-nonexistent-candidate");
			String hidden_history_json=AsJSON(hidden_history,true), nonexistent_json=AsJSON(nonexistent,true), hidden_candidate_json=AsJSON(hidden_counterevidence,true);
			require_t67("hidden history is generic denied",MapGetString(hidden_history,"status")=="denied"&&hidden_history.GetCount()==1&&hidden_history_json==nonexistent_json&&hidden_history_json.Find("t17-evidence-item")<0);
			require_t67("hidden transition is generic denied",MapGetString(hidden_advance,"status")=="denied"&&hidden_advance.GetCount()==1);
			require_t67("hidden proposal link is redacted",MapGetString(hidden_counterevidence,"status")=="denied"&&hidden_counterevidence.GetCount()==1&&hidden_candidate_json==AsJSON(CandidateWorkflow::Project(after_hypothesis_create->data,app,ws,"t65-nonexistent-candidate"),true)&&hidden_candidate_json.Find("Synthetic T66")<0);
			CatalogReadHandle hidden_route_pin=app.PinCatalog();ValueMap hidden_headless=hidden_route_pin?CandidateWorkflow::Project(hidden_route_pin->data,app,ws,hcid):ValueMap();
			HttpRequest hidden_workflow_api(base_url+"/api/candidates/workflow?workspace="+UrlEncode(ws)+"&candidate="+UrlEncode(hcid));hidden_workflow_api.Header("Cookie","wf_auth_session="+UrlEncode(live.token));hidden_workflow_api.Timeout(3000);String hidden_workflow_api_body=hidden_workflow_api.Execute();int hidden_workflow_api_status=hidden_workflow_api.GetStatusCode();Value hidden_workflow_api_value=ParseJSON(hidden_workflow_api_body);ValueMap hidden_workflow_api_result;if(hidden_workflow_api_value.Is<ValueMap>())hidden_workflow_api_result=hidden_workflow_api_value;
			HttpRequest hidden_workflow_html(base_url+"/candidate-workflow?workspace="+UrlEncode(ws)+"&candidate="+UrlEncode(hcid));hidden_workflow_html.Header("Cookie","wf_auth_session="+UrlEncode(live.token));hidden_workflow_html.Timeout(3000);String hidden_workflow_html_body=hidden_workflow_html.Execute();int hidden_workflow_html_status=hidden_workflow_html.GetStatusCode();
			String hidden_hrev2=MapGetString(hypothetical_revision,"revision_id");bool hidden_route_redacted=hidden_route_pin&&hidden_workflow_api_status==200&&hidden_workflow_html_status==200&&MapGetString(hidden_headless,"status")=="denied"&&AsJSON(hidden_workflow_api_result,true)==AsJSON(hidden_headless,true)&&hidden_workflow_api_result.GetCount()==1&&hidden_workflow_html_body.Find("Workflow status: <strong>denied</strong>")>=0&&hidden_workflow_html_body.Find(hcid)<0&&hidden_workflow_html_body.Find(hpid)<0&&hidden_workflow_html_body.Find(hr1)<0&&hidden_workflow_html_body.Find(hidden_hrev2)<0&&hidden_workflow_html_body.Find(unrelated_diff_item_id)<0&&hidden_workflow_html_body.Find(counter_item_id)<0&&hidden_workflow_html_body.Find("Synthetic T66 title")<0;
			require_t67("authenticated hidden-source HTML/API match redacted headless denial",hidden_route_redacted);
			sf.visible=true; registry.sources.Set(0,sf); app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
		}
		if(counter_sf.visible) {
			counter_sf.visible=false; registry.sources.Set(1,counter_sf); app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
			ValueMap hidden_counter_only=after_hypothesis_create?CandidateWorkflow::Project(after_hypothesis_create->data,app,ws,hcid):ValueMap();
			ValueMap hidden_lineage_counter=after_hypothesis_create?CandidateLineage::Project(after_hypothesis_create->data,app,ws):ValueMap();
			counterevidence_redacted=MapGetString(hidden_counter_only,"status")=="denied"&&hidden_counter_only.GetCount()==1;
			ValueArray hidden_lineage_records=MapGet(hidden_lineage_counter,"records"); String hidden_lineage_json=AsJSON(hidden_lineage_counter,true); bool hidden_placeholder=false;
			for(const Value& row:hidden_lineage_records) if(MapGetString((ValueMap)row,"kind")=="redacted-lineage-link") hidden_placeholder=true;
			counterevidence_lineage_redacted=MapGetString(hidden_lineage_counter,"status")=="visible"&&!MapGet(hidden_lineage_counter,"hypothetical")&&hidden_placeholder&&hidden_lineage_json.Find(hcid)<0&&hidden_lineage_json.Find(hpid)<0&&hidden_lineage_json.Find(hr1)<0&&hidden_lineage_json.Find(counter_item_id)<0&&hidden_lineage_json.Find(counter_source)<0;
			if(!counterevidence_redacted||!counterevidence_lineage_redacted) {
				const DataDocument *hidden_counter_doc=after_hypothesis_create?after_hypothesis_create->data.FindDocument(ws+":"+counter_item_id):NULL;
				Cerr()<<"T68 hidden-counter diagnostic workflow="<<MapGetString(hidden_counter_only,"status")<<"/"<<hidden_counter_only.GetCount()<<" lineage="<<MapGetString(hidden_lineage_counter,"status")<<"/"<<hidden_lineage_records.GetCount()<<" placeholder="<<hidden_placeholder<<" visible_lineage="<<MapGetString(lineage_hyp_projection,"status")<<" revise="<<MapGetString(hypothetical_revision,"status")<<"/"<<hypothesis_revision_committed<<"/"<<hypothesis_revision_reloaded<<" counter_policy="<<app.IsAssessmentSourceVisible(ws,counter_source)
				     <<" counter_doc="<<(hidden_counter_doc?hidden_counter_doc->valid:false)<<"/"<<(hidden_counter_doc?hidden_counter_doc->validated:false)<<"/"<<(hidden_counter_doc?MapGetString(hidden_counter_doc->data,"source_identity"):String("missing"))<<"\n";
				if(after_hypothesis_create) for(const DataDocument& snapshot:after_hypothesis_create->data.GetDocuments()) if(snapshot.kind=="candidate-proposal"&&MapGetString(snapshot.data,"candidate_id")==hcid) {
					const DataDocument *candidate=after_hypothesis_create->data.FindDocument(ws+":"+hcid), *revision=after_hypothesis_create->data.FindDocument(ws+":"+MapGetString(snapshot.data,"proposal_revision_id"));
					Cerr()<<"T68 hidden-counter snapshot id="<<snapshot.id<<" valid="<<snapshot.valid<<"/"<<snapshot.validated<<" candidate="<<(candidate?candidate->valid:false)<<"/"<<(candidate?candidate->validated:false)<<" revision="<<(revision?revision->valid:false)<<"/"<<(revision?revision->validated:false)
					     <<" uncertainty_match="<<(candidate&&revision&&MapGetString(candidate->data,"uncertainty")==MapGetString(revision->data,"uncertainty")&&MapGetString(revision->data,"uncertainty")==MapGetString(snapshot.data,"uncertainty"))
					     <<" evidence_match="<<(candidate&&revision&&AsJSON(MapGet(candidate->data,"evidence"),true)==AsJSON(MapGet(snapshot.data,"evidence_links"),true)&&AsJSON(MapGet(revision->data,"evidence"),true)==AsJSON(MapGet(snapshot.data,"evidence_links"),true))
					     <<" counter_match="<<(candidate&&revision&&AsJSON(MapGet(candidate->data,"counterevidence"),true)==AsJSON(MapGet(snapshot.data,"counterevidence"),true)&&AsJSON(MapGet(revision->data,"counterevidence"),true)==AsJSON(MapGet(snapshot.data,"counterevidence"),true))
					     <<" source="<<MapGetString(snapshot.data,"source_identity")<<" source_visible="<<app.IsAssessmentSourceVisible(ws,MapGetString(snapshot.data,"source_identity"))<<"\n";
				}
			}
			require_t67("hidden counterevidence is generic denied",counterevidence_redacted&&counterevidence_lineage_redacted);
			counter_sf.visible=true; registry.sources.Set(1,counter_sf); app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
		}
		require_t67("primary evidence source policy restored",app.IsAssessmentSourceVisible(ws,source_identity.object_id));
		require_t67("counterevidence source policy restored",app.IsAssessmentSourceVisible(ws,counter_source));
		bool fork_rejected=false, cycle_rejected=false; String fork_write_status, fork_projection_status, cycle_write_status, cycle_projection_status; ValueMap fork_projection_result, cycle_projection_result;
		CatalogReadHandle pre_fork_pin=app.PinLatestCatalog();String pre_fork_status=pre_fork_pin?MapGetString(CandidateWorkflow::Project(pre_fork_pin->data,app,ws,cid),"status"):String("no_catalog");
		require_t67("main candidate visible before fork tamper",pre_fork_status=="visible");
		if(source_event) {
			ValueMap fork_record=source_event->data; String fork_op="t67-fork-event-op"; String fork_id="candidate-stage-event-"+DataStore::ContentDigest(fork_op+"\n"+cid+"\n"+r2+"\nsource-inspected").Left(32); fork_record.Set("id",fork_id); ValueMap fork_env=MapGet(fork_record,"envelope"); fork_env.Set("stable_id",fork_id); fork_record.Set("envelope",fork_env); fork_record.Set("operation_id",fork_op);
			DataBatchChange fc; fc.action=DATA_BATCH_CREATE; fc.qualified_id=ws+":"+fork_id; fc.proposed=fork_record; Vector<DataBatchChange> fork_changes; fork_changes.Add(fc);
			ManagerSkylarkApp::BatchFinalizationResult fork_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-fork-writer-op",fork_changes); fork_write_status=fork_write.status;
			CatalogReadHandle fork_pin=app.PinLatestCatalog(); if(fork_pin) { fork_projection_result=CandidateWorkflow::Project(fork_pin->data,app,ws,cid); fork_projection_status=MapGetString(fork_projection_result,"status"); } fork_rejected=fork_write.committed&&fork_pin&&fork_projection_status=="denied"&&fork_projection_result.GetCount()==1;
			if(!fork_rejected) { ValueArray frefs=MapGet(fork_record,"evidence_ids"); String fid=frefs.IsEmpty()?String():String(frefs[0]); const DataDocument *fi=fork_pin&&!fid.IsEmpty()?fork_pin->data.FindDocument(ws+":"+fid):NULL;const DataDocument *fs=fi?fork_pin->data.FindDocument(ws+":"+MapGetString(fi->data,"snapshot")):NULL;Cerr()<<"T67 fork evidence diagnostic id="<<fid<<" item="<<(fi?fi->valid:false)<<":"<<(fi?fi->validated:false)<<":"<<(fi?fi->validation_state:String("none"))<<":"<<(fi?MapGetString(fi->data,"resolution"):String("none"))<<":"<<(fi?MapGetString(fi->data,"visibility"):String("none"))<<" snapshot="<<(fs?fs->valid:false)<<":"<<(fs?fs->validated:false)<<":"<<(fs?fs->validation_state:String("none"))<<":"<<(fs?MapGetString(fs->data,"resolution"):String("none"))<<":"<<(fs?MapGetString(fs->data,"visibility"):String("none"))<<" source="<<(fi?MapGetString(fi->data,"source_identity"):String("none"))<<" policy="<<(fi?app.IsAssessmentSourceVisible(ws,MapGetString(fi->data,"source_identity")):false)<<" forkdoc="<<(fork_pin&&fork_pin->data.FindDocument(ws+":"+fork_id)?fork_pin->data.FindDocument(ws+":"+fork_id)->valid:false)<<"\n"; }
		}
		String hrev2=MapGetString(hypothetical_revision,"revision_id");
		CatalogReadHandle before_cycle=app.PinLatestCatalog(); const DataDocument *hyp_stage_head=before_cycle?before_cycle->data.FindDocument(ws+":"+"candidate-stage-head-"+DataStore::ContentDigest(ws+"\n"+hcid+"\n"+hrev2).Left(32)):NULL;
		ValueArray h_stage_refs; h_stage_refs.Add(unrelated_diff_item_id);
		ValueMap hyp_source_stage=before_cycle&&hyp_stage_head?CandidateWorkflow::AdvanceStage(before_cycle->data,app,ws,hcid,hrev2,hyp_candidate?hyp_candidate->revision:String(),hyp_stage_head->revision,MapGetString(hyp_stage_head->data,"event_id"),"source-inspected","cycle fixture inspection",h_stage_refs,write_source,"t67-cycle-source",live):ValueMap();
		CatalogReadHandle cycle_source_pin=app.PinLatestCatalog(); const DataDocument *cycle_event=cycle_source_pin?cycle_source_pin->data.FindDocument(ws+":"+"candidate-stage-event-"+DataStore::ContentDigest("t67-cycle-source\n"+hcid+"\n"+hrev2+"\nsource-inspected").Left(32)):NULL;
		if(cycle_event) { ValueMap changed_event=cycle_event->data; changed_event.Set("previous_event_id",cycle_event->id); DataBatchChange cc; cc.action=DATA_BATCH_UPDATE; cc.qualified_id=cycle_event->qualified_id; cc.expected_revision=cycle_event->revision; cc.expected_source_revision=cycle_event->source_revision; cc.proposed=changed_event; Vector<DataBatchChange> cycle_changes; cycle_changes.Add(cc); ManagerSkylarkApp::BatchFinalizationResult cycle_write=SystemAuthService::FinalizeBatchForSession(app,live,write_source,ws,"t67-cycle-writer-op",cycle_changes); cycle_write_status=cycle_write.status; CatalogReadHandle cycle_pin=app.PinLatestCatalog(); if(cycle_pin) { cycle_projection_result=CandidateWorkflow::Project(cycle_pin->data,app,ws,hcid); cycle_projection_status=MapGetString(cycle_projection_result,"status"); } cycle_rejected=cycle_write.committed&&cycle_pin&&cycle_projection_status=="denied"&&cycle_projection_result.GetCount()==1; }
		require_t67("hypothesis revision cycle fixture committed",MapGetString(hyp_source_stage,"status")=="committed");
		if(!fork_rejected) Cerr()<<"T67 fork details write="<<fork_write_status<<" projection="<<fork_projection_status<<" keys="<<fork_projection_result.GetCount()<<"\n";
		require_t67("forked current revision history denied and redacted",fork_rejected);
		if(!cycle_rejected) Cerr()<<"T67 cycle details write="<<cycle_write_status<<" projection="<<cycle_projection_status<<" keys="<<cycle_projection_result.GetCount()<<" event="<<(cycle_event?cycle_event->id:String("missing"))<<"\n";
		require_t67("cyclic current revision history denied and redacted",cycle_rejected);
		if(!ok||!t67_ok) { String invalids, sources; if(after_create) for(const DataDocument& d:after_create->data.GetDocuments()) if(d.workspace_id==ws) { if(!d.valid) invalids<<d.id<<":"<<d.invalid_reason<<";"; sources<<d.id<<"="<<d.source_id<<"/"<<d.kind<<";"; } const DataDocument *debug_diff=after_hypothesis_create?after_hypothesis_create->data.FindDocument(ws+":"+unrelated_diff_item_id):NULL;const DataDocument *debug_diff_snapshot=debug_diff?after_hypothesis_create->data.FindDocument(ws+":"+MapGetString(debug_diff->data,"snapshot")):NULL; Cerr()<<"FAILED T66/T67 proposal and stage workflow setup="<<setup<<" t67="<<t67_ok<<" mismatch_assessment="<<mismatched_assessment_write.committed<<":"<<mismatched_assessment_write.status<<"/"<<mismatched_assessment_denied<<" linked_assessment="<<linked_assessment_write.committed<<":"<<linked_assessment_write.status<<"/"<<linked_assessment_ready<<" stale_assessment="<<stale_marker_write.committed<<":"<<stale_marker_write.status<<"/"<<stale_assessment_denied<<" current_diff="<<current_proposal_has_diff<<" other_diff="<<other_proposal_has_diff<<" diff_item="<<(debug_diff?debug_diff->valid:false)<<":"<<(debug_diff?debug_diff->validation_state:String("none"))<<":"<<(debug_diff?MapGetString(MapGet(debug_diff->data,"locator"),"type"):String("none"))<<":"<<(debug_diff?MapGetString(debug_diff->data,"resolution"):String("none"))<<":"<<(debug_diff?MapGetString(debug_diff->data,"visibility"):String("none"))<<" diff_snapshot="<<(debug_diff_snapshot?debug_diff_snapshot->valid:false)<<":"<<(debug_diff_snapshot?MapGetString(debug_diff_snapshot->data,"source_kind"):String("none"))<<":"<<(debug_diff_snapshot?MapGetString(debug_diff_snapshot->data,"resolution"):String("none"))<<":"<<(debug_diff_snapshot?MapGetString(debug_diff_snapshot->data,"visibility"):String("none"))<<":"<<(debug_diff_snapshot?MapGetString(MapGet(debug_diff_snapshot->data,"locator"),"type"):String("none"))<<":"<<(debug_diff_snapshot?AsJSON(MapGet(debug_diff_snapshot->data,"locator"),true):String("none"))<<" item_locator="<<(debug_diff?AsJSON(MapGet(debug_diff->data,"locator"),true):String("none"))<<" hlinks="<<AsJSON(MapGet(h_current_form,"evidence_links"),true)<<" hypothesis="<<AsJSON(hypothetical_create,true)<<" hproject="<<AsJSON(h_project,true)<<" unlinked_source="<<AsJSON(unrelated_source_stage,true)<<" unlinked_review="<<AsJSON(unrelated_review,true)<<" source_bytes_preserved="<<(source_event_after!=NULL&&!source_event_bytes.IsEmpty()&&LoadFile(source_event_after->path)==source_event_bytes)<<" head_revs="<<(source_head?source_head->revision:String("none"))<<"/"<<(review_head?review_head->revision:String("none"))<<" head_data_revs="<<(source_head?MapGetString(source_head->data,"revision"):String("none"))<<"/"<<(review_head?MapGetString(review_head->data,"revision"):String("none"))<<" head_env_revs="<<(source_head?MapGetString(MapGet(source_head->data,"envelope"),"revision"):String("none"))<<"/"<<(review_head?MapGetString(MapGet(review_head->data,"envelope"),"revision"):String("none"))<<" docs="<<sources<<" invalid="<<invalids<<" create="<<AsJSON(created,true)<<" draft_projection="<<AsJSON(draft_projection,true)<<" source_stage="<<AsJSON(source_stage,true)<<" source_retry="<<AsJSON(source_stage_retry,true)<<" changed_retry="<<AsJSON(source_stage_changed,true)<<" stale="<<AsJSON(stale_stage,true)<<" review="<<AsJSON(review_stage,true)<<" missing_outcome="<<AsJSON(review_missing_outcome,true)<<" missing_citation="<<AsJSON(review_missing_citation,true)<<" blocked_spike="<<AsJSON(blocked_spike,true)<<" blocked_deliverable="<<AsJSON(blocked_deliverable,true)<<" review_projection="<<AsJSON(review_projection,true)<<" render_contains="<<rendered_stage.Find("spike-verified: blocked")<<"\n"; Cerr()<<"T67 diagnostic diff-source="<<(debug_diff?MapGetString(debug_diff->data,"source_identity"):String("none"))<<" snapshot-source="<<(debug_diff_snapshot?MapGetString(debug_diff_snapshot->data,"source_identity"):String("none"))<<" retracted="<<(debug_diff_snapshot?AsJSON(MapGet(debug_diff_snapshot->data,"retracted"),true):String("none"))<<" lines="<<(debug_diff?AsJSON(MapGet(debug_diff->data,"locator"),true):String("none"))<<"/"<<(debug_diff_snapshot?AsJSON(MapGet(debug_diff_snapshot->data,"locator"),true):String("none"))<<" policy="<<(debug_diff?app.IsAssessmentSourceVisible(ws,MapGetString(debug_diff->data,"source_identity")):false)<<" firsttypes="<<(debug_diff?MapGet(MapGet(debug_diff->data,"locator"),"first").GetType():0)<<"/"<<(debug_diff?MapGet(MapGet(debug_diff->data,"locator"),"last").GetType():0)<<"\n"; }
		app.instance_policy=prior_instance; app.policy_workspace_settings=prior_workspace; app.policy_operator_settings=prior_operator; app.trusted_policy_registry=pick(prior_registry);
		app.test_registration=prior; app.test_registration_change=prior_change; app.test_registration_provider=prior_provider; app.test_registration_change_point=prior_change_point;
		bool restored=app.RefreshCatalog();
		if(!ok||!t67_ok||!restored) return false;
		Cout()<<"ok    T66 proposal preflight/idempotency/revision and T67 persisted linked-evidence stages, exact assessment pins, CAS/retry, redaction, fork/cycle denial and T19/T20 blockers\n";
	}
	// T83 HTTP integration uses a disposable writable source and restores the
	// app registration and trusted policy unconditionally before checking errors.
	{
		const String ws="t83-http-workspace",source="t83-http-source",hidden="t83-http-hidden-source";
		String root=workspace.FixtureRoot("t83-http-review");
		DataSourceRegistration prior=app.test_registration,prior_change=app.test_registration_change;
		bool prior_provider=app.test_registration_provider; int prior_change_point=app.test_registration_change_point;
		String prior_scanned=app.test_registration_scanned_root,prior_root=app.test_fixture_root;
		PolicySettings prior_instance=app.instance_policy,prior_workspace_policy=app.policy_workspace_settings,prior_operator=app.policy_operator_settings;
		TrustedPolicyRegistry prior_registry=pick(app.trusted_policy_registry);
		bool hidden_package_ok=true,denied_check_ok=true,hidden_comment_ok=true,cross_comment_ok=true,cross_check_ok=true,invalid_thread_ok=true,incomplete_ok=true;
		bool t83_ok=CreatePrivateDirectory(root)&&RealizeDirectory(AppendFileName(root,"v2"))&&RealizeDirectory(AppendFileName(root,"data"));
		if(!t83_ok) Cerr()<<"T83 fixture directory setup failed\n";
		app.test_fixture_root=root; app.test_registration_provider=true; app.test_registration.id=source; app.test_registration.root=NormalizePath(root);
		app.test_registration.enabled=true; app.test_registration.read_only=false; app.test_registration.write_enabled=true;
		app.test_registration_change=app.test_registration; app.test_registration_change_point=0; app.test_registration_scanned_root.Clear();
		TrustedPolicyRegistry registry; registry.configured=true; PolicyWorkspaceFact workspace_fact; workspace_fact.identity.workspace_id=ws; workspace_fact.identity.object_id=ws; registry.workspaces.Add(workspace_fact);
		for(const String& identity:{source,hidden,String("t83-evidence-source"),String("t83-topology-source")}) { PolicyIdentity pi; pi.workspace_id=ws; pi.object_id=identity; PolicySourceFact sf; sf.workspace=workspace_fact.identity; sf.identity=pi; sf.classification=POLICY_PUBLIC; sf.visible=identity!=hidden; registry.sources.Add(sf); PolicyProviderFact pf; pf.workspace=workspace_fact.identity; pf.identity=pi; pf.training_policy="eligible"; pf.enabled=true; pf.config_revision="t83-r1"; pf.secret_reference="secretref:t83-synthetic"; registry.providers.Add(pf); }
		PolicySettings instance; instance.configured=true; PolicySettings no_overrides;
		auto refresh_visible=[&]() { if(!app.RefreshCatalog()) return false; return app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry); };
		if(t83_ok) { bool loaded=app.RefreshCatalog(); String policy_error; bool valid_registry=registry.Validate(policy_error); bool installed=loaded&&app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry); if(!installed) Cerr()<<"T83 initial policy/catalog setup failed mode="<<app.IsIsolatedTestMode()<<" registry="<<valid_registry<<":"<<policy_error<<" loaded="<<loaded<<"\n"; t83_ok=installed; }
		UserSession session=SystemAuthService::CreateTestSessionForTests();
		ReviewPackagePreview first; String comment_id,thread_id,head_comment_id,head_thread_id,head_comment_body="T83_HEAD_COMMENT_SECRET_SENTINEL",stored_comment_body="<script>alert('t83')</script>& reviewed\nsecond line",comment_body=" \t<script>alert('t83')</script>& reviewed\nsecond line \t ";
		DataWriteAuthorization write; write.authenticated=true; write.action_scope="record:write"; write.principal=session.username; write.source_id=source; write.workspace_id=ws;
		DataStore writer;
		if(t83_ok&&(!writer.LoadWriteFixtureForTests(root,source))) { Cerr()<<"T83 writable fixture load failed\n"; t83_ok=false; }
		// A registered catalog root is shared by V2Store and DataStore, so V2Store's
		// canonical goals directory is root/goals (the separate fixture adapter uses root/v2).
		String goal_dir=AppendFileName(root,"goals/"+ws); RealizeDirectory(goal_dir);
		ValueMap goal_record; goal_record.Add("id","t83-visible-goal"); goal_record.Add("kind","goal"); goal_record.Add("repo",ws); goal_record.Add("workspace_id",ws); goal_record.Add("source_identity",source); goal_record.Add("title","T83 <Visible & Goal>"); goal_record.Add("tags",ValueArray()); goal_record.Add("state",ValueMap()); goal_record.Add("tasks",ValueArray());
		ValueMap ambiguous_goal=goal_record; ambiguous_goal.Set("repo","t83-alternate-repo"); ambiguous_goal.Set("workspace_id","other-workspace");
		ValueMap hidden_goal=goal_record; hidden_goal.Set("id","t83-hidden-goal"); hidden_goal.Set("repo","t83-hidden-repo"); hidden_goal.Set("source_identity",hidden);
		String goal_path=AppendFileName(goal_dir,"visible.json"),ambiguous_goal_path=AppendFileName(AppendFileName(root,"goals/t83-alternate-repo"),"ambiguous.json"); RealizeDirectory(GetFileFolder(ambiguous_goal_path));
		String hidden_goal_path=AppendFileName(AppendFileName(root,"goals/t83-hidden-repo"),"hidden.json"); RealizeDirectory(GetFileFolder(hidden_goal_path));
		if(t83_ok&&(!SaveFile(goal_path,AsJSON(goal_record,true))||!SaveFile(ambiguous_goal_path,AsJSON(ambiguous_goal,true))||!SaveFile(hidden_goal_path,AsJSON(hidden_goal,true)))) t83_ok=false;
		String explicit_v2_goals=AppendFileName(root,"v2/goals");
		RealizeDirectory(AppendFileName(explicit_v2_goals,ws)); RealizeDirectory(AppendFileName(explicit_v2_goals,"t83-alternate-repo")); RealizeDirectory(AppendFileName(explicit_v2_goals,"t83-hidden-repo"));
		if(t83_ok&&(!SaveFile(AppendFileName(AppendFileName(explicit_v2_goals,ws),"visible.json"),AsJSON(goal_record,true))||!SaveFile(AppendFileName(AppendFileName(explicit_v2_goals,"t83-alternate-repo"),"ambiguous.json"),AsJSON(ambiguous_goal,true))||!SaveFile(AppendFileName(AppendFileName(explicit_v2_goals,"t83-hidden-repo"),"hidden.json"),AsJSON(hidden_goal,true)))) t83_ok=false;
		V2Store t83_v2_probe,explicit_t83_v2_probe; bool t83_v2_probe_loaded=t83_v2_probe.LoadFixtureForTests(root),explicit_v2_loaded=explicit_t83_v2_probe.LoadFixtureForTests(AppendFileName(root,"v2"));
		if(!t83_v2_probe_loaded||t83_v2_probe.GetGoals().GetCount()!=3||!explicit_v2_loaded||explicit_t83_v2_probe.GetGoals().GetCount()!=3) t83_ok=false;
		String topology_id="topology/t83-review-scope/detail",topology_path=AppendFileName(root,"data/t83-topology.json");
		ValueMap topology_node; topology_node.Add("id","t83-node"); topology_node.Add("type","service"); topology_node.Add("name","T83 <Topology & node>"); ValueArray topology_nodes; topology_nodes.Add(topology_node);
		ValueMap topology_record; topology_record.Add("id",topology_id); topology_record.Add("kind","topology"); topology_record.Add("workspace_id",ws); topology_record.Add("source_identity","t83-topology-source"); topology_record.Add("name","T83 <Topology & map>"); topology_record.Add("nodes",topology_nodes); topology_record.Add("edges",ValueArray());
		ValueMap topology_env; topology_env.Add("schema_id","skylark/topology"); topology_env.Add("schema_version","1"); topology_env.Add("workspace_id",ws); topology_env.Add("stable_id",topology_id); topology_env.Add("record_kind","topology"); topology_env.Add("revision","1"); topology_env.Add("visibility","private"); topology_env.Add("provenance_ref","synthetic-t83"); topology_record.Add("envelope",topology_env);
		if(t83_ok&&!SaveFile(topology_path,AsJSON(topology_record,true))) t83_ok=false;
		const String evidence_snapshot_id="t83-evidence-snapshot",evidence_item_id="t83-evidence-item",evidence_excerpt="T83 <Evidence & excerpt>";
		ValueMap locator; locator.Add("type","paper-section"); locator.Add("id","t83-source-paper"); locator.Add("page",3); locator.Add("section","<Methods & results>");
		String evidence_hash=DataStore::ContentDigest(evidence_excerpt);
		ValueMap evidence_snapshot; evidence_snapshot.Add("id",evidence_snapshot_id); evidence_snapshot.Add("kind","evidence-snapshot"); evidence_snapshot.Add("workspace_id",ws); evidence_snapshot.Add("source_kind","paper"); evidence_snapshot.Add("source_identity","t83-evidence-source"); evidence_snapshot.Add("upstream_revision","t83-source-r3"); evidence_snapshot.Add("previous_revision",""); evidence_snapshot.Add("content_sha256",evidence_hash); evidence_snapshot.Add("captured_at","2026-01-01T00:00:00Z"); evidence_snapshot.Add("source_at","2026-01-01T00:00:00Z"); evidence_snapshot.Add("collector","synthetic-t83"); evidence_snapshot.Add("locator",locator); evidence_snapshot.Add("resolution","resolved"); evidence_snapshot.Add("content",evidence_excerpt); evidence_snapshot.Add("classification","public"); evidence_snapshot.Add("visibility","visible"); evidence_snapshot.Add("retracted",false); evidence_snapshot.Add("predecessor","none"); evidence_snapshot.Add("contributors",ValueArray());
		ValueMap snapshot_env; snapshot_env.Add("schema_id","skylark/evidence-snapshot"); snapshot_env.Add("schema_version","2"); snapshot_env.Add("workspace_id",ws); snapshot_env.Add("stable_id",evidence_snapshot_id); snapshot_env.Add("record_kind","evidence-snapshot"); snapshot_env.Add("revision","1"); snapshot_env.Add("visibility","private"); snapshot_env.Add("provenance_ref","synthetic-t83"); evidence_snapshot.Add("envelope",snapshot_env);
		ValueMap evidence_item; evidence_item.Add("id",evidence_item_id); evidence_item.Add("kind","evidence-item"); evidence_item.Add("workspace_id",ws); evidence_item.Add("snapshot",evidence_snapshot_id); evidence_item.Add("source_identity","t83-evidence-source"); evidence_item.Add("upstream_revision","t83-source-r3"); evidence_item.Add("content_sha256",evidence_hash); evidence_item.Add("locator",locator); evidence_item.Add("resolution","resolved"); evidence_item.Add("excerpt",evidence_excerpt); evidence_item.Add("classification","public"); evidence_item.Add("visibility","visible"); evidence_item.Add("contributors",ValueArray());
		ValueMap item_env=snapshot_env; item_env.Set("schema_id","skylark/evidence-item"); item_env.Set("stable_id",evidence_item_id); item_env.Set("record_kind","evidence-item"); evidence_item.Add("envelope",item_env);
		if(t83_ok&&(!SaveFile(AppendFileName(root,"data/t83-evidence-snapshot.json"),AsJSON(evidence_snapshot,true))||!SaveFile(AppendFileName(root,"data/t83-evidence-item.json"),AsJSON(evidence_item,true)))) t83_ok=false;
		ReviewPackageInput input; input.workspace_id=ws; input.goal_id="t83-visible-goal"; input.scope_id="t83-scope"; input.base_revision="t83-base"; input.head_revision="t83-head-1"; input.selected_paths.Add("src/t83.cpp"); input.author=session.username; input.author_state="known-local-author"; input.capture_state="captured"; input.check_refs.Add("t83-review-scope"); input.artifact_refs.Add(evidence_item_id);
		input.patch_bytes="diff --git a/src/t83.cpp b/src/t83.cpp\n--- a/src/t83.cpp\n+++ b/src/t83.cpp\n@@ -1 +1 @@\n-old\n+T83_PATCH_SECRET_SENTINEL\n";
		if(t83_ok) { first=ReviewPackage::Capture(writer,input,write,"t83-capture-r1"); String recovery=writer.RecoverPendingBatches(write).status; bool refreshed=first.status=="committed"&&recovery=="recovered"&&refresh_visible(); CatalogReadHandle initial_link_pin=app.PinLatestCatalog(); t83_ok=refreshed&&initial_link_pin&&initial_link_pin->v2.GetGoals().GetCount()==3; }
		String base_url="http://127.0.0.1:"+AsString(port);
		auto get=[&](const String& path,int& status) { HttpRequest req(base_url+path); req.Header("Cookie","wf_auth_session="+UrlEncode(session.token)); req.Timeout(3000); String body=req.Execute(); status=req.GetStatusCode(); return body; };
		auto post=[&](const ValueMap& data,const String& origin,int& status) { HttpRequest req(base_url+"/api/reviews/comment"); req.POST(); req.Header("Cookie","wf_auth_session="+UrlEncode(session.token)); req.Header("Origin",origin); req.ContentType("application/json"); req.PostData(AsJSON(data,true)); req.Timeout(3000); String body=req.Execute(); status=req.GetStatusCode(); return body; };
		if(t83_ok) {
			int status=0; String overview_html=get("/reviews",status); int api_status=0; String overview_api=get("/api/reviews",api_status);
			CatalogReadHandle before_comment=app.PinLatestCatalog(); ValueMap projection=before_comment?ReviewPackageViewService::Build(app,*before_comment,first.package_id,first.revision_id):ValueMap();
			if(MapGetString(projection,"view_state")!="ready") { const DataDocument *revision=before_comment?before_comment->data.FindDocument(ws+":"+first.revision_id):NULL; Cerr()<<"T83 projection diagnostics catalog="<<(before_comment?before_comment->status:String("none"))<<" data="<<(before_comment?before_comment->data_status:String("none"))<<" rev="<<(revision?revision->valid:false)<<":"<<(revision?revision->validated:false)<<":"<<(revision?revision->validation_state:String("none"))<<":"<<(revision?revision->source_id:String("none"))<<" source_visible="<<app.IsAssessmentSourceVisible(ws,source)<<"\n"; }
			ValueMap comment_request; comment_request.Add("workspace_id",ws); comment_request.Add("package_id",first.package_id); comment_request.Add("revision_id",first.revision_id); comment_request.Add("thread_id",String()); comment_request.Add("body",comment_body); comment_request.Add("record_type","comment"); comment_request.Add("operation_id","t83-comment-op");
			int write_status=0; ValueMap write_result=ParseJSON(post(comment_request,base_url,write_status)); comment_id=MapGetString(write_result,"comment_id");
			CatalogReadHandle after_comment=app.PinLatestCatalog(); const DataDocument *saved_comment=after_comment?after_comment->data.FindDocument(ws+":"+comment_id):NULL;
			thread_id=saved_comment?MapGetString(saved_comment->data,"thread_id"):String();
			uint64 retry_generation=after_comment?after_comment->generation:0; int retry_status=0; ValueMap retry_result=ParseJSON(post(comment_request,base_url,retry_status)); CatalogReadHandle after_retry=app.PinLatestCatalog();
			ValueMap forged=comment_request; forged.Add("author","forged-reviewer"); int forged_status=0; post(forged,base_url,forged_status);
			ValueMap huge=comment_request; huge.Set("operation_id","t83-huge-body"); huge.Set("body",String('x',8193)); int huge_status=0; ValueMap huge_result=ParseJSON(post(huge,base_url,huge_status));
			ValueMap control=comment_request; control.Set("operation_id","t83-control-body"); String nul_body="bad"; nul_body.Cat((char)0); nul_body<<"body"; control.Set("body",nul_body); int control_status=0; ValueMap control_result=ParseJSON(post(control,base_url,control_status));
			ValueMap malformed=comment_request; malformed.Set("operation_id","t83-utf8-body"); String invalid_utf8; invalid_utf8.Cat((char)0xc0); invalid_utf8.Cat((char)0xaf); malformed.Set("body",invalid_utf8); int utf8_status=0; ValueMap utf8_result=ParseJSON(post(malformed,base_url,utf8_status));
			ValueMap changed=comment_request; changed.Set("body","changed under same operation"); int changed_status=0; ValueMap changed_result=ParseJSON(post(changed,base_url,changed_status));
			ValueMap hostile_origin=comment_request; hostile_origin.Set("operation_id","t83-cross-origin"); int origin_status=0; post(hostile_origin,"https://attacker.invalid",origin_status);
			CatalogReadHandle after_denials=app.PinLatestCatalog();
			bool utf8_denied=utf8_status>=400||MapGetString(utf8_result,"status")=="invalid";
			bool rejects_no_write=forged_status==400&&MapGetString(huge_result,"status")=="invalid"&&MapGetString(control_result,"status")=="invalid"&&utf8_denied&&MapGetString(changed_result,"status")=="conflict"&&origin_status==403&&after_retry&&after_denials&&after_denials->generation==retry_generation;
			DataStore second_writer; if(!second_writer.LoadWriteFixtureForTests(root,source)) t83_ok=false;
			ReviewPackageInput next=input; next.previous_revision=first.revision_id; next.head_revision="t83-head-2"; next.patch_bytes="diff --git a/src/t83.cpp b/src/t83.cpp\n--- a/src/t83.cpp\n+++ b/src/t83.cpp\n@@ -1 +1 @@\n-old\n+T83_PATCH_SECOND_SENTINEL\n";
			ReviewPackagePreview second; if(t83_ok) { second=ReviewPackage::Capture(second_writer,next,write,"t83-capture-r2"); bool second_ready=second.status=="committed"&&second_writer.RecoverPendingBatches(write).status=="recovered"&&refresh_visible(); if(!second_ready) Cerr()<<"T83 second revision diagnostics="<<second.status<<"\n"; t83_ok=second_ready; }
			ValueMap head_comment_request=comment_request; head_comment_request.Set("revision_id",second.revision_id); head_comment_request.Set("body",head_comment_body); head_comment_request.Set("operation_id","t83-head-comment-op");
			int head_comment_status=0; ValueMap head_comment_result=ParseJSON(post(head_comment_request,base_url,head_comment_status)); head_comment_id=MapGetString(head_comment_result,"comment_id");
			CatalogReadHandle after_head_comment=app.PinLatestCatalog(); const DataDocument *saved_head_comment=after_head_comment?after_head_comment->data.FindDocument(ws+":"+head_comment_id):NULL; head_thread_id=saved_head_comment?MapGetString(saved_head_comment->data,"thread_id"):String();
			int detail_status=0,detail_api_status=0,thread_status=0,thread_api_status=0;
			String detail_html=get("/review-package/"+UrlEncode(first.package_id)+"/"+UrlEncode(first.revision_id),detail_status);
			String detail_api=get("/api/reviews/package/"+UrlEncode(first.package_id)+"/"+UrlEncode(first.revision_id),detail_api_status);
			String thread_html=get("/review-thread/"+UrlEncode(thread_id),thread_status),thread_api=get("/api/reviews/thread/"+UrlEncode(thread_id),thread_api_status);
			int fresh_overview_status=0,fresh_overview_api_status=0; String fresh_overview_html=get("/reviews",fresh_overview_status),fresh_overview_api=get("/api/reviews",fresh_overview_api_status);
			ValueMap detail_json=ParseJSON(detail_api),thread_json=ParseJSON(thread_api);
			CatalogReadHandle parity_pin=app.PinLatestCatalog(); ValueMap expected_overview=parity_pin?ReviewPackageViewService::Build(app,*parity_pin):ValueMap(); ValueMap expected_detail=parity_pin?ReviewPackageViewService::Build(app,*parity_pin,first.package_id,first.revision_id):ValueMap();
			ValueMap fresh_overview_json=ParseJSON(fresh_overview_api);
			ValueMap linked_package=MapGet(detail_json,"package"); ValueArray goal_links=MapGet(linked_package,"goal_links"),topology_links=MapGet(linked_package,"topology_links"),evidence_links=MapGet(linked_package,"evidence_items");
			ValueMap goal_link=goal_links.GetCount()==1?(ValueMap)goal_links[0]:ValueMap(),topology_link=topology_links.GetCount()==1?(ValueMap)topology_links[0]:ValueMap(),evidence_link=evidence_links.GetCount()==1?(ValueMap)evidence_links[0]:ValueMap();
			String goal_url=MapGetString(goal_link,"url"),topology_url=MapGetString(topology_link,"url"),evidence_anchor=MapGetString(evidence_link,"anchor");
			String linked_goal_page=goal_url.IsEmpty()?String():get(goal_url,detail_status); int linked_goal_status=detail_status;
			String linked_topology_page=topology_url.IsEmpty()?String():get(topology_url,detail_status); int linked_topology_status=detail_status;
			String evidence_href="href=\"#"+evidence_anchor+"\"",evidence_id="id=\""+evidence_anchor+"\"";
			ValueArray fresh_rows=MapGet(fresh_overview_json,"packages"); ValueMap linked_overview=fresh_rows.GetCount()==1?(ValueMap)fresh_rows[0]:ValueMap();
			ValueArray overview_goals=MapGet(linked_overview,"goal_links"),overview_topologies=MapGet(linked_overview,"topology_links"),overview_evidence=MapGet(linked_overview,"evidence_items");
			String overview_typed_links=AsJSON(linked_overview,true);
			bool t84_actual_links=goal_url.Find("/goal/t83-http-workspace%3Agoal%3At83-visible-goal?workspace=t83-http-workspace&package=")==0&&goal_url.Find("&revision=")>=0&&topology_url.Find("/data/diagram/scope/t83-review-scope?workspace=t83-http-workspace&package=")==0&&topology_url.Find("&revision=")>=0&&evidence_anchor=="review-evidence-t83-evidence-item"&&MapGetString(evidence_link,"snapshot_id")=="t83-evidence-snapshot"&&MapGetString(evidence_link,"snapshot_revision").GetCount()>0&&MapGetString(evidence_link,"snapshot_source_revision").GetCount()>0&&MapGetString(evidence_link,"upstream_revision")=="t83-source-r3"&&AsJSON(MapGet(evidence_link,"locator"),true).Find("<Methods & results>")>=0&&MapGetString(evidence_link,"excerpt")=="T83 <Evidence & excerpt>"&&detail_html.Find("href=\""+goal_url.Left(goal_url.Find("?"))+"?")>=0&&detail_html.Find("href=\""+topology_url.Left(topology_url.Find("?"))+"?")>=0&&detail_html.Find(evidence_href)>=0&&detail_html.Find(evidence_id)>=0&&detail_html.Find("&lt;Visible &amp; Goal&gt;")>=0&&detail_html.Find("T83 &lt;Topology &amp; map&gt;")>=0&&detail_html.Find("T83 &lt;Evidence &amp; excerpt&gt;")>=0&&detail_html.Find("<Methods & results>")<0&&linked_goal_status==200&&linked_goal_page.Find("t83-visible-goal")>=0&&linked_topology_status==200&&linked_topology_page.Find("Topology: scope/t83-review-scope")>=0&&overview_goals.GetCount()==1&&overview_topologies.GetCount()==1&&overview_evidence.GetCount()==1&&overview_typed_links.Find("/goal/t83-http-workspace%3Agoal%3At83-visible-goal")>=0&&overview_typed_links.Find("/data/diagram/scope/t83-review-scope")>=0&&overview_typed_links.Find("#review-evidence-t83-evidence-item")>=0;
			auto check_redacted_package_refs=[&](const ValueMap& changed,const String& forbidden,bool bad_goal=true,bool bad_topology=true,bool bad_evidence=true) {
				CatalogReadHandle before=app.PinLatestCatalog(); const DataDocument *doc=before?before->data.FindDocument(ws+":"+first.revision_id):NULL; if(!doc) return false;
				String path=doc->path,original=LoadFile(path); ValueMap changed_record=doc->data;
				for(const char *field:{"goal_id","check_refs","artifact_refs"}) { int field_at=changed.Find(field); if(field_at>=0) changed_record.Set(field,changed.GetValue(field_at)); }
				if(!SaveFile(path,AsJSON(changed_record,true))||!refresh_visible()) { SaveFile(path,original); refresh_visible(); return false; }
				CatalogReadHandle changed_pin=app.PinLatestCatalog(); ValueMap projected=changed_pin?ReviewPackageViewService::Build(app,*changed_pin,first.package_id,first.revision_id):ValueMap();
				int hs=0,js=0; String rendered=get("/review-package/"+UrlEncode(first.package_id)+"/"+UrlEncode(first.revision_id),hs),api=get("/api/reviews/package/"+UrlEncode(first.package_id)+"/"+UrlEncode(first.revision_id),js); ValueMap api_view=ParseJSON(api),projected_package=MapGet(projected,"package");
				String combined=AsJSON(projected,true)+api+rendered; ValueArray bad_goals=MapGet(projected_package,"goal_links"),bad_topology_links=MapGet(projected_package,"topology_links"),bad_evidence_links=MapGet(projected_package,"evidence_items");
				bool ok=MapGetString(projected,"view_state")=="ready"&&hs==200&&js==200&&MapGetString(api_view,"view_state")=="ready"&&combined.Find(forbidden)<0&&combined.Find(evidence_hash)<0&&(!bad_goal||MapGetString(projected_package,"goal_id").IsEmpty())&&(!bad_goal||(!bad_goals.IsEmpty()&&(bool)MapGet((ValueMap)bad_goals[0],"unavailable")))&&(!bad_topology||(!bad_topology_links.IsEmpty()&&(bool)MapGet((ValueMap)bad_topology_links[bad_topology_links.GetCount()-1],"unavailable")))&&(!bad_evidence||(!bad_evidence_links.IsEmpty()&&(bool)MapGet((ValueMap)bad_evidence_links[bad_evidence_links.GetCount()-1],"unavailable")))&&(!bad_goal&&!bad_topology&&!bad_evidence||rendered.Find("Related record unavailable")>=0)&&rendered.Find("/data/doc/")<0;
				SaveFile(path,original); if(!refresh_visible()) return false; return ok;
			};
			ValueMap missing_refs=first.record; missing_refs.Set("goal_id","t83-missing-goal-marker"); ValueArray missing_checks,missing_artifacts; missing_checks.Add("t83-missing-scope-marker"); missing_artifacts.Add("t83-missing-evidence-marker"); missing_refs.Set("check_refs",missing_checks); missing_refs.Set("artifact_refs",missing_artifacts);
			ValueMap cross_refs=first.record; cross_refs.Set("goal_id","other-workspace:goal:cross-goal-marker"); ValueArray cross_checks,cross_artifacts; cross_checks.Add("other-workspace:topology:cross-scope-marker"); cross_artifacts.Add("other-workspace:cross-evidence-marker"); cross_refs.Set("check_refs",cross_checks); cross_refs.Set("artifact_refs",cross_artifacts);
			bool missing_links_redacted=check_redacted_package_refs(missing_refs,"t83-missing-"); bool cross_links_redacted=check_redacted_package_refs(cross_refs,"cross-");
			ValueMap hidden_refs=first.record; hidden_refs.Set("goal_id","t83-hidden-repo:goal:t83-hidden-goal");
			bool hidden_goal_redacted=check_redacted_package_refs(hidden_refs,"t83-hidden-goal",true,false,false);
			CatalogReadHandle hidden_link_pin=app.PinLatestCatalog(); const DataDocument *linked_topology_doc=hidden_link_pin?hidden_link_pin->data.FindDocument(ws+":"+topology_id):NULL; const DataDocument *linked_evidence_doc=hidden_link_pin?hidden_link_pin->data.FindDocument(ws+":"+evidence_item_id):NULL;
			String topology_bytes=linked_topology_doc?LoadFile(linked_topology_doc->path):String(),evidence_bytes=linked_evidence_doc?LoadFile(linked_evidence_doc->path):String();
			ValueMap hidden_topology_record=linked_topology_doc?linked_topology_doc->data:ValueMap(); hidden_topology_record.Set("source_identity",hidden);
			ValueMap hidden_evidence_record=linked_evidence_doc?linked_evidence_doc->data:ValueMap(); hidden_evidence_record.Set("visibility","restricted");
			bool hidden_topology_written=linked_topology_doc&&linked_evidence_doc&&SaveFile(linked_topology_doc->path,AsJSON(hidden_topology_record,true))&&SaveFile(linked_evidence_doc->path,AsJSON(hidden_evidence_record,true))&&refresh_visible();
			bool hidden_evidence_redacted=hidden_topology_written&&check_redacted_package_refs(first.record,"t83-evidence-item",false,true,true);
			if(linked_topology_doc) SaveFile(linked_topology_doc->path,topology_bytes); if(linked_evidence_doc) SaveFile(linked_evidence_doc->path,evidence_bytes); refresh_visible();
			ValueMap ambiguous_refs=first.record; ambiguous_refs.Set("goal_id","t83-visible-goal");
			String ambiguous_original=LoadFile(ambiguous_goal_path); ValueMap ambiguous_workspace=ambiguous_goal; ambiguous_workspace.Set("workspace_id",ws); ambiguous_workspace.Set("source_identity",source); SaveFile(ambiguous_goal_path,AsJSON(ambiguous_workspace,true)); refresh_visible();
			bool ambiguous_goal_redacted=check_redacted_package_refs(ambiguous_refs,"t83-visible-goal",true,false,false); SaveFile(ambiguous_goal_path,ambiguous_original); refresh_visible();
			String exact_topology_id_path=AppendFileName(root,"data/t83-exact-topology.json"); ValueMap exact_topology=topology_record; exact_topology.Set("id","t83-review-scope"); ValueMap exact_topology_env=topology_env; exact_topology_env.Set("stable_id","t83-review-scope"); exact_topology.Set("envelope",exact_topology_env); SaveFile(exact_topology_id_path,AsJSON(exact_topology,true)); refresh_visible();
			bool ambiguous_topology_redacted=check_redacted_package_refs(first.record,"t83-review-scope",false,true,false); FileDelete(exact_topology_id_path); refresh_visible();
			String evidence_duplicate_path=AppendFileName(root,"data/t83-evidence-item-duplicate.json");
			bool evidence_duplicate_written=SaveFile(evidence_duplicate_path,AsJSON(evidence_item,true))&&refresh_visible();
			bool ambiguous_evidence_redacted=evidence_duplicate_written&&check_redacted_package_refs(first.record,"t83-evidence-item",false,false,true); FileDelete(evidence_duplicate_path); refresh_visible();
			if(!missing_links_redacted||!cross_links_redacted||!hidden_goal_redacted||!hidden_evidence_redacted||!ambiguous_goal_redacted||!ambiguous_topology_redacted||!ambiguous_evidence_redacted) t83_ok=false;
			Vector<String> generic_paths={"/data","/data/k/review-package-revision","/data/k/review-thread","/data/k/review-comment","/data/c/builtin","/api/data/kind/review-package-revision","/api/data/kind/review-thread","/api/data/kind/review-comment"};
			String generic_combined; bool generic_private_404=true;
			for(const String& path:generic_paths) { int gs=0; String response=get(path,gs); generic_combined<<response; if(path.Find("/data/k/")==0||path.Find("/api/data/kind/")==0) generic_private_404&=gs==404; }
			for(const String& id:{first.revision_id,comment_id,thread_id,head_comment_id,head_thread_id}) { int hs=0,as=0; generic_combined<<get("/data/doc/"+UrlEncode(ws+":"+id),hs)<<get("/api/data/doc/"+UrlEncode(ws+":"+id),as); generic_private_404&=hs==404&&as==404; }
			bool generic_clean=generic_private_404&&generic_combined.Find(first.package_id)<0&&generic_combined.Find(first.revision_id)<0&&generic_combined.Find(comment_id)<0&&generic_combined.Find(thread_id)<0&&generic_combined.Find(head_comment_id)<0&&generic_combined.Find(head_thread_id)<0&&generic_combined.Find("T83_PATCH_SECRET_SENTINEL")<0&&generic_combined.Find("T83_PATCH_SECOND_SENTINEL")<0&&generic_combined.Find(stored_comment_body)<0&&generic_combined.Find(head_comment_body)<0&&generic_combined.Find("review-package-revision")<0&&generic_combined.Find("review-thread")<0&&generic_combined.Find("review-comment")<0;
			ValueArray detail_comments=MapGet(detail_json,"comments"),thread_comments=MapGet(thread_json,"comments");
			ValueArray overview_packages=MapGet(fresh_overview_json,"packages"); bool head_count_ok=overview_packages.GetCount()==1&&(int)MapGet((ValueMap)overview_packages[0],"comment_count")==1;
			ValueMap detail_package=MapGet(detail_json,"package"); bool stale_json=detail_comments.GetCount()==1&&(bool)MapGet(detail_package,"stale")&&(bool)MapGet((ValueMap)detail_comments[0],"stale");
			bool dedicated_ok=status==200&&api_status==200&&overview_html.Find(first.package_id)>=0&&overview_html.Find("T83_PATCH_SECRET_SENTINEL")<0&&overview_api.Find(first.package_id)>=0&&overview_api.Find("T83_PATCH_SECRET_SENTINEL")<0&&fresh_overview_status==200&&fresh_overview_api_status==200&&fresh_overview_html.Find(stored_comment_body)<0&&fresh_overview_api.Find(stored_comment_body)<0&&fresh_overview_html.Find(head_comment_body)<0&&fresh_overview_api.Find(head_comment_body)<0&&fresh_overview_html.Find("T83_PATCH_SECOND_SENTINEL")<0&&fresh_overview_api.Find("T83_PATCH_SECOND_SENTINEL")<0&&head_count_ok&&head_comment_status==200&&MapGetString(head_comment_result,"status")=="committed"&&AsJSON(fresh_overview_json,true)==AsJSON(expected_overview,true)&&AsJSON(detail_json,true)==AsJSON(expected_detail,true)&&write_status==200&&MapGetString(write_result,"status")=="committed"&&retry_status==200&&MapGetString(retry_result,"status")=="already_committed"&&rejects_no_write&&second.status=="committed"&&detail_status==200&&detail_api_status==200&&thread_status==200&&thread_api_status==200&&detail_html.Find("This is a stale package revision")>=0&&detail_html.Find("&lt;script&gt;")>=0&&detail_html.Find("<script>alert('t83')</script>")<0&&detail_html.Find("T83_PATCH_SECRET_SENTINEL")>=0&&MapGetString(detail_json,"patch_text").Find("T83_PATCH_SECRET_SENTINEL")>=0&&detail_comments.GetCount()==1&&stale_json&&MapGetString((ValueMap)detail_comments[0],"body")==stored_comment_body&&thread_comments.GetCount()==1&&thread_html.Find("This is a stale package revision")>=0&&t84_actual_links;
			t83_ok=t83_ok&&generic_clean&&dedicated_ok;
			if(!t83_ok) Cerr()<<"FAILED T83/T84 visible review HTTP parity/redaction generic="<<generic_clean<<" dedicated="<<dedicated_ok<<" links="<<t84_actual_links<<" redactions="<<missing_links_redacted<<cross_links_redacted<<hidden_goal_redacted<<hidden_evidence_redacted<<ambiguous_goal_redacted<<ambiguous_topology_redacted<<" goal/topology status="<<linked_goal_status<<"/"<<linked_topology_status<<"\n";
			// The linked check carries a hidden source identity. It makes the package unavailable without exposing any of its identifiers or payload.
			PolicySourceFact package_fact=registry.sources[0]; package_fact.visible=false; registry.sources.Set(0,package_fact);
			if(!app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry)) t83_ok=false;
			CatalogReadHandle before_hidden_write=app.PinLatestCatalog(); ValueMap hidden_write_request=comment_request; hidden_write_request.Set("operation_id","t83-hidden-package-write"); int hidden_write_status=0; ValueMap hidden_write_result=ParseJSON(post(hidden_write_request,base_url,hidden_write_status)); CatalogReadHandle after_hidden_write=app.PinLatestCatalog();
			int hidden_overview_status=0,hidden_detail_status=0; String hidden_overview=get("/api/reviews",hidden_overview_status),hidden_detail=get("/review-package/"+UrlEncode(first.package_id)+"/"+UrlEncode(first.revision_id),hidden_detail_status);
			hidden_package_ok=hidden_write_status==200&&MapGetString(hidden_write_result,"status")!="committed"&&before_hidden_write&&after_hidden_write&&before_hidden_write->generation==after_hidden_write->generation&&hidden_overview_status==200&&hidden_overview.Find(first.package_id)<0&&hidden_overview.Find("T83_PATCH_SECRET_SENTINEL")<0&&hidden_detail_status==404&&hidden_detail.Find(first.package_id)<0&&hidden_detail.Find("T83_PATCH_SECRET_SENTINEL")<0; if(!hidden_package_ok) t83_ok=false;
			// Restore package visibility before testing source-denied checks and comments.
			package_fact.visible=true; registry.sources.Set(0,package_fact); app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
			CatalogReadHandle mutate_pin=app.PinLatestCatalog(); const DataDocument *thread_doc=mutate_pin?mutate_pin->data.FindDocument(ws+":"+thread_id):NULL; const DataDocument *comment_doc=mutate_pin?mutate_pin->data.FindDocument(ws+":"+comment_id):NULL;
			String original_thread=thread_doc?AsJSON(thread_doc->data,true):String();
			if(!thread_doc||!comment_doc) t83_ok=false;
			else {
				const DataDocument *head_doc=mutate_pin->data.FindDocument(ws+":"+second.revision_id); String original_head=head_doc?AsJSON(head_doc->data,true):String();
				ValueMap thread_check=thread_doc->data; thread_check.Set("source_identity","t83-denied-check-source");
				ValueMap head_record=head_doc?head_doc->data:ValueMap(); ValueArray denied_checks; denied_checks.Add(thread_id); head_record.Set("check_refs",denied_checks);
				if(!head_doc||!SaveFile(thread_doc->path,AsJSON(thread_check,true))||!SaveFile(head_doc->path,AsJSON(head_record,true))||!refresh_visible()) t83_ok=false;
				int denied_check_status=0; String denied_check=get("/review-package/"+UrlEncode(second.package_id)+"/"+UrlEncode(second.revision_id),denied_check_status);
				int denied_overview_status=0; String denied_overview=get("/api/reviews",denied_overview_status);
				CatalogReadHandle before_denied_check_write=app.PinLatestCatalog(); ValueMap denied_check_write=comment_request; denied_check_write.Set("revision_id",second.revision_id); denied_check_write.Set("operation_id","t83-denied-check-write"); int denied_check_write_status=0; ValueMap denied_check_write_result=ParseJSON(post(denied_check_write,base_url,denied_check_write_status)); CatalogReadHandle after_denied_check_write=app.PinLatestCatalog();
				denied_check_ok=denied_check_status==404&&denied_check.Find(second.package_id)<0&&denied_check.Find("T83_PATCH_SECRET_SENTINEL")<0&&denied_overview.Find(second.package_id)<0&&denied_check_write_status==200&&MapGetString(denied_check_write_result,"status")!="committed"&&before_denied_check_write&&after_denied_check_write&&before_denied_check_write->generation==after_denied_check_write->generation; if(!denied_check_ok) t83_ok=false;
				if(head_doc) SaveFile(head_doc->path,original_head);
				if(!SaveFile(thread_doc->path,original_thread)||!refresh_visible()) t83_ok=false;
				ValueMap cross_head_record=head_doc?head_doc->data:ValueMap(); ValueArray cross_checks; cross_checks.Add("other-workspace:private-check"); cross_head_record.Set("check_refs",cross_checks);
				if(!head_doc||!SaveFile(head_doc->path,AsJSON(cross_head_record,true))||!refresh_visible()) t83_ok=false;
				int cross_check_status=0; String cross_check=get("/review-package/"+UrlEncode(second.package_id)+"/"+UrlEncode(second.revision_id),cross_check_status);
				cross_check_ok=cross_check_status==404&&cross_check.Find(second.package_id)<0&&cross_check.Find("T83_PATCH_SECOND_SENTINEL")<0; if(!cross_check_ok) t83_ok=false;
				if(head_doc) SaveFile(head_doc->path,original_head);
				if(!refresh_visible()) t83_ok=false;
				// A valid comment with a policy-hidden source identity is omitted from detail and counts.
			CatalogReadHandle source_pin=app.PinLatestCatalog(); const DataDocument *source_comment=source_pin?source_pin->data.FindDocument(ws+":"+head_comment_id):NULL; String source_comment_bytes=source_comment?AsJSON(source_comment->data,true):String();
				ValueMap hidden_comment_record=source_comment?source_comment->data:ValueMap(); hidden_comment_record.Set("source_identity","t83-denied-comment-source");
				if(!source_comment||!SaveFile(source_comment->path,AsJSON(hidden_comment_record,true))||!refresh_visible()) t83_ok=false;
				int hidden_comment_api_status=0; String hidden_comment_api=get("/api/reviews/package/"+UrlEncode(second.package_id)+"/"+UrlEncode(second.revision_id),hidden_comment_api_status);
				ValueMap hidden_comment_view=ParseJSON(hidden_comment_api); ValueArray hidden_comments=MapGet(hidden_comment_view,"comments");
				int hidden_comment_overview_status=0; String hidden_comment_overview=get("/api/reviews",hidden_comment_overview_status); ValueMap hidden_comment_overview_json=ParseJSON(hidden_comment_overview); ValueArray hidden_packages=MapGet(hidden_comment_overview_json,"packages");
				hidden_comment_ok=hidden_comment_api_status==200&&hidden_comments.IsEmpty()&&hidden_comment_api.Find(head_comment_body)<0&&hidden_comment_overview_status==200&&hidden_packages.GetCount()==1&&(int)MapGet((ValueMap)hidden_packages[0],"comment_count")==0&&hidden_comment_overview.Find(head_comment_body)<0; if(!hidden_comment_ok) t83_ok=false;
				if(source_comment) SaveFile(source_comment->path,source_comment_bytes);
				if(!refresh_visible()) t83_ok=false;
				CatalogReadHandle cross_pin=app.PinLatestCatalog(); const DataDocument *cross_comment=cross_pin?cross_pin->data.FindDocument(ws+":"+head_comment_id):NULL; String cross_comment_bytes=cross_comment?AsJSON(cross_comment->data,true):String();
				ValueMap cross_comment_record=cross_comment?cross_comment->data:ValueMap(); ValueArray cross_comment_refs; cross_comment_refs.Add("other-workspace:private-check"); cross_comment_record.Set("check_refs",cross_comment_refs);
				if(!cross_comment||!SaveFile(cross_comment->path,AsJSON(cross_comment_record,true))||!refresh_visible()) t83_ok=false;
				int cross_comment_status=0; String cross_comment_api=get("/api/reviews/package/"+UrlEncode(second.package_id)+"/"+UrlEncode(second.revision_id),cross_comment_status); ValueMap cross_comment_view=ParseJSON(cross_comment_api); ValueArray cross_comments=MapGet(cross_comment_view,"comments");
				cross_comment_ok=cross_comment_status==200&&cross_comments.IsEmpty()&&cross_comment_api.Find(head_comment_body)<0; if(!cross_comment_ok) { CatalogReadHandle cross_after=app.PinLatestCatalog(); const DataDocument *test_comment=cross_after?cross_after->data.FindDocument(ws+":"+head_comment_id):NULL; Cerr()<<"T83 cross-comment status="<<cross_comment_status<<" comments="<<cross_comments.GetCount()<<" body="<<cross_comment_api.Find(head_comment_body)<<" identity="<<(test_comment?MapGetString(test_comment->data,"source_identity"):String("missing"))<<"\n"; t83_ok=false; }
				if(cross_comment) SaveFile(cross_comment->path,cross_comment_bytes);
				if(!refresh_visible()) t83_ok=false;
				ValueMap thread_record=thread_doc->data,thread_env=MapGet(thread_record,"envelope"); thread_env.Set("visibility","public"); thread_record.Set("envelope",thread_env);
				if(!SaveFile(thread_doc->path,AsJSON(thread_record,true))||!refresh_visible()) t83_ok=false;
				int invalid_thread_status=0; String invalid_thread=get("/api/reviews/thread/"+UrlEncode(thread_id),invalid_thread_status);
				int hidden_comment_status=0; String hidden_thread=get("/review-package/"+UrlEncode(first.package_id)+"/"+UrlEncode(first.revision_id),hidden_comment_status);
				invalid_thread_ok=invalid_thread_status==404&&invalid_thread.Find(stored_comment_body)<0&&hidden_thread.Find(stored_comment_body)<0; if(!invalid_thread_ok) t83_ok=false;
				if(!SaveFile(thread_doc->path,original_thread)||!refresh_visible()) t83_ok=false;
			}
			int missing_status=0; String missing_thread=get("/api/reviews/thread/nonexistent-t83-thread",missing_status);
			if(missing_status!=404||missing_thread.Find("nonexistent-t83-thread")>=0) t83_ok=false;
			int no_edit_status=0,no_delete_status=0,no_decision_status=0;
			get("/api/reviews/comment/"+UrlEncode(comment_id)+"/edit",no_edit_status); get("/api/reviews/comment/"+UrlEncode(comment_id)+"/delete",no_delete_status); get("/api/reviews/decision",no_decision_status);
			if(no_edit_status!=404||no_delete_status!=404||no_decision_status!=404) t83_ok=false;
			// An incomplete fixture is unavailable; restoring root/catalog is unconditional below.
			String incomplete_root=AppendFileName(root,"incomplete-fixture");
			RealizeDirectory(AppendFileName(incomplete_root,"v2")); RealizeDirectory(AppendFileName(incomplete_root,"data"));
			SaveFile(AppendFileName(incomplete_root,"data/bad.json"),"{malformed synthetic catalog record");
			app.test_fixture_root=incomplete_root; bool incomplete_loaded=app.RefreshCatalog(); if(incomplete_loaded) app.InstallSyntheticPolicyContextForTests(instance,no_overrides,no_overrides,registry);
			int incomplete_status=0; String incomplete=get("/api/reviews",incomplete_status);
			CatalogReadHandle incomplete_pin=app.PinLatestCatalog();
			incomplete_ok=incomplete_loaded&&incomplete_pin&&incomplete_pin->data_status!="loaded"&&incomplete_status==200&&incomplete.Find(first.package_id)<0&&incomplete.Find("T83_PATCH_SECRET_SENTINEL")<0; if(!incomplete_ok) t83_ok=false;
		}
		app.instance_policy=prior_instance; app.policy_workspace_settings=prior_workspace_policy; app.policy_operator_settings=prior_operator;
		app.trusted_policy_registry=pick(prior_registry); app.test_registration=prior; app.test_registration_change=prior_change;
		app.test_registration_provider=prior_provider; app.test_registration_change_point=prior_change_point; app.test_registration_scanned_root=prior_scanned; app.test_fixture_root=prior_root;
		bool restored=app.RefreshCatalog();
		if(!t83_ok||!restored) { Cerr()<<"FAILED T83 authenticated HTTP review route, private redaction, check/thread/comment policy matrix or fixture restoration stages="<<hidden_package_ok<<denied_check_ok<<hidden_comment_ok<<cross_comment_ok<<cross_check_ok<<invalid_thread_ok<<incomplete_ok<<" restored="<<restored<<"\n"; return false; }
		Cout()<<"ok    T83 HTTP generic redaction, policy-denied check/source, authenticated package/thread parity, stale escaping, append retry/denials, invalid thread, and restoration\n";
		Cout()<<"ok    T84 populated HTTP/headless goal, topology-scope and evidence links; missing, cross-workspace, hidden and ambiguous refs use generic redaction\n";
	}
	{
		HttpRequest goals_req(base_url + "/api/goals");
		goals_req.Timeout(3000);
		String api_goals = goals_req.Execute();
		HttpRequest goal_page(base_url + "/goal/alpha:goal:shared-goal");
		goal_page.Timeout(3000);
		String html_goal = goal_page.Execute();
		const V2Goal* unique_goal = app.PinLatestCatalog()->FindGoal("alpha:goal:unique-goal");
		CatalogRecordRead cli_unique_goal = ReadCatalogRecord(*app.PinLatestCatalog(), "unique-goal", true);
		HttpRequest unique_goal_alias_page(base_url + "/goal/unique-goal");
		unique_goal_alias_page.Timeout(3000);
		String unique_goal_alias_html = unique_goal_alias_page.Execute();
		HttpRequest ambiguous_goal_page(base_url + "/goal/shared-goal");
		ambiguous_goal_page.Timeout(3000);
		ambiguous_goal_page.Execute();
		HttpRequest issues_req(base_url + "/api/issues");
		issues_req.Timeout(3000);
		String api_issues = issues_req.Execute();
		HttpRequest releases_req(base_url + "/api/releases");
		releases_req.Timeout(3000);
		String api_releases = releases_req.Execute();
		auto catalog = app.PinLatestCatalog();
		const V2Goal* synthetic_goal = catalog->FindGoal("alpha:goal:shared-goal");
		CatalogRecordRead cli_goal = synthetic_goal ? ReadCatalogRecord(*catalog, synthetic_goal->qualified_id) : CatalogRecordRead();
		ValueMap goals_json = ParseJSON(api_goals);
		ValueArray goal_rows = MapGet(goals_json, "goals");
		ValueMap api_goal_row;
		bool api_has_unique_goal = false;
		for(const Value& row : goal_rows) {
			ValueMap candidate = row;
			if(MapGetString(candidate, "qualified_id") == "alpha:goal:shared-goal") api_goal_row = candidate;
			if(MapGetString(candidate, "qualified_id") == "alpha:goal:unique-goal") api_has_unique_goal = true;
		}
		ValueMap cli_goal_record = MapGet(cli_goal.response, "record");
		bool goal_parity = cli_goal.status == "ok" && !api_goal_row.IsEmpty() &&
		                   is_sha256(synthetic_goal->source_revision) && is_sha256(catalog->source_revision) &&
		                   cli_goal_record.GetCount() == synthetic_goal->source_data.GetCount() &&
		                   MapGetString(cli_goal.response, "qualified_id") == synthetic_goal->qualified_id &&
		                   MapGetString(cli_goal.response, "record_revision") == synthetic_goal->source_revision &&
		                   MapGetString(cli_goal.response, "source_revision") == synthetic_goal->source_revision &&
		                   MapGetString(cli_goal.response, "validation_state") == "typed" &&
		                   (int64)MapGet(cli_goal.response, "catalog_generation") == catalog->generation;
		for(int i = 0; goal_parity && i < synthetic_goal->source_data.GetCount(); i++) {
			String key = synthetic_goal->source_data.GetKey(i);
			goal_parity = AsJSON(MapGet(cli_goal_record, key)) == AsJSON(MapGet(api_goal_row, key));
		}
		const DataDocument* synthetic_doc = catalog->data.FindDocument("paper-001");
		CatalogRecordRead cli_doc = synthetic_doc ? ReadCatalogRecord(*catalog, synthetic_doc->qualified_id) : CatalogRecordRead();
		HttpRequest api_doc_req(base_url + "/api/data/doc/paper-001");
		api_doc_req.Timeout(3000);
		ValueMap api_doc = ParseJSON(api_doc_req.Execute());
		HttpRequest html_doc_alias_req(base_url + "/data/doc/paper-001");
		html_doc_alias_req.Timeout(3000);
		String html_doc_alias = html_doc_alias_req.Execute();
		HttpRequest html_doc_qualified_req(base_url + "/data/doc/" + synthetic_doc->qualified_id);
		html_doc_qualified_req.Timeout(3000);
		String html_doc_qualified = html_doc_qualified_req.Execute();
		HttpRequest html_doc_ambiguous_req(base_url + "/data/doc/shared-goal");
		html_doc_ambiguous_req.Timeout(3000);
		html_doc_ambiguous_req.Execute();
		HttpRequest api_doc_ambiguous_req(base_url + "/api/data/doc/shared-goal");
		api_doc_ambiguous_req.Timeout(3000);
		api_doc_ambiguous_req.Execute();
		bool doc_parity = synthetic_doc && cli_doc.status == "ok" && api_doc_req.GetStatusCode() == 200 &&
		                 synthetic_doc->source_revision == DataStore::RecordSourceRevision(synthetic_doc->rel_path, LoadFile(synthetic_doc->path)) &&
		                 AsJSON(MapGet(cli_doc.response, "record")) == AsJSON(MapGet(api_doc, "data")) &&
		                 MapGetString(cli_doc.response, "qualified_id") == synthetic_doc->qualified_id &&
		                 MapGetString(cli_doc.response, "record_revision") == synthetic_doc->revision &&
		                 MapGetString(cli_doc.response, "source_revision") == synthetic_doc->source_revision &&
		                 MapGetString(cli_doc.response, "validation_state") == synthetic_doc->validation_state &&
		                 (bool)MapGet(cli_doc.response, "valid") == synthetic_doc->valid &&
		                 (bool)MapGet(cli_doc.response, "validated") == synthetic_doc->validated &&
		                 (int64)MapGet(cli_doc.response, "catalog_generation") == catalog->generation;
		CatalogRecordRead cli_issue_alias = ReadCatalogRecord(*catalog, "issue-a", true);
		ValueMap issues_json = ParseJSON(api_issues);
		ValueArray issue_rows = MapGet(issues_json, "issues");
		ValueMap api_issue_row;
		for(const Value& row : issue_rows) {
			ValueMap candidate = row;
			if(MapGetString(candidate, "qualified_id") == "alpha:issue:issue-a") { api_issue_row = candidate; break; }
		}
		const V2Issue* synthetic_issue = catalog->FindIssue("alpha:issue:issue-a");
		ValueMap cli_issue_record = MapGet(cli_issue_alias.response, "record");
		bool alias_parity = synthetic_issue && cli_issue_alias.status == "ok" && !api_issue_row.IsEmpty() &&
		                    MapGetString(cli_issue_alias.response, "qualified_id") == "alpha:issue:issue-a" &&
		                    cli_issue_record.GetCount() == synthetic_issue->source_data.GetCount();
		for(int i = 0; alias_parity && i < synthetic_issue->source_data.GetCount(); i++) {
			String key = synthetic_issue->source_data.GetKey(i);
			alias_parity = AsJSON(MapGet(cli_issue_record, key)) == AsJSON(MapGet(api_issue_row, key));
		}
		CatalogRecordRead cli_doc_alias = ReadCatalogRecord(*catalog, "paper-001", true);
		bool api_alias_parity = cli_doc_alias.status == "ok" &&
		                        MapGetString(cli_doc_alias.response, "qualified_id") == MapGetString(api_doc, "qualified_id") &&
		                        AsJSON(MapGet(cli_doc_alias.response, "record")) == AsJSON(MapGet(api_doc, "data"));
		bool goal_route_parity = unique_goal && cli_unique_goal.status == "ok" &&
		                         MapGetString(cli_unique_goal.response, "qualified_id") == unique_goal->qualified_id &&
		                         api_has_unique_goal && goal_page.GetStatusCode() == 200 && html_goal.Find("alpha:goal:shared-goal") >= 0 &&
		                         unique_goal_alias_page.GetStatusCode() == 200 && unique_goal_alias_html.Find(unique_goal->qualified_id) >= 0 &&
		                         ambiguous_goal_page.GetStatusCode() == 409;
		bool doc_route_parity = synthetic_doc && html_doc_alias_req.GetStatusCode() == 200 &&
		                        html_doc_qualified_req.GetStatusCode() == 200 &&
		                        html_doc_alias.Find(synthetic_doc->qualified_id) >= 0 &&
		                        html_doc_qualified.Find(synthetic_doc->qualified_id) >= 0 &&
		                        api_doc_ambiguous_req.GetStatusCode() == 409 && html_doc_ambiguous_req.GetStatusCode() == 409;
		CatalogRecordRead ambiguous_alias = ReadCatalogRecord(*catalog, "shared-goal", true);
		bool ambiguous_status = ambiguous_alias.status == "ambiguous";
		bool missing_status = ReadCatalogRecord(*catalog, "missing:goal:no-record").status == "missing";
		bool unqualified_status = ReadCatalogRecord(*catalog, "paper-001").status == "unqualified";
		CatalogRecordRead ok_read = ReadCatalogRecord(*catalog, synthetic_goal ? synthetic_goal->qualified_id : String());
		CatalogRecordRead ambiguous_read = ReadCatalogRecord(*catalog, "shared-goal", true);
		CatalogRecordRead missing_read = ReadCatalogRecord(*catalog, "missing:goal:no-record");
		CatalogRecordRead unqualified_read = ReadCatalogRecord(*catalog, "paper-001");
		if(goals_req.GetStatusCode() != 200 || api_goals.Find("state_snapshots") < 0 ||
		   api_goals.Find("file_refs") < 0 || api_goals.Find("loaded_with_errors") < 0 ||
		   goal_page.GetStatusCode() != 200 || html_goal.Find("state_snapshots") < 0 ||
		   html_goal.Find("G2") < 0 || issues_req.GetStatusCode() != 200 || api_issues.Find("keep this note") < 0 ||
		   releases_req.GetStatusCode() != 200 || api_releases.Find("release-doc") < 0 || api_releases.Find("alpha:issue:issue-a") < 0 ||
		   !goal_parity || !doc_parity || !alias_parity || !api_alias_parity || !goal_route_parity || !doc_route_parity ||
		   !ambiguous_status || !missing_status || !unqualified_status ||
		   CatalogRecordExitCode(ok_read) != 0 || CatalogRecordExitCode(ambiguous_read) != 5 ||
		   CatalogRecordExitCode(missing_read) != 4 || CatalogRecordExitCode(unqualified_read) != 2 ||
		   CatalogRecordExitCode(cli_issue_alias) != 0 || CatalogRecordExitCode(ambiguous_alias) != 5 ||
		   CatalogRecordExitCode(cli_doc_alias) != 0) {
			Cerr() << "FAILED: typed HTML/API adapters diverged or dropped source fields/status\n";
			return false;
		}
		Cout() << "ok    CLI/API/HTML detail routes share canonical alias, qualified, and ambiguous resolution\n";
		Cout() << "ok    typed HTML/API parity preserves goal snapshots/gates, issue notes, release links, and source errors\n";
	}
	{
		auto before = app.PinLatestCatalog();
		HttpRequest refresh(base_url + "/api/data/refresh");
		refresh.POST();
		refresh.Header("Origin", base_url);
		refresh.ContentType("application/json");
		refresh.PostData("{}");
		refresh.Timeout(3000);
		String body = refresh.Execute();
		auto after = app.PinLatestCatalog();
		if(refresh.GetStatusCode() != 200 || after->generation <= before->generation || body.Find("catalog_generation") < 0 ||
		   body.Find("/home/") >= 0 || body.Find(workspace.root) >= 0 ||
		   body.Find("issues/alpha/bad.json") < 0 || body.Find("loaded_with_errors") < 0) {
			Cerr() << "FAILED: explicit authorized refresh did not publish a safe, observable generation\n";
			return false;
		}
		Cout() << "ok    explicit refresh publishes one observable immutable generation without absolute metadata\n";
	}
	{
		String valid_root = app.test_fixture_root;
		String failed_root = workspace.FixtureRoot("partial-failing-fixture");
		String partial_v2_root = AppendFileName(failed_root, "v2");
		RealizeDirectory(AppendFileName(partial_v2_root, "goals/partial"));
		SaveFile(AppendFileName(partial_v2_root, "goals/partial/goal.json"),
			"{\"id\":\"partial-only\",\"kind\":\"goal\",\"repo\":\"partial\",\"title\":\"Must be discarded\",\"tags\":[],\"state\":{},\"tasks\":[]}");
		app.test_fixture_root = failed_root;
		HttpRequest failed_refresh(base_url + "/api/data/refresh");
		failed_refresh.POST();
		failed_refresh.Header("Origin", base_url);
		failed_refresh.ContentType("application/json");
		failed_refresh.PostData("{}");
		failed_refresh.Timeout(3000);
		failed_refresh.Execute();
		auto failed = app.PinLatestCatalog();
		CatalogRecordRead unavailable_read = ReadCatalogRecord(*failed, "partial:goal:partial-only");
		bool has_v2_discard_diagnostic = false, has_data_failure_diagnostic = false;
		for(const String& diagnostic : failed->diagnostics) {
			has_v2_discard_diagnostic |= diagnostic == "v2:records_discarded_due_to_catalog_load_error";
			has_data_failure_diagnostic |= diagnostic == "data:source_load_failed";
		}
		HttpRequest failed_goals(base_url + "/api/goals");
		failed_goals.Timeout(3000);
		failed_goals.Execute();
		String goals_error = failed_goals.GetContent();
		HttpRequest failed_data(base_url + "/api/data/spec");
		failed_data.Timeout(3000);
		failed_data.Execute();
		String data_error = failed_data.GetContent();
		HttpRequest failed_ideas(base_url + "/api/tasks/ideas");
		failed_ideas.Timeout(3000);
		failed_ideas.Execute();
		String ideas_error = failed_ideas.GetContent();
		app.test_fixture_root = valid_root;
		HttpRequest restored_refresh(base_url + "/api/data/refresh");
		restored_refresh.POST();
		restored_refresh.Header("Origin", base_url);
		restored_refresh.ContentType("application/json");
		restored_refresh.PostData("{}");
		restored_refresh.Timeout(3000);
		restored_refresh.Execute();
		auto recovered = app.PinLatestCatalog();
		if(failed_refresh.GetStatusCode() != 503 || failed->status != "load_error" ||
		   failed->data_status != "load_error" || failed->v2_status != "load_error" ||
		   unavailable_read.status != "unavailable" || CatalogRecordExitCode(unavailable_read) != 3 ||
		   !failed->v2.GetGoals().IsEmpty() || !failed->data.GetDocuments().IsEmpty() ||
		   !has_v2_discard_diagnostic || !has_data_failure_diagnostic ||
		   failed_goals.GetStatusCode() != 503 || failed_data.GetStatusCode() != 503 || failed_ideas.GetStatusCode() != 503 ||
		   goals_error.Find("load_error") < 0 || goals_error.Find("records_discarded_due_to_catalog_load_error") < 0 ||
		   data_error.Find("load_error") < 0 || data_error.Find("source_load_failed") < 0 ||
		   ideas_error.Find("load_error") < 0 || goals_error.Find(workspace.root) >= 0 ||
		   data_error.Find(workspace.root) >= 0 || ideas_error.Find(workspace.root) >= 0 ||
		   restored_refresh.GetStatusCode() != 200 || recovered->status == "load_error" ||
		   recovered->v2_status == "load_error" || recovered->data_status == "load_error" ||
		   recovered->v2.GetGoals().IsEmpty() || recovered->data.GetDocuments().IsEmpty()) {
			Cerr() << "FAILED: partial-source failure was treated as empty or stale data was retained; failed=" << failed_refresh.GetStatusCode()
			       << ", api=" << failed_goals.GetStatusCode() << "/" << failed_data.GetStatusCode() << "/" << failed_ideas.GetStatusCode()
			       << ", failure_statuses=" << failed->v2_status << "/" << failed->data_status
			       << ", restored=" << restored_refresh.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    failed refresh is distinct from valid empty and publishes no stale records\n";
	}

	// Test GET /api/projects
	{
		HttpRequest req(base_url + "/api/projects");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || !res.Find("manager-skylark")) {
			Cerr() << "FAILED: /api/projects returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    /api/projects (200 OK)\n";
	}

	// Test GET /api/tasks/next/manager-skylark
	{
		HttpRequest req(base_url + "/api/tasks/next/manager-skylark");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200) {
			Cerr() << "FAILED: /api/tasks/next returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    /api/tasks/next/manager-skylark (200 OK)\n";
	}

	// Test GET /
	{
		HttpRequest req(base_url + "/");
		req.Timeout(3000);
		String res = req.Execute();
		String home = GetHomeDirectory();
		if(req.GetStatusCode() != 200 || res.Find("Manager Skylark") < 0 || (!home.IsEmpty() && res.Find(home) >= 0)) {
			Cerr() << "FAILED: GET / returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    GET / Dashboard (200 OK)\n";
	}
	{
		HttpRequest req(base_url + "/project/manager-skylark");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("<script>alert(7)</script>") >= 0 ||
		   res.Find("&lt;script&gt;alert(7)&lt;/script&gt;") < 0) {
			Cerr() << "FAILED: workspace title did not remain escaped in its detail view\n";
			return false;
		}
		Cout() << "ok    project title is escaped in the workspace detail view\n";
	}

	// Data specifications expose relative counts/schema metadata, never the
	// configured absolute root or local home path.
	{
		HttpRequest req(base_url + "/api/policy/effective");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.PostData("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"public\"],\"provider_id\":\"provider\",\"action\":\"publication\"}");
		req.Timeout(3000);
		String body = req.Execute();
	ValueMap policy = ParseJSON(body);
	if(req.GetStatusCode() != 200 || policy.GetCount() != 10 ||
	   MapGetString(policy, "status") != "unconfigured" ||
	   MapGetString(policy, "review_granularity") != "unconfirmed" ||
	   (bool)MapGet(policy, "publication_allowed") || (bool)MapGet(policy, "external_model_allowed") ||
	   body.Find(workspace.root) >= 0 || body.Find(GetHomeDirectory()) >= 0 ||
		   body.Find("secret_reference") >= 0 || body.Find("provider_id") >= 0) {
			Cerr() << "FAILED: effective-policy response was not fail-closed and redacted (status=" << req.GetStatusCode()
			       << ", fields=" << policy.GetCount() << ", body=" << body << ")\n";
			return false;
		}
		Cout() << "ok    authenticated effective-policy API is unconfigured and redacted\n";
	}
	{
		PolicyIdentity workspace_id; workspace_id.workspace_id = "synthetic"; workspace_id.object_id = "synthetic";
		auto identity = [](const String& id) { PolicyIdentity x; x.workspace_id = "synthetic"; x.object_id = id; return x; };
		TrustedPolicyRegistry registry; registry.configured = true;
		PolicyWorkspaceFact workspace_fact; workspace_fact.identity = workspace_id; registry.workspaces.Add(workspace_fact);
		auto add_source = [&](const String& id, PolicyClassification classification, bool visible) {
			PolicySourceFact source; source.workspace = workspace_id; source.identity = identity(id);
			source.classification = classification; source.visible = visible; registry.sources.Add(source);
		};
		add_source("public", POLICY_PUBLIC, true);
		add_source("hidden", POLICY_PUBLIC, false);
		add_source("protected", POLICY_PROTECTED, true);
		add_source("derived", POLICY_PUBLIC, true);
		PolicyAncestryFact ancestry; ancestry.child = identity("derived"); ancestry.parent = identity("protected"); registry.ancestry.Add(ancestry);
		PolicyIdentity protected_identity = identity("protected"); registry.protected_ancestry.Add(protected_identity);
		PolicyProviderFact provider; provider.workspace = workspace_id; provider.identity = identity("provider"); provider.training_policy = "eligible"; provider.enabled=true; provider.config_revision="synthetic-r1"; provider.secret_reference="secretref:synthetic-api"; registry.providers.Add(provider);
		PolicySettings instance; instance.configured = true; instance.remote_enabled = true; instance.export_enabled = true;
		instance.secret_reference = "synthetic-secret-marker"; instance.review_granularity = "goal";
		PolicySettings workspace_settings; workspace_settings.configured = true; workspace_settings.remote_enabled = false; workspace_settings.export_enabled = true;
		PolicySettings operator_settings;
		if(!app.InstallSyntheticPolicyContextForTests(instance, workspace_settings, operator_settings, registry)) return false;
		UserSession session = SystemAuthService::CreateTestSessionForTests();
		int policy_http_status = 0;
		auto policy_request = [&](const String& payload) {
			HttpRequest req(base_url + "/api/policy/effective"); req.POST();
			req.Header("Origin", base_url);
			req.Header("Cookie", "wf_auth_session=" + UrlEncode(session.token));
			req.ContentType("application/json"); req.PostData(payload); req.Timeout(3000);
			String body = req.Execute(); policy_http_status = req.GetStatusCode(); return ValueMap(ParseJSON(body));
		};
		ValueMap public_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"public\"],\"provider_id\":\"provider\",\"action\":\"publication\"}");
		int public_http_status = policy_http_status;
		ValueMap public_model_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"public\"],\"provider_id\":\"provider\",\"action\":\"model\"}");
		ValueMap hidden_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"hidden\"],\"provider_id\":\"provider\",\"action\":\"publication\"}");
		ValueMap protected_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"derived\"],\"provider_id\":\"provider\",\"action\":\"model\"}");
		ValueMap mixed_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"public\",\"protected\"],\"provider_id\":\"provider\",\"action\":\"model\"}");
		ValueMap unknown_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"unknown\"],\"provider_id\":\"provider\",\"action\":\"model\"}");
		ValueMap forged_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"public\"],\"provider_id\":\"provider\",\"action\":\"publication\",\"classification\":\"public\"}");
		int forged_http_status = policy_http_status;
		PolicyInput headless; headless.workspace = workspace_id; headless.sources.Add(identity("public")); headless.provider = identity("provider"); headless.action = POLICY_ACTION_MODEL;
		EffectivePolicy expected = app.ResolveEffectivePolicy(headless);
		if(public_http_status != 200 || forged_http_status != 400 || MapGetString(public_result, "status") != "evaluated" || !(bool)MapGet(public_result, "publication_allowed") ||
		   (bool)MapGet(public_model_result, "external_model_allowed") != (bool)expected.external_model_allowed ||
		   (bool)MapGet(hidden_result, "publication_allowed") || (bool)MapGet(protected_result, "external_model_allowed") ||
		   (bool)MapGet(mixed_result, "external_model_allowed") || MapGetString(mixed_result, "classification") != "protected" ||
		   MapGetString(unknown_result, "reason") != "unknown_source_identity" || forged_result.GetCount() != 0 ||
		   (bool)expected.external_model_allowed || MapGetString(protected_result, "classification") != "protected" ||
		   AsJSON(public_result).Find("synthetic-secret-marker") >= 0 ||
		   AsJSON(public_result).Find("secretref:synthetic-api") >= 0 || AsJSON(public_result).Find("secret_reference") >= 0) {
			app.ResetSyntheticPolicyContextForTests();
			Cerr() << "FAILED: synthetic effective-policy API diverged (http=" << policy_http_status
			       << ", forged_http=" << forged_http_status << ", public=" << AsJSON(public_result)
			       << ", hidden=" << AsJSON(hidden_result) << ", protected=" << AsJSON(protected_result)
			       << ", unknown=" << AsJSON(unknown_result) << ", forged=" << AsJSON(forged_result) << ")\n";
			return false;
		}
		app.ResetSyntheticPolicyContextForTests();
		ValueMap reset_result = policy_request("{\"workspace_id\":\"synthetic\",\"source_ids\":[\"public\"],\"provider_id\":\"provider\",\"action\":\"publication\"}");
		if(MapGetString(reset_result, "status") != "unconfigured") return false;
		Cout() << "ok    authenticated synthetic policy API resolves trusted facts, restrictions, unknowns, redaction, and reset\n";
	}
	{
		HttpRequest req(base_url + "/api/data/spec");
		req.Timeout(3000);
		String res = req.Execute();
		String home = GetHomeDirectory();
		if(req.GetStatusCode() != 200 || res.Find("\"root\"") >= 0 || res.Find(workspace.root) >= 0 ||
		   res.Find("issues/alpha/bad.json") < 0 || (!home.IsEmpty() && res.Find(home) >= 0)) {
			Cerr() << "FAILED: GET /api/data/spec leaked root path or returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    GET /api/data/spec (no absolute root disclosure)\n";
	}

	// Test GET /chat (embedded rendering is disabled until stored content is sanitized)
	{
		HttpRequest req(base_url + "/chat");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Embedded chat rendering is unavailable") < 0) {
			Cerr() << "FAILED: GET /chat returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    GET /chat (sanitized-unavailable notice, 200 OK)\n";
	}

	// Test GET /webchat (WebChat Standalone View)
	{
		HttpRequest req(base_url + "/webchat");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("chat-app") < 0) {
			Cerr() << "FAILED: GET /webchat returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    GET /webchat Standalone WebChat (200 OK)\n";
	}

	// Test GET /api/framework/spec (WebFramework MegaMenu semantic spec)
	{
		HttpRequest req(base_url + "/api/framework/spec");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("WebFramework") < 0) {
			Cerr() << "FAILED: GET /api/framework/spec returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    GET /api/framework/spec Framework Spec (200 OK)\n";
	}

	// Test GET /api/chat/sessions
	String test_session_id;
	{
		HttpRequest req(base_url + "/api/chat/sessions");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("\"sessions\"") < 0) {
			Cerr() << "FAILED: GET /api/chat/sessions returned " << req.GetStatusCode() << "\n";
			return false;
		}
		Value v = ParseJSON(res);
		if(v["sessions"].GetCount() > 0)
			test_session_id = v["sessions"][0]["id"];
		Cout() << "ok    GET /api/chat/sessions (200 OK, session=" << test_session_id << ")\n";
	}

	if(test_session_id.IsEmpty())
		test_session_id = "default";

	// Test POST /api/chat/message
	{
		ValueMap msg;
		msg.Add("session_id", test_session_id);
		msg.Add("content", "Synthetic <script>alert(6)</script> chat input");
		msg.Add("role", "user");

		HttpRequest req(base_url + "/api/chat/message");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.PostData(AsJSON(msg, true));
		req.Timeout(3000);
		String res = req.Execute();
		Value resp = ParseJSON(res);
		if(req.GetStatusCode() != 200 || !resp["success"]) {
			Cerr() << "FAILED: POST /api/chat/message returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    POST /api/chat/message (200 OK)\n";
	}
	{
		HttpRequest req(base_url + "/webchat?session=" + UrlEncode(test_session_id));
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("<script>alert(6)</script>") >= 0 ||
		   res.Find("&lt;script&gt;alert(6)&lt;/script&gt;") < 0 ||
		   res.Find("function setMessageText(element, text, streaming)") < 0 ||
		   res.Find("element.textContent = text") < 0 || res.Find("innerHTML") >= 0) {
			Cerr() << "FAILED: WebChat did not display stored hostile text as escaped plain text\n";
			return false;
		}
		Cout() << "ok    WebChat uses textContent for hostile stored messages and stream deltas\n";
	}

	// Test GET /api/chat/stream/{session_id} (SSE stream)
	{
		HttpRequest req(base_url + "/api/chat/stream/" + test_session_id + "?prompt=Hello");
		req.Timeout(5000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("data:") < 0) {
			Cerr() << "FAILED: GET /api/chat/stream returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /api/chat/stream (200 OK SSE streaming)\n";
	}

	// Test POST /api/tasks/evaluate-paper truthfully reports the not-yet-built pipeline.
	{
		HttpRequest req(base_url + "/api/tasks/evaluate-paper");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 501) {
			Cerr() << "FAILED: POST /api/tasks/evaluate-paper returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    POST /api/tasks/evaluate-paper (501, analysis unavailable)\n";
	}

	// Test POST /api/data/schema/generate truthfully reports the unimplemented inference engine.
	{
		HttpRequest req(base_url + "/api/data/schema/generate");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 501) {
			Cerr() << "FAILED: POST /api/data/schema/generate returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    POST /api/data/schema/generate (501, inference unavailable)\n";
	}

	// External model dispatch must remain disabled until scoped provider policy
	// explicitly authorizes the input data for a configured provider.
	{
		HttpRequest req(base_url + "/api/sweagent/run");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.Timeout(3000);
		req.Execute();
		if(req.GetStatusCode() != 503) {
			Cerr() << "FAILED: external SweAgent dispatch must remain disabled, got " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    POST /api/sweagent/run (503, external dispatch disabled)\n";
	}

	// External publishing must remain disabled until source classification and
	// explicit publication authorization are implemented.
	{
		HttpRequest req(base_url + "/api/project/synthetic/sync-github");
		req.Timeout(3000);
		req.Execute();
		if(req.GetStatusCode() != 503) {
			Cerr() << "FAILED: GitHub project synchronization must remain disabled, got " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    GitHub project synchronization (503, external publishing disabled)\n";
	}

	// A mutating GitHub API request must also fail closed rather than relying on
	// a disabled UI control.
	{
		HttpRequest req(base_url + "/api/gh/issues/synthetic/create");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.Timeout(3000);
		req.Execute();
		if(req.GetStatusCode() != 503) {
			Cerr() << "FAILED: GitHub issue publication must remain disabled, got " << req.GetStatusCode() << "\n";
			return false;
		}
		Cout() << "ok    GitHub issue publication (503, external operation disabled)\n";
	}

	// Legacy SourceForge demo routes must not serve built-in example projects or
	// write mock records until a typed, configured source adapter exists.
	{
		HttpRequest req(base_url + "/projects/synthetic");
		req.Timeout(3000);
		req.Execute();
		if(req.GetStatusCode() != 503) {
			Cerr() << "FAILED: unconfigured SourceForge project route must return 503, got " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    SourceForge project page (503, no built-in demo content)\n";
	}
	{
		HttpRequest req(base_url + "/api/sf/tickets/synthetic/create");
		req.POST();
		req.Header("Origin", base_url);
		req.ContentType("application/json");
		req.PostData("{\"summary\":\"synthetic fixture\"}");
		req.Timeout(3000);
		req.Execute();
		if(req.GetStatusCode() != 503) {
			Cerr() << "FAILED: unconfigured SourceForge mutation route must return 503, got " << req.GetStatusCode() << "\n";
			app.Quit();
			server_thread.Wait();
			return false;
		}
		Cout() << "ok    SourceForge ticket mutation (503, no mock-store write)\n";
	}

	// Test GET /api/workspace/discover against the loaded synthetic fixture.
	{
		HttpRequest req(base_url + "/api/workspace/discover");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200) {
			Cerr() << "FAILED: GET /api/workspace/discover returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Value v = ParseJSON(res);
		if(!v["success"]) {
			Cerr() << "FAILED: GET /api/workspace/discover logic error: " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /api/workspace/discover (200 OK, generic discovery)\n";
	}

	// Test GET /api/tasks/ideas (re-pointed to the generic DataStore fixture)
	{
		HttpRequest req(base_url + "/api/tasks/ideas");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200) {
			Cerr() << "FAILED: GET /api/tasks/ideas returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Value v = ParseJSON(res);
		if(v["ideas"].GetCount() != 1) {
			Cerr() << "FAILED: /api/tasks/ideas expected 1 idea from fixture, got " << v["ideas"].GetCount() << ": " << res << "\n";
			return false;
		}
		if(v["ideas"][0]["id"] != "task-001") {
			Cerr() << "FAILED: /api/tasks/ideas returned wrong document: " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /api/tasks/ideas (200 OK, DataStore-backed, task-001)\n";
	}

	// Test GET /api/papers/mining (re-pointed to the generic DataStore fixture)
	{
		HttpRequest req(base_url + "/api/papers/mining");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200) {
			Cerr() << "FAILED: GET /api/papers/mining returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Value v = ParseJSON(res);
		if(v["papers"].GetCount() != 1) {
			Cerr() << "FAILED: /api/papers/mining expected 1 paper from fixture, got " << v["papers"].GetCount() << ": " << res << "\n";
			return false;
		}
		if(v["papers"][0]["id"] != "paper-001") {
			Cerr() << "FAILED: /api/papers/mining returned wrong document: " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /api/papers/mining (200 OK, DataStore-backed, paper-001)\n";
	}

	// Test GET /data (Data Explorer index page over the fixture store)
	{
		HttpRequest req(base_url + "/data");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Data Explorer") < 0) {
			Cerr() << "FAILED: GET /data returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /data Data Explorer (200 OK)\n";
	}

	// Test GET /data/papers (Scientific Paper Mining pipeline page)
	{
		HttpRequest req(base_url + "/data/papers");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Scientific Paper Mining") < 0 || res.Find("paper-001") < 0) {
			Cerr() << "FAILED: GET /data/papers returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /data/papers Paper Mining (200 OK, paper-001 listed)\n";
	}

	// Test GET /data/ideas (Task Ideas tier page)
	{
		HttpRequest req(base_url + "/data/ideas");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Task Ideas") < 0 || res.Find("task-001") < 0) {
			Cerr() << "FAILED: GET /data/ideas returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /data/ideas Task Ideas (200 OK, task-001 listed)\n";
	}
	{
		auto catalog = app.PinLatestCatalog();
		const DataDocument* original_topology = catalog->data.FindDocument("topology/synthetic/topology-001");
		CatalogRecordRead topology_qualified = original_topology ? ReadCatalogRecord(*catalog, original_topology->qualified_id, true) : CatalogRecordRead();
		CatalogRecordRead topology_alias = ReadCatalogRecord(*catalog, "topology-002", true);
		CatalogRecordRead topology_ambiguous = ReadCatalogRecord(*catalog, "topology-001", true);
		HttpRequest qualified_req(base_url + "/data/diagram/" + (original_topology ? original_topology->qualified_id : String("missing")));
		qualified_req.Timeout(3000);
		String qualified_html = qualified_req.Execute();
		HttpRequest api_qualified_req(base_url + "/api/data/doc/" + (original_topology ? original_topology->qualified_id : String("missing")));
		api_qualified_req.Timeout(3000);
		ValueMap api_qualified = ParseJSON(api_qualified_req.Execute());
		HttpRequest alias_req(base_url + "/data/diagram/topology-002");
		alias_req.Timeout(3000);
		String alias_html = alias_req.Execute();
		HttpRequest scope_req(base_url + "/data/diagram/scope/synthetic");
		scope_req.Timeout(3000);
		scope_req.Execute();
		String scope_html = scope_req.GetContent();
		HttpRequest ambiguous_req(base_url + "/data/diagram/topology-001");
		ambiguous_req.Timeout(3000);
		ambiguous_req.Execute();
		HttpRequest wrong_kind_req(base_url + "/data/diagram/paper-001");
		wrong_kind_req.Timeout(3000);
		wrong_kind_req.Execute();
		HttpRequest api_alias_req(base_url + "/api/data/doc/topology-002");
		api_alias_req.Timeout(3000);
		ValueMap api_alias = ParseJSON(api_alias_req.Execute());
		HttpRequest api_ambiguous_req(base_url + "/api/data/doc/topology-001");
		api_ambiguous_req.Timeout(3000);
		api_ambiguous_req.Execute();
		bool escaped_label = qualified_html.Find("&lt;script&gt;alert(4)&lt;/script&gt;") >= 0;
		bool active_label = qualified_html.Find("<script>alert(4)</script>") >= 0;
		bool unsafe_url = qualified_html.Find("javascript:alert(5)") >= 0;
		if(!original_topology || topology_qualified.status != "ok" || topology_alias.status != "ok" ||
		   topology_ambiguous.status != "ambiguous" || CatalogRecordExitCode(topology_ambiguous) != 5 ||
		   MapGetString(topology_qualified.response, "qualified_id") != original_topology->qualified_id ||
		   qualified_req.GetStatusCode() != 200 || qualified_html.Find(original_topology->qualified_id) < 0 ||
		   api_qualified_req.GetStatusCode() != 200 || MapGetString(api_qualified, "qualified_id") != original_topology->qualified_id ||
		   qualified_html.Find("2 nodes") < 0 || !escaped_label || active_label || unsafe_url ||
		   alias_req.GetStatusCode() != 200 || MapGetString(topology_alias.response, "qualified_id") != MapGetString(api_alias, "qualified_id") ||
		   alias_html.Find(MapGetString(topology_alias.response, "qualified_id")) < 0 || alias_html.Find("1 nodes") < 0 ||
		   scope_req.GetStatusCode() != 503 || scope_html.Find("diag-topology") >= 0 || scope_html.Find("incomplete_scope") < 0 ||
		   ambiguous_req.GetStatusCode() != 409 || api_ambiguous_req.GetStatusCode() != 409 ||
		   wrong_kind_req.GetStatusCode() != 404) {
			Cerr() << "FAILED: topology detail resolver or scope completeness contract\n";
			return false;
		}
		String route_fixture_root = app.test_fixture_root;
		String complete_root = workspace.FixtureRoot("complete-topology-scope");
		RealizeDirectory(AppendFileName(complete_root, "v2"));
		auto save_scope_doc = [&](const String& id, const String& node_id) {
			String path = AppendFileName(complete_root, "data/topology/" + node_id + ".json");
			RealizeDirectory(GetFileFolder(path));
			ValueMap node; node.Add("id", node_id); node.Add("type", "service"); node.Add("name", node_id);
			ValueArray nodes; nodes.Add(node);
			ValueMap record; record.Add("id", id); record.Add("kind", "topology"); record.Add("name", id);
			record.Add("nodes", nodes); record.Add("edges", ValueArray());
			record.Add("envelope", record_envelope("topology", "topology", id));
			return SaveFile(path, AsJSON(record, true));
		};
		if(!save_scope_doc("topology/synthetic/complete-a", "complete-node-a") ||
		   !save_scope_doc("topology/synthetic/complete-b", "complete-node-b")) {
			Cerr() << "FAILED: could not create complete topology scope fixtures\n";
			return false;
		}
		app.test_fixture_root = complete_root;
		if(!app.RefreshCatalog() || app.PinLatestCatalog()->data_status != "loaded") {
			Cerr() << "FAILED: complete topology scope fixture did not load cleanly\n";
			return false;
		}
		HttpRequest complete_scope_req(base_url + "/data/diagram/scope/synthetic");
		complete_scope_req.Timeout(3000);
		String complete_scope_html = complete_scope_req.Execute();
		app.test_fixture_root = route_fixture_root;
		if(!app.RefreshCatalog() || complete_scope_req.GetStatusCode() != 200 || complete_scope_html.Find("2 nodes") < 0) {
			Cerr() << "FAILED: complete topology scope did not aggregate its source documents\n";
			return false;
		}
		Cout() << "ok    topology qualified/unique/ambiguous routes share catalog resolver; only complete scopes aggregate\n";
		String valid_fixture_root = app.test_fixture_root;
		String partial_root = workspace.FixtureRoot("partial-topology-source");
		String partial_doc_id = "topology/synthetic/partial-only";
		String partial_doc_file = AppendFileName(partial_root, "data/topo/partial-only.json");
		RealizeDirectory(AppendFileName(partial_root, "v2"));
		RealizeDirectory(GetFileFolder(partial_doc_file));
		ValueMap partial_node; partial_node.Add("id", "partial-node"); partial_node.Add("type", "service"); partial_node.Add("name", "Partial synthetic node");
		ValueArray partial_nodes; partial_nodes.Add(partial_node);
		ValueMap partial_topology; partial_topology.Add("id", partial_doc_id); partial_topology.Add("kind", "topology");
		partial_topology.Add("name", "Partial scope fixture"); partial_topology.Add("nodes", partial_nodes); partial_topology.Add("edges", ValueArray());
		partial_topology.Add("envelope", record_envelope("topology", "topology", partial_doc_id));
		if(!SaveFile(partial_doc_file, AsJSON(partial_topology, true)) ||
		   !SaveFile(AppendFileName(partial_root, "data/topo/broken.json"), "{ malformed synthetic topology peer")) {
			Cerr() << "FAILED: could not write partial topology scope fixture\n";
			return false;
		}
		app.test_fixture_root = partial_root;
		if(!app.RefreshCatalog()) { Cerr() << "FAILED: partial topology source should load with explicit parse-error status\n"; return false; }
		auto partial_catalog = app.PinLatestCatalog();
		const DataDocument* partial_document = partial_catalog->data.FindDocument(partial_doc_id);
		HttpRequest partial_scope_req(base_url + "/data/diagram/scope/synthetic");
		partial_scope_req.Timeout(3000);
		partial_scope_req.Execute();
		String partial_scope_html = partial_scope_req.GetContent();
		HttpRequest partial_doc_req(base_url + "/data/diagram/" + (partial_document ? partial_document->qualified_id : String("missing")));
		partial_doc_req.Timeout(3000);
		String partial_doc_html = partial_doc_req.Execute();
		app.test_fixture_root = valid_fixture_root;
		if(!app.RefreshCatalog()) { Cerr() << "FAILED: topology fixture restoration refresh failed\n"; return false; }
		if(!partial_document || partial_catalog->data_status != "loaded_with_errors" ||
		   partial_scope_req.GetStatusCode() != 503 || partial_scope_html.Find("diag-topology") >= 0 ||
		   partial_scope_html.Find("incomplete_scope") < 0 || partial_doc_req.GetStatusCode() != 200 ||
		   partial_doc_html.Find("<svg") < 0 || partial_doc_html.Find("individually validated topology") < 0) {
			Cerr() << "FAILED: partial topology scope rendered as complete or valid direct document was hidden\n";
			return false;
		}
		Cout() << "ok    partial topology scope is visibly unavailable while a valid qualified document remains readable\n";
	}

	// Test GET /data/k/paper (kind listing page)
	{
		HttpRequest req(base_url + "/data/k/paper");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("paper-001") < 0) {
			Cerr() << "FAILED: GET /data/k/paper returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /data/k/paper Kind view (200 OK)\n";
	}
	{
		HttpRequest req(base_url + "/data/k/hostile-link");
		req.Timeout(3000);
		String res = req.Execute();
		String expected_href = "href=\"/data/doc/" + UrlEncode(hostile_link_id) + "\"";
		String unencoded_href = "href=\"/data/doc/" + hostile_link_id + "\"";
		bool encoded = res.Find(expected_href) >= 0;
		bool unencoded = res.Find(unencoded_href) >= 0;
		bool javascript_href = res.Find("href=\"javascript:") >= 0;
		bool raw_event_attribute = res.Find("onmouseover=\"alert(8)") >= 0;
		if(req.GetStatusCode() != 200 || !encoded || unencoded || javascript_href ||
		   raw_event_attribute) {
			Cerr() << "FAILED: data listing emitted an unsafe hostile identifier link (status=" << req.GetStatusCode()
			       << ", expected=" << encoded << ", unencoded=" << unencoded
			       << ", javascript=" << javascript_href << ", raw_event=" << raw_event_attribute << ")\n";
			int link_pos = res.Find("record-8");
			if(link_pos >= 0)
				Cerr() << "Hostile record response fragment: " << res.Mid(max(0, link_pos - 30), 120) << "\n";
			return false;
		}
		Cout() << "ok    data listing percent-encodes hostile identifiers in href attributes\n";
	}

	// Test GET /data/doc/paper-001 (generic record view with array-of-maps table)
	// The "authors" field is an array-of-maps: assert it renders as a real table
	// (<thead> + column headers) and no "[col key error]" / conversion text leaks.
	{
		HttpRequest req(base_url + "/data/doc/paper-001");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Research") < 0) {
			Cerr() << "FAILED: GET /data/doc/paper-001 returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		if(res.Find("<thead>") < 0 || res.Find("affiliation") < 0) {
			Cerr() << "FAILED: /data/doc/paper-001 did not render the authors array-of-maps table: " << res << "\n";
			return false;
		}
		if(res.Find("col key error") >= 0 || res.Find("Invalid value conversion") >= 0) {
			Cerr() << "FAILED: /data/doc/paper-001 rendered conversion error markers: " << res << "\n";
			return false;
		}
		if(res.Find("<script>alert(1)</script>") >= 0 || res.Find("&lt;script&gt;alert(1)&lt;/script&gt;") < 0 ||
		   res.Find("<img src=x onerror=alert(3)>") >= 0 || res.Find("&lt;img src=x onerror=alert(3)&gt;") < 0) {
			Cerr() << "FAILED: /data/doc/paper-001 did not safely escape malicious document text\n";
			return false;
		}
		Cout() << "ok    GET /data/doc/paper-001 record view (authors array-of-maps table renders)\n";
	}

	// Test GET /data/doc/task-001 (generic record view with "risks" array-of-maps)
	{
		HttpRequest req(base_url + "/data/doc/task-001");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("Implement Schema B") < 0) {
			Cerr() << "FAILED: GET /data/doc/task-001 returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		if(res.Find("col key error") >= 0 || res.Find("Invalid value conversion") >= 0) {
			Cerr() << "FAILED: /data/doc/task-001 rendered conversion error markers: " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /data/doc/task-001 record view (risks array-of-maps table renders)\n";
	}

	// Test GET /api/data/doc/paper-001 (generic document JSON API)
	{
		HttpRequest req(base_url + "/api/data/doc/paper-001");
		req.Timeout(3000);
		String res = req.Execute();
		if(req.GetStatusCode() != 200 || res.Find("\"authors\"") < 0) {
			Cerr() << "FAILED: GET /api/data/doc/paper-001 returned " << req.GetStatusCode() << ": " << res << "\n";
			return false;
		}
		Cout() << "ok    GET /api/data/doc/paper-001 (200 OK, generic JSON with authors)\n";
	}

	Cout() << "ManagerSkylark verification PASSED\n";
	return true;
}

END_UPP_NAMESPACE

using namespace Upp;

CONSOLE_APP_MAIN
{
	const Vector<String>& args = CommandLine();
	bool run_self_tests = false;
	int catalog_command_count = 0;
	int catalog_command_index = -1;
	int index_rebuild_count = 0;
	int index_rebuild_index = -1;
	for(int i = 0; i < args.GetCount(); i++)
		if(args[i] == "test" || args[i] == "--test") {
			run_self_tests = true;
		}
		else if(args[i] == "--catalog-record") {
			catalog_command_count++;
			catalog_command_index = i;
		}
		else if(args[i] == "--catalog-index-rebuild") {
			index_rebuild_count++;
			index_rebuild_index = i;
		}
	if(index_rebuild_count) {
		bool valid = index_rebuild_count == 1 && index_rebuild_index == 0 && args.GetCount() == 2 &&
		             args[1] == "--enable-catalog-index" && !run_self_tests && !catalog_command_count;
		if(!valid) {
			ValueMap response;
			response.Add("status", "opt_in_required_or_invalid_usage");
			response.Add("usage", "ManagerSkylark --catalog-index-rebuild --enable-catalog-index");
			Cout() << AsJSON(response, true) << "\n";
			Cout().Flush();
			SetExitCode(2);
			return;
		}
		GetCurrentDirectory();
		ManagerSkylarkApp app;
		app.EnableCatalogIndex(true);
		bool refreshed = app.RefreshCatalog();
		CatalogIndexResult result;
		if(refreshed) result = app.RebuildCatalogIndex();
		else result.status = "catalog_unavailable";
		ValueMap response;
		response.Add("status", result.status);
		if(result.status == "ready") {
			response.Add("generation_id", result.generation);
			response.Add("source_revision", result.source_revision);
			response.Add("record_count", result.count);
		}
		Cout() << AsJSON(response, true) << "\n";
		Cout().Flush();
		SetExitCode(result.status == "ready" ? 0 : 3);
		return;
	}
	if(catalog_command_count) {
		// Initialize U++ process environment before early exits from this CLI path.
		GetCurrentDirectory();
		CatalogRecordRead read;
		String requested;
		bool usage_ok = catalog_command_count == 1 && catalog_command_index == 0 && args.GetCount() == 2 &&
		                !run_self_tests && !args[1].StartsWith("--");
		if(usage_ok)
			requested = args[1];
		if(!usage_ok || requested.IsEmpty()) {
			read.status = "usage";
			read.response.Add("status", "usage");
			read.response.Add("usage", "ManagerSkylark --catalog-record <qualified-id-or-unique-legacy-alias>");
			Cout() << AsJSON(read.response, true) << "\n";
			Cout().Flush();
			SetExitCode(2);
			return;
		}
		ManagerSkylarkApp app;
		app.RefreshCatalog();
		auto catalog = app.PinLatestCatalog();
		if(catalog)
			read = ReadCatalogRecord(*catalog, requested, true);
		else {
			read.status = "unavailable";
			read.response.Add("status", "unavailable");
			read.response.Add("requested_id", requested);
		}
		Cout() << AsJSON(read.response, true) << "\n";
		Cout().Flush();
		SetExitCode(CatalogRecordExitCode(read));
		return;
	}
	if(run_self_tests) {
		bool ok = ManagerSkylarkApp::RunSelfTests(18080);
		Cout().Flush();
		Cerr().Flush();
		SetExitCode(ok ? 0 : 1);
		return;
	}

	ManagerSkylarkApp app;
	if(app.Initialize(args))
		app.Run();
}
