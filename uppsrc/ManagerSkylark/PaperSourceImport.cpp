#include "PaperSourceImport.h"

NAMESPACE_UPP

static bool PaperToken(const String& s, int max = 128)
{
	if(s.IsEmpty() || s.GetCount() > max) return false;
	for(int i = 0; i < s.GetCount(); i++) {
		byte c = s[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':')) return false;
	}
	return true;
}

static bool PaperUtc(const String& s)
{
	if(s.GetCount()!=20 || s[4]!='-' || s[7]!='-' || s[10]!='T' || s[13]!=':' || s[16]!=':' || s[19]!='Z') return false;
	for(int i=0;i<s.GetCount();i++) if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&i!=19&&(s[i]<'0'||s[i]>'9')) return false;
	int year=atoi(s.Left(4)), month=atoi(s.Mid(5,2)), day=atoi(s.Mid(8,2)), hour=atoi(s.Mid(11,2)), minute=atoi(s.Mid(14,2)), second=atoi(s.Mid(17,2));
	static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
	if(month<1||month>12||hour>23||minute>59||second>59) return false;
	int maxday=days[month-1]+(month==2&&(year%4==0&&(year%100!=0||year%400==0)));
	return day>=1&&day<=maxday;
}

static bool NumericHostLabel(const String& label)
{
	if(label.IsEmpty()) return false;
	if(label.Left(2)=="0x" && label.GetCount()>2) {
		for(int i=2;i<label.GetCount();i++) if(!((label[i]>='0'&&label[i]<='9')||(label[i]>='a'&&label[i]<='f'))) return false;
		return true;
	}
	for(int i=0;i<label.GetCount();i++) if(label[i]<'0'||label[i]>'9') return false;
	return true;
}

static bool ValidPaperHost(const String& host)
{
	if(host.IsEmpty() || host.GetCount()>253 || host!=ToLower(host) || host.Find('.')<0) return false;
	if(host=="localhost" || host.EndsWith(".localhost") || host.EndsWith(".local") || host.EndsWith(".internal") ||
	   host.EndsWith(".home") || host.EndsWith(".lan") || host.EndsWith(".test") || host.EndsWith(".onion") ||
	   host.EndsWith(".home.arpa") || host.EndsWith(".localdomain") || host.EndsWith(".intranet") ||
	   host.EndsWith(".corp") || host.EndsWith(".private")) return false;
	Vector<String> labels=Split(host,'.');
	bool numeric_literal=true;
	for(const String& label:labels) {
		if(label.IsEmpty() || label.GetCount()>63 || label[0]=='-' || label[label.GetCount()-1]=='-') return false;
		for(int i=0;i<label.GetCount();i++) if(!((label[i]>='a'&&label[i]<='z')||(label[i]>='0'&&label[i]<='9')||label[i]=='-')) return false;
		if(!NumericHostLabel(label)) numeric_literal=false;
	}
	return !numeric_literal;
}

static bool ValidPaperPath(const String& path, bool allow_prefix)
{
	if(path.IsEmpty() || path[0]!='/' || (path.GetCount()>1 && path[path.GetCount()-1]=='/' && !allow_prefix) || path.Find("//")>=0) return false;
	for(int i=0;i<path.GetCount();i++) {
		byte c=path[i];
		if(c<=32 || c>=127 || c=='\\' || c=='%' || c=='?' || c=='#') return false;
		if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='-'||c=='.'||c=='_'||c=='~')) return false;
	}
	Vector<String> segments=Split(path,'/');
	for(const String& segment:segments) if(segment=="." || segment=="..") return false;
	return true;
}

static bool ParsePaperUrl(const String& url, String& host, String& path)
{
	if(url.GetCount()>2048 || url.Find('#')>=0 || url.Find('?')>=0 || url.Find('\\')>=0 || url.Left(8)!="https://") return false;
	for(int i=0;i<url.GetCount();i++) if((byte)url[i]<=32 || (byte)url[i]>=127) return false;
	int slash=url.Find('/',8);
	String authority=slash<0?url.Mid(8):url.Mid(8,slash-8);
	path=slash<0?String("/"):url.Mid(slash);
	if(authority.IsEmpty() || authority.Find('@')>=0 || authority.Find(':')>=0 || authority.Find('%')>=0 || !ValidPaperHost(authority) || !ValidPaperPath(path,false)) return false;
	host=authority;
	return true;
}

static bool RulePathMatch(const String& path, const String& prefix)
{
	if(path==prefix) return true;
	if(prefix=="/") return path.StartsWith("/");
	if(prefix.EndsWith("/")) return path.StartsWith(prefix);
	return path.StartsWith(prefix) && path.GetCount()>prefix.GetCount() && path[prefix.GetCount()]=='/';
}

static bool ValidPaperRule(const PublicPaperSourceRule& r)
{
	return PaperToken(r.rule_id) && ValidPaperHost(r.hostname) && ValidPaperPath(r.path_prefix,true) &&
	       PaperToken(r.internal_use_mode) && r.max_bytes>0 && r.max_sections>0 && r.max_sections<=62 &&
	       !r.allowed_license_ids.IsEmpty();
}

static ValueMap PaperEnvelope(const String& kind, const String& ws, const String& id)
{
	ValueMap e; e.Add("schema_id", "skylark/" + kind); e.Add("schema_version", "1");
	e.Add("workspace_id", ws); e.Add("stable_id", id); e.Add("record_kind", kind);
	e.Add("revision", "1"); e.Add("visibility", "private"); e.Add("provenance_ref", "trusted-paper-source-import"); return e;
}

bool PaperTransportAttestation::Matches(const String& source_url, const String& final_url, const Vector<String>& redirects, const String& body, const String& mime_type) const
{
	String material=source_url+"\n"+final_url+"\n";
	for(const String& redirect:redirects) material<<redirect<<"\n";
	material<<DataStore::ContentDigest(body)<<"\n"<<mime_type;
	return !binding.IsEmpty() && tool_version=="curl-https-v1" && binding==DataStore::ContentDigest(material);
}

bool PaperSourceImport::Validate(const PaperFetchSnapshot& v, const Vector<PublicPaperSourceRule>& rules, String& error)
{
	error = "invalid";
	String host, path;
	if(!PaperToken(v.workspace_id) || !PaperToken(v.source_identity) || !PaperToken(v.external_version) ||
	   !PaperToken(v.extractor) || !PaperToken(v.extractor_version) || !PaperToken(v.config_revision) ||
	   !PaperToken(v.license_id) || !PaperToken(v.license_evidence_ref) || v.source_bytes.IsEmpty() ||
	   v.source_bytes.GetCount() > 2*1024*1024 || DataStore::ContentDigest(v.source_bytes) != ToLower(v.source_sha256) ||
	   (v.mime_type != "application/pdf" && v.mime_type != "text/plain") || v.sections.IsEmpty() || v.sections.GetCount() > 62 || !PaperUtc(v.captured_at)) return false;
	const PublicPaperSourceRule *rule = NULL;
	if(!ParsePaperUrl(v.source_url, host, path)) return false;
	Index<String> rule_ids;
	for(const PublicPaperSourceRule& r : rules) {
		if(!ValidPaperRule(r) || rule_ids.Find(r.rule_id)>=0) return false;
		rule_ids.Add(r.rule_id);
	}
	for(const PublicPaperSourceRule& r : rules) if(r.hostname == host && RulePathMatch(path,r.path_prefix)) {
		if(rule) return false; rule = &r;
	}
	bool license_allowed=false;
	if(rule) for(const String& license : rule->allowed_license_ids) if(license==v.license_id) license_allowed=true;
	if(!rule || !PaperToken(rule->rule_id) || !PaperToken(rule->internal_use_mode) || rule->max_bytes <= 0 || rule->max_sections <= 0 ||
   v.source_bytes.GetCount() > rule->max_bytes || v.sections.GetCount() > rule->max_sections || !license_allowed || v.license_evidence_ref.IsEmpty()) return false;
	if(v.redirect_chain.GetCount() > 3) return false;
	String current = v.source_url;
	for(const String& redirect : v.redirect_chain) {
		String rh, rp;
		if(!ParsePaperUrl(redirect, rh, rp) || rh != rule->hostname || !RulePathMatch(rp,rule->path_prefix)) return false;
		current = redirect;
	}
	String final_host, final_path;
	if(!ParsePaperUrl(v.final_url, final_host, final_path) || final_host != rule->hostname || !RulePathMatch(final_path,rule->path_prefix) || current != v.final_url || !v.peer_address_claim.IsEmpty()) return false;
	Index<String> locators;
	for(const PaperFetchSection& s : v.sections) {
		if(!PaperToken(s.section) || s.page < 1 || s.page > 100000 || s.text.IsEmpty() || s.text.GetCount() > 4096 ||
		   s.locator != s.section+":p"+AsString(s.page) || locators.Find(s.locator) >= 0) return false;
		locators.Add(s.locator);
		for(int i = 0; i < s.text.GetCount(); i++) if(s.text[i] == 0) return false;
	}
	error.Clear(); return true;
}

PaperSourceImportResult PaperSourceImport::Import(const DataStore& store, const Vector<PublicPaperSourceRule>& rules,
                                                   const TrustedPolicyRegistry& policy, const PaperFetchSnapshot& v,
                                                   const DataWriteAuthorization& auth)
{
	return Import(store,rules,policy,v,v.transport_attestation,auth);
}

PaperSourceImportResult PaperSourceImport::Import(const DataStore& store, const Vector<PublicPaperSourceRule>& rules,
                                                   const TrustedPolicyRegistry& policy, const PaperFetchSnapshot& v,
                                                   const PaperTransportAttestation& attestation,
                                                   const DataWriteAuthorization& auth)
{
	PaperSourceImportResult out;
	String error;
	if(!auth.AllowsWorkspace(v.workspace_id) || !auth.AllowsSource(v.workspace_id, store.GetSourceId())) { out.status = "denied"; return out; }
	bool transported=attestation.Matches(v.source_url,v.final_url,v.redirect_chain,v.source_bytes,v.mime_type);
	if((!attestation.GetToolVersion().IsEmpty() && !transported) || (!v.peer_address_claim.IsEmpty())) { out.status="invalid"; return out; }
	if(!Validate(v, rules, error)) { out.status = "invalid"; return out; }
	String revision = v.external_version + "-extract-" + v.extractor_version + "-" + v.config_revision;
	String identity = v.source_identity;
	String hash = DataStore::ContentDigest(v.source_bytes);
	Vector<String> section_pins;
	for(const PaperFetchSection& section:v.sections) { ValueMap pin; pin.Add("section",section.section); pin.Add("page",section.page); pin.Add("text",section.text); pin.Add("locator",section.locator); section_pins.Add(AsJSON(pin,true)); }
	Sort(section_pins); String extraction_bytes; for(const String& pin:section_pins) extraction_bytes << pin << "\n";
	String extraction_hash=DataStore::ContentDigest(extraction_bytes);
	String src_id = "paper-source-" + DataStore::ContentDigest(v.workspace_id+"\n"+identity+"\n"+revision).Left(32);
	ValueMap source; source.Add("id",src_id); source.Add("kind","paper-source-snapshot"); source.Add("workspace_id",v.workspace_id);
	source.Add("source_identity",identity); source.Add("source_url",v.source_url); source.Add("external_version",v.external_version);
	source.Add("upstream_revision",revision); source.Add("source_sha256",hash); source.Add("extraction_sha256",extraction_hash); source.Add("captured_at",v.captured_at);
	String matched_rule_id, internal_use_mode;
	for(const PublicPaperSourceRule& r:rules) { String h,p; if(ParsePaperUrl(v.source_url,h,p)&&h==r.hostname&&RulePathMatch(p,r.path_prefix)) { matched_rule_id=r.rule_id; internal_use_mode=r.internal_use_mode; } }
	source.Add("rule_id",matched_rule_id); source.Add("internal_use_mode",internal_use_mode); source.Add("final_url",v.final_url);
	source.Add("license_id",v.license_id); source.Add("license_evidence_ref",v.license_evidence_ref);
	source.Add("extractor",v.extractor); source.Add("extractor_version",v.extractor_version); source.Add("config_revision",v.config_revision);
	source.Add("mime_type",v.mime_type); source.Add("transport_installed",transported); source.Add("request_performed",transported);
	ValueArray redirects; for(const String& claim:v.redirect_chain) redirects.Add(claim);
	source.Add("redirect_chain",redirects); source.Add("peer_address_state",transported?String("public_dns_pinned_tls_verified"):String("not_observed_transport_disabled"));
	source.Add("transport_id",transported?String("curl"):String("disabled")); source.Add("transport_version",transported?attestation.GetToolVersion():String("none"));
	source.Add("transport_verification_state",transported?String("tls_peer_and_hostname_verified"):String("disabled"));
	source.Add("section_count",v.sections.GetCount()); source.Add("envelope",PaperEnvelope("paper-source-snapshot",v.workspace_id,src_id));
	EvidencePaperValue paper; EvidenceCapture& c=paper.capture; c.workspace_id=v.workspace_id; c.source_identity=identity; c.upstream_revision=revision;
	c.source_at=v.captured_at; c.captured_at=v.captured_at; c.collector="paper-source-adapter"; c.policy_source_ids.Add(identity);
	for(const PaperFetchSection& s:v.sections) { EvidencePaperSectionEntry e; e.paper_id=s.locator; e.section=s.section; e.page=s.page; e.content=s.text; e.source_identity=identity; e.upstream_revision=revision; paper.sections.Add(e); }
	// Reuse T14's trusted policy validation and canonical section item construction.
	String policy_error;
	int workspace_matches=0, source_matches=0;
	for(const PolicyWorkspaceFact& w:policy.workspaces) if(w.identity.workspace_id==v.workspace_id) workspace_matches++;
	for(const PolicySourceFact& s:policy.sources) if(s.identity.workspace_id==v.workspace_id && s.identity.object_id==identity && s.visible && s.classification==POLICY_PUBLIC) source_matches++;
	bool inherited=false;
	for(const PolicyAncestryFact& edge:policy.ancestry) if(edge.child.workspace_id==v.workspace_id && edge.child.object_id==identity) inherited=true;
	for(const PolicyIdentity& protected_id:policy.protected_ancestry) if(protected_id.workspace_id==v.workspace_id && protected_id.object_id==identity) inherited=true;
	if(!policy.configured || !policy.Validate(policy_error) || workspace_matches!=1 || source_matches!=1 || inherited) { out.status="denied"; return out; }
	const DataDocument *existing_source=NULL;
	for(const DataDocument& d:store.GetDocuments()) if(d.kind=="paper-source-snapshot" && d.workspace_id==v.workspace_id && d.id==src_id) { if(existing_source) { out.status="revision_conflict"; return out; } existing_source=&d; }
	if(existing_source) {
		if(!existing_source->valid || !existing_source->validated || AsJSON(existing_source->data,true)!=AsJSON(source,true)) { out.status="revision_conflict"; return out; }
		int snapshot_count=0,item_count=0;
		for(const DataDocument& d:store.GetDocuments()) if(d.workspace_id==v.workspace_id && MapGetString(d.data,"source_identity")==identity && MapGetString(d.data,"upstream_revision")==revision) {
			if(d.kind=="evidence-snapshot") { if(!d.valid||!d.validated) { out.status="revision_conflict"; return out; } snapshot_count++; }
			if(d.kind=="evidence-item") { if(!d.valid||!d.validated) { out.status="revision_conflict"; return out; } item_count++; }
		}
		out.status=snapshot_count==1 && item_count==v.sections.GetCount() ? "already_committed" : "revision_conflict";
		return out;
	}
	EvidencePreview first;
	c.locator.type="paper-section"; c.locator.id=paper.sections[0].paper_id; c.locator.first=paper.sections[0].page; c.locator.section=paper.sections[0].section;
	first=EvidenceIngestion::PrepareImport(store,policy,paper,auth);
	if(first.status!="ready" && first.status!="already_committed") { out.status=first.status; return out; }
	if(first.snapshot.IsEmpty()) { out.status=first.status=="already_committed"?String("evidence_conflict"):first.status; return out; }
	ValueMap ev_snapshot=first.snapshot; ValueMap ev_env=MapGet(ev_snapshot,"envelope"); ev_env.Set("provenance_ref","paper-source:"+src_id); ev_snapshot.Set("envelope",ev_env);
	Vector<DataBatchChange> changes; DataBatchChange sc; sc.action=DATA_BATCH_CREATE; sc.qualified_id=v.workspace_id+":"+src_id; sc.proposed=source; changes.Add(sc);
	String evidence_snapshot_id=MapGetString(ev_snapshot,"id");
	bool evidence_snapshot_present=false;
	for(const DataDocument& d:store.GetDocuments()) if(d.id==evidence_snapshot_id&&d.workspace_id==v.workspace_id) evidence_snapshot_present=true;
	if(!evidence_snapshot_present) { DataBatchChange ec; ec.action=DATA_BATCH_CREATE; ec.qualified_id=v.workspace_id+":"+evidence_snapshot_id; ec.proposed=ev_snapshot; changes.Add(ec); }
	for(int i=0;i<paper.sections.GetCount();i++) {
		EvidencePaperValue one; one.capture=paper.capture; one.capture.locator.type="paper-section"; one.capture.locator.id=paper.sections[i].paper_id;
		one.capture.locator.first=paper.sections[i].page; one.capture.locator.section=paper.sections[i].section;
		one.sections=clone(paper.sections);
		EvidencePreview ep=EvidenceIngestion::PrepareImport(store,policy,one,auth);
		if(ep.status=="already_committed") continue;
		if(ep.status!="ready") { out.status=ep.status; return out; }
		ValueMap item=ep.evidence; item.Set("snapshot",evidence_snapshot_id);
		ValueMap env=MapGet(item,"envelope"); env.Set("provenance_ref","paper-source:"+src_id); item.Set("envelope",env);
		DataBatchChange change; change.action=DATA_BATCH_CREATE; change.qualified_id=v.workspace_id+":"+MapGetString(item,"id"); change.proposed=item; changes.Add(change);
		out.record_ids.Add(MapGetString(item,"id"));
	}
	if(changes.GetCount()>64) { out.status="invalid"; out.record_ids.Clear(); return out; }
	String operation="paper-import-"+DataStore::ContentDigest(v.workspace_id+"\n"+src_id+"\n"+hash).Left(40);
	DataBatchResult result=store.PublishBatch(auth,operation,changes);
	out.status=result.status; if(result.status=="committed"||result.status=="already_committed") { out.operation_id=operation; out.record_ids.Insert(0,evidence_snapshot_id); out.record_ids.Insert(0,src_id); }
	return out;
}

bool PaperSourceImport::RunSyntheticTests()
{
	Vector<PublicPaperSourceRule> rules; PublicPaperSourceRule rule; rule.rule_id="t73-rule"; rule.hostname="papers.example.invalid"; rule.path_prefix="/approved"; rule.allowed_license_ids.Add("license-synthetic"); rule.max_bytes=1024; rule.max_sections=4; rule.internal_use_mode="internal-only"; rules.Add(rule);
	PaperFetchSnapshot v; v.workspace_id="t73-workspace"; v.source_identity="t73-paper"; v.external_version="v1"; v.source_url="https://papers.example.invalid/approved/paper"; v.final_url=v.source_url; v.license_id="license-synthetic"; v.license_evidence_ref="license-terms-synthetic"; v.source_bytes="synthetic PDF bytes"; v.source_sha256=DataStore::ContentDigest(v.source_bytes); v.captured_at="2026-01-01T00:00:00Z"; v.extractor="synthetic-extractor"; v.extractor_version="v1"; v.config_revision="c1"; v.mime_type="application/pdf";
	PaperFetchSection section; section.section="abstract"; section.page=1; section.text="synthetic section text"; section.locator="abstract:p1"; v.sections.Add(section);
	String error;
	if(!Validate(v,rules,error)) return false;
	PaperFetchSnapshot bad=v; bad.source_url="http://papers.example.invalid/approved/paper"; if(Validate(bad,rules,error)) return false;
	for(const String& host:{"2130706433","127.1","0177.0.0.1","0x7f000001","0x7f.0.0.1","127.0.0.1","10.1.2.3","192.168.1.1","169.254.1.1","0.0.0.0","224.0.0.1","::1","[::1]","::ffff:127.0.0.1","fe80::1%eth0","localhost","a.localhost","paper.local","paper.internal","router.home","printer.lan","nas.home.arpa"}) {
		bad=v; bad.source_url="https://"+host+"/approved/paper"; bad.final_url=bad.source_url;
		if(Validate(bad,rules,error)) return false;
	}
	for(const String& url:{"https://user@papers.example.invalid/approved/paper","https://papers.example.invalid:443/approved/paper","https://papers.example.invalid/approved/paper#part","https://papers.example.invalid/approved/paper?q=x","https://papers.example.invalid/approved/%2e%2e/private","https://papers.example.invalid/approved/%2E%2E/private","https://papers.example.invalid/approved//paper","https://papers.example.invalid/approved/./paper","https://papers.example.invalid/approved/../private","https://papers.example.invalid\\approved\\paper","https://papers.example.invalid/approved/paper ","https://papers.example.invalid/approved/pa\x01per","https://-bad.example.invalid/approved/paper","https://bad-.example.invalid/approved/paper","https://bad..example.invalid/approved/paper"}) {
		bad=v; bad.source_url=url; bad.final_url=url;
		if(Validate(bad,rules,error)) return false;
	}
	bad=v; bad.source_url="https://papers.example.invalid/approved-evil/paper"; bad.final_url=bad.source_url; if(Validate(bad,rules,error)) return false;
	bad=v; bad.redirect_chain.Add("https://papers.example.invalid/approved-evil/step"); bad.final_url=bad.redirect_chain.Top(); if(Validate(bad,rules,error)) return false;
	bad=v; bad.redirect_chain.Add("https://papers.example.invalid/approved/step"); bad.final_url="https://papers.example.invalid/approved-evil/final"; if(Validate(bad,rules,error)) return false;
	bad=v; bad.redirect_chain.Add("https://papers.example.invalid/approved/step"); bad.final_url=bad.redirect_chain.Top(); if(!Validate(bad,rules,error)) return false;
	bad=v; for(int i=0;i<4;i++) bad.redirect_chain.Add("https://papers.example.invalid/approved/step"+AsString(i)); bad.final_url=bad.redirect_chain.Top(); if(Validate(bad,rules,error)) return false;
	PublicPaperSourceRule bad_rule=rule; bad_rule.hostname="Papers.example.invalid"; Vector<PublicPaperSourceRule> bad_rules; bad_rules.Add(bad_rule); if(Validate(v,bad_rules,error)) return false;
	bad_rule=rule; bad_rule.path_prefix="/approved/../private"; bad_rules.Clear(); bad_rules.Add(bad_rule); if(Validate(v,bad_rules,error)) return false;
	bad_rule=rule; bad_rule.path_prefix="/approved//private"; bad_rules.Clear(); bad_rules.Add(bad_rule); if(Validate(v,bad_rules,error)) return false;
	bad_rule=rule; bad_rule.path_prefix="/approved/%2e%2e/private"; bad_rules.Clear(); bad_rules.Add(bad_rule); if(Validate(v,bad_rules,error)) return false;
	bad_rule=rule; bad_rule.path_prefix="approved"; bad_rules.Clear(); bad_rules.Add(bad_rule); if(Validate(v,bad_rules,error)) return false;
	bad=v; bad.license_id="unknown-license"; if(Validate(bad,rules,error)) return false;
	bad=v; bad.source_sha256=String('0',64); if(Validate(bad,rules,error)) return false;
	bad=v; bad.sections[0].locator="unknown:p1"; if(Validate(bad,rules,error)) return false;
	bad=v; bad.mime_type="application/octet-stream"; if(Validate(bad,rules,error)) return false;
	bad=v; bad.source_bytes=String('x',1025); bad.source_sha256=DataStore::ContentDigest(bad.source_bytes); if(Validate(bad,rules,error)) return false;
	bad=v; bad.license_evidence_ref.Clear(); if(Validate(bad,rules,error)) return false;
	bad=v; bad.sections[0].text=String('x',4097); if(Validate(bad,rules,error)) return false;
	bad=v; bad.captured_at="not-a-time"; if(Validate(bad,rules,error)) return false;
	bad=v; bad.sections.Add(section); if(Validate(bad,rules,error)) return false;
	PaperSourceImport empty; if(!empty.rules.IsEmpty() || Validate(v,empty.rules,error)) return false;
	return true;
}

END_UPP_NAMESPACE
