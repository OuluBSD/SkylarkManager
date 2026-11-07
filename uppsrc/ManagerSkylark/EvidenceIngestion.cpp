#include "EvidenceIngestion.h"

NAMESPACE_UPP

static bool EvidenceToken(const String& s, int limit = 160)
{
	if(s.IsEmpty() || s.GetCount() > limit) return false;
	for(int i = 0; i < s.GetCount(); i++) {
		byte c = s[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ':')) return false;
	}
	return true;
}

static int64 DaysFromCivil(int y, unsigned m, unsigned d)
{
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return (int64)era * 146097 + (int64)doe - 719468;
}

static void CivilFromDays(int64 z, int& y, unsigned& m, unsigned& d)
{
	z += 719468;
	int64 era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
	y = (int)yoe + (int)era * 400;
	unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
	unsigned mp = (5*doy + 2)/153;
	d = doy - (153*mp+2)/5 + 1;
	m = mp + (mp < 10 ? 3 : -9);
	y += m <= 2;
}

static bool Digits(const String& s, int at, int n, int& out)
{
	if(at < 0 || at + n > s.GetCount()) return false;
	out = 0;
	for(int i = 0; i < n; i++) { char c=s[at+i]; if(c<'0'||c>'9') return false; out=out*10+c-'0'; }
	return true;
}

static String PadNumber(int n, int width)
{
	String s=AsString(n);
	while(s.GetCount()<width) s="0"+s;
	return s;
}

static bool NormalizeUtc(const String& in, String& out)
{
	int y,mo,d,h,mi,se;
	if(in.GetCount() < 20 || in[4]!='-' || in[7]!='-' || in[10]!='T' || in[13]!=':' || in[16]!=':' ||
	   !Digits(in,0,4,y)||!Digits(in,5,2,mo)||!Digits(in,8,2,d)||!Digits(in,11,2,h)||!Digits(in,14,2,mi)||!Digits(in,17,2,se) ||
	   mo<1||mo>12||d<1||d>31||h>23||mi>59||se>59) return false;
	static const int month_days[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
	int max_day=month_days[mo-1];
	if(mo==2 && (y%4==0 && (y%100!=0 || y%400==0))) max_day++;
	if(d>max_day) return false;
	int pos=19, offset=0;
	String fraction;
	if(pos < in.GetCount() && in[pos]=='.') {
		pos++; int start=pos, digits=0;
		while(pos<in.GetCount() && in[pos]>='0'&&in[pos]<='9') { if(++digits>9) return false; pos++; }
		if(!digits) return false;
		fraction=in.Mid(start,digits);
		while(fraction.GetCount() && fraction[fraction.GetCount()-1]=='0') fraction=fraction.Left(fraction.GetCount()-1);
	}
	if(pos < in.GetCount() && in[pos]=='Z') pos++;
	else if(pos+6 == in.GetCount() && (in[pos]=='+'||in[pos]=='-') && in[pos+3]==':' ) {
		int oh,om; if(!Digits(in,pos+1,2,oh)||!Digits(in,pos+4,2,om)||oh>23||om>59) return false;
		offset=(oh*60+om)*(in[pos]=='+'?1:-1); pos+=6;
	}
	else return false;
	if(pos!=in.GetCount()) return false;
	int64 sec=DaysFromCivil(y,mo,d)*86400+h*3600+mi*60+se-(int64)offset*60;
	int64 days=sec/86400, rem=sec%86400; if(rem<0) { rem+=86400; days--; }
	unsigned um,ud; CivilFromDays(days,y,um,ud);
	if(y<0 || y>9999) return false;
	out=PadNumber(y,4)+"-"+PadNumber((int)um,2)+"-"+PadNumber((int)ud,2)+"T"+
	     PadNumber((int)(rem/3600),2)+":"+PadNumber((int)(rem/60%60),2)+":"+PadNumber((int)(rem%60),2)+(fraction.IsEmpty()?String():"."+fraction)+"Z";
	return true;
}

static String KindName(EvidenceSourceKind k)
{
	switch(k) {
	case EVIDENCE_PR_REVIEW: return "pr-review";
	case EVIDENCE_COMMIT_DIFF: return "commit-diff";
	case EVIDENCE_MESSAGE_THREAD: return "message-thread";
	case EVIDENCE_PAPER: return "paper";
	}
	return String();
}

ValueMap EvidenceLocator::ToValue() const
{
	ValueMap v; v.Add("type", type);
	if(type=="commit-line") { v.Add("id",id); v.Add("path",path); v.Add("first",first); v.Add("last",last); }
	else if(type=="pr-review") { v.Add("id",id); v.Add("revision",section); }
	else if(type=="message-thread") { v.Add("id",id); v.Add("thread",section); }
	else if(type=="paper-section") { v.Add("id",id); v.Add("page",first); v.Add("section",section); }
	return v;
}

static bool ValidLocator(const EvidenceLocator& l, EvidenceSourceKind k)
{
	if(!EvidenceToken(l.id,128)) return false;
	if(k==EVIDENCE_PR_REVIEW) return l.type=="pr-review" && EvidenceToken(l.section,128) && l.path.IsEmpty() && l.first==0 && l.last==0;
	if(k==EVIDENCE_COMMIT_DIFF) {
		if(l.type!="commit-line" || l.path.IsEmpty() || l.path.GetCount()>240 || l.path[0]=='/' || l.path.Find('\\')>=0 || l.first<1 || l.last<l.first || l.last-l.first>1000) return false;
		for(const String& p : Split(l.path,'/')) if(p.IsEmpty()||p=="."||p=="..") return false;
		for(int i=0;i<l.path.GetCount();i++) if((byte)l.path[i]<32 || (byte)l.path[i]==127) return false;
		return l.section.IsEmpty();
	}
	if(k==EVIDENCE_MESSAGE_THREAD) return l.type=="message-thread" && EvidenceToken(l.section,128) && l.path.IsEmpty() && l.first==0 && l.last==0;
	if(k==EVIDENCE_PAPER) return l.type=="paper-section" && l.first>=1 && l.first<=100000 && l.last==0 && EvidenceToken(l.section,128) && l.path.IsEmpty();
	return false;
}

static const PolicySourceFact *FindSource(const TrustedPolicyRegistry& p, const String& workspace, const String& id, bool& ambiguous)
{
	const PolicySourceFact *found=NULL; ambiguous=false;
	const PolicyWorkspaceFact *workspace_fact=NULL;
	for(const PolicyWorkspaceFact& w:p.workspaces) if(w.identity.workspace_id==workspace) { if(workspace_fact) ambiguous=true; workspace_fact=&w; }
	if(ambiguous || !workspace_fact) return NULL;
	for(const PolicySourceFact& s : p.sources) if(s.workspace.Key()==workspace_fact->identity.Key() && s.identity.workspace_id==workspace && s.identity.object_id==id) { if(found) ambiguous=true; found=&s; }
	return found;
}

static bool EffectiveSourcePolicy(const TrustedPolicyRegistry& p, const String& workspace, const Vector<String>& ids, String& classification, String& visibility)
{
	String error;
	if(!p.configured || !p.Validate(error) || ids.IsEmpty() || ids.GetCount()>256) return false;
	PolicyClassification level=POLICY_PUBLIC;
	Index<String> visited;
	Index<String> contributors;
	Vector<const PolicySourceFact*> pending;
	for(const String& id : ids) {
		if(contributors.Find(id)>=0) return false;
		contributors.Add(id);
		bool ambiguous=false; const PolicySourceFact *s=FindSource(p,workspace,id,ambiguous);
		if(!s||ambiguous) return false;
		pending.Add(s);
	}
	bool visible=true;
	for(int at=0;at<pending.GetCount();at++) {
		const PolicySourceFact *current=pending[at];
		String key=current->identity.Key();
		if(visited.Find(key)>=0) continue;
		if(visited.GetCount()>=512) return false;
		visited.Add(key);
		if(current->identity.workspace_id!=workspace) return false;
		if(!current->visible) visible=false;
		if(current->classification>level) level=current->classification;
		for(const PolicyIdentity& protected_id:p.protected_ancestry) if(protected_id.Key()==key) level=POLICY_PROTECTED;
		for(const PolicyAncestryFact& edge:p.ancestry) if(edge.child.Key()==key) {
			if(edge.child.workspace_id!=workspace || edge.parent.workspace_id!=workspace) return false;
			bool ambiguous=false; const PolicySourceFact *parent=FindSource(p,workspace,edge.parent.object_id,ambiguous);
			if(!parent||ambiguous) return false;
			pending.Add(parent);
			if(pending.GetCount()>512) return false;
		}
	}
	classification=level==POLICY_PUBLIC?"public":level==POLICY_PRIVATE?"private":"protected";
	visibility=visible?"visible":"restricted";
	if(!visible) return false;
	return true;
}

static String LocatorIdentity(const EvidenceLocator& l) { return AsJSON(l.ToValue(),true); }

static ValueMap EvidenceEnvelope(const String& kind, const String& workspace, const String& id)
{
	ValueMap e; e.Add("schema_id", "skylark/" + kind); e.Add("schema_version", "2");
	e.Add("workspace_id", workspace); e.Add("stable_id", id); e.Add("record_kind", kind);
	e.Add("revision", "1"); e.Add("visibility", "private"); e.Add("provenance_ref", "trusted-evidence-import");
	return e;
}

static EvidencePreview SafeEvidenceFailure(const EvidencePreview& in)
{
	EvidencePreview out;
	out.status = in.status.IsEmpty() ? String("invalid") : in.status;
	return out;
}

static EvidencePreview PreviewCapture(const DataStore& store, const TrustedPolicyRegistry& policy, const EvidenceCapture& c, EvidenceSourceKind expected_kind)
{
	EvidencePreview r; r.status="invalid"; r.resolution="invalid";
	String source_at,captured_at;
	if(c.kind!=expected_kind || KindName(c.kind).IsEmpty() || !EvidenceToken(c.workspace_id,128) || !EvidenceToken(c.source_identity) ||
	   !EvidenceToken(c.upstream_revision) || (!c.previous_revision.IsEmpty() && !EvidenceToken(c.previous_revision)) || c.previous_revision==c.upstream_revision || !EvidenceToken(c.collector,128) || c.pinned_locations.GetCount()>EvidenceIngestion::MAX_LOCATIONS ||
	   c.policy_source_ids.IsEmpty() || c.policy_source_ids.GetCount()>256 || !NormalizeUtc(c.source_at,source_at) || !NormalizeUtc(c.captured_at,captured_at) || !ValidLocator(c.locator,c.kind)) return SafeEvidenceFailure(r);
	String visibility;
	Vector<String> contributor_ids=clone(c.policy_source_ids);
	bool source_contributor=false;
	for(const String& id:contributor_ids) if(id==c.source_identity) source_contributor=true;
	if(!source_contributor || !EffectiveSourcePolicy(policy,c.workspace_id,contributor_ids,r.classification,visibility)) { r.status="denied"; return SafeEvidenceFailure(r); }
	r.visibility=visibility;
	String selected_content;
	int matches=0;
	int64 supplied_bytes=0;
	Vector<String> pinned_records;
	for(const EvidencePinnedLocation& location:c.pinned_locations) {
		if(!ValidLocator(location.locator,c.kind) || location.source_identity!=c.source_identity || location.upstream_revision!=c.upstream_revision || location.content.GetCount()>EvidenceIngestion::MAX_CONTENT_BYTES) { r.status="invalid"; return SafeEvidenceFailure(r); }
		for(int n=0;n<location.content.GetCount();n++) if(location.content[n]==0) { r.status="invalid"; return SafeEvidenceFailure(r); }
		supplied_bytes+=location.content.GetCount();
		if(supplied_bytes>EvidenceIngestion::MAX_CONTENT_BYTES) { r.status="invalid"; return SafeEvidenceFailure(r); }
		String key=LocatorIdentity(location.locator);
		pinned_records.Add(key+"\n"+AsJSON(location.content,true)+"\n"+location.source_identity+"\n"+location.upstream_revision);
		if(key==LocatorIdentity(c.locator)) { matches++; selected_content=location.content; }
	}
	Sort(pinned_records);
	String full_snapshot;
	for(const String& record : pinned_records) full_snapshot << record << "\n";
	if(full_snapshot.GetCount()>EvidenceIngestion::MAX_CONTENT_BYTES) { r.status="invalid"; return SafeEvidenceFailure(r); }
	r.content_sha256=DataStore::ContentDigest(full_snapshot);
	if(c.retracted) r.resolution="retracted";
	else if(matches>1) r.resolution="ambiguous";
	else if(matches==0) r.resolution="unresolved";
	else r.resolution="resolved";
	if(r.resolution!="resolved") selected_content.Clear();
	String stable=DataStore::ContentDigest(c.workspace_id+"\n"+KindName(c.kind)+"\n"+c.source_identity+"\n"+c.upstream_revision);
	r.snapshot_id="evi-s-"+stable;
	String evid=DataStore::ContentDigest(stable+"\n"+LocatorIdentity(c.locator));
	String snapshot_id=r.snapshot_id;
	int prior_count=0, exact_count=0;
	const DataDocument *exact=NULL;
	for(const DataDocument& d : store.GetDocuments()) if(d.kind=="evidence-snapshot" &&
	   MapGetString(d.data,"workspace_id")==c.workspace_id && MapGetString(d.data,"source_identity")==c.source_identity) {
		if(!d.valid || !d.validated) { r.status="lineage_conflict"; return SafeEvidenceFailure(r); }
		String rev=MapGetString(d.data,"upstream_revision");
		if(MapGetString(d.data,"source_kind")!=KindName(c.kind)) continue;
		prior_count++;
		if(rev==c.upstream_revision) { exact_count++; exact=&d; }
		if(!c.previous_revision.IsEmpty() && rev==c.previous_revision) { r.predecessor_id=d.id; }
	}
	if(exact_count>1) { r.status="revision_conflict"; return SafeEvidenceFailure(r); }
	if(exact) {
		String expected_predecessor;
		if(!c.previous_revision.IsEmpty()) {
			int predecessor_count=0;
			for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id==c.workspace_id && d.kind=="evidence-snapshot" &&
			   MapGetString(d.data,"source_kind")==KindName(c.kind) && MapGetString(d.data,"source_identity")==c.source_identity &&
			   MapGetString(d.data,"upstream_revision")==c.previous_revision) { predecessor_count++; expected_predecessor=d.id; }
			if(predecessor_count!=1) { r.status="revision_conflict"; return SafeEvidenceFailure(r); }
		}
		String stored_predecessor=MapGetString(exact->data,"predecessor");
		if(MapGetString(exact->data,"content_sha256")!=r.content_sha256 || MapGet(exact->data,"retracted")!=c.retracted ||
		   MapGetString(exact->data,"previous_revision")!=c.previous_revision ||
		   stored_predecessor!=(expected_predecessor.IsEmpty()?String("none"):expected_predecessor)) { r.status="revision_conflict"; return SafeEvidenceFailure(r); }
		snapshot_id=exact->id;
	}
	else if(c.retracted && c.previous_revision.IsEmpty()) { r.status="lineage_conflict"; return SafeEvidenceFailure(r); }
	else if(prior_count==0) {
		if(!c.previous_revision.IsEmpty()) { r.status="lineage_conflict"; return SafeEvidenceFailure(r); }
	}
	else {
		int predecessor_count=0;
		for(const DataDocument& d : store.GetDocuments()) if(d.workspace_id==c.workspace_id && d.kind=="evidence-snapshot" && MapGetString(d.data,"source_kind")==KindName(c.kind) && MapGetString(d.data,"source_identity")==c.source_identity && MapGetString(d.data,"upstream_revision")==c.previous_revision) predecessor_count++;
		if(c.previous_revision.IsEmpty() || predecessor_count!=1) { r.status="lineage_conflict"; return SafeEvidenceFailure(r); }
	}
	String evidence_id="evi-i-"+evid;
	if(c.retracted) { r.resolution="retracted"; selected_content="[retracted]"; }
	r.snapshot.Add("id",snapshot_id); r.snapshot.Add("kind","evidence-snapshot"); r.snapshot.Add("workspace_id",c.workspace_id); r.snapshot.Add("source_kind",KindName(c.kind));
	r.snapshot.Add("source_identity",c.source_identity); r.snapshot.Add("upstream_revision",c.upstream_revision); r.snapshot.Add("content_sha256",r.content_sha256);
	r.snapshot.Add("captured_at",captured_at); r.snapshot.Add("source_at",source_at); r.snapshot.Add("collector",c.collector); r.snapshot.Add("locator",c.locator.ToValue());
	r.snapshot.Add("resolution",r.resolution); r.snapshot.Add("content",c.retracted?String("[retracted]"):full_snapshot); r.snapshot.Add("classification",r.classification); r.snapshot.Add("visibility",r.visibility);
	r.snapshot.Add("retracted",c.retracted); r.snapshot.Add("predecessor",r.predecessor_id.IsEmpty()?String("none"):r.predecessor_id); r.snapshot.Add("previous_revision",c.previous_revision);
	ValueArray contributors; for(const String& id:contributor_ids) contributors.Add(id); r.snapshot.Add("contributors",contributors);
	r.snapshot.Add("envelope", EvidenceEnvelope("evidence-snapshot", c.workspace_id, snapshot_id));
	r.evidence.Add("id",evidence_id); r.evidence.Add("kind","evidence-item"); r.evidence.Add("workspace_id",c.workspace_id); r.evidence.Add("snapshot",snapshot_id);
	r.evidence.Add("source_identity",c.source_identity); r.evidence.Add("upstream_revision",c.upstream_revision); r.evidence.Add("content_sha256",r.content_sha256);
	r.evidence.Add("locator",c.locator.ToValue()); r.evidence.Add("resolution",r.resolution); r.evidence.Add("excerpt",selected_content.Left(4096));
	r.evidence.Add("classification",r.classification); r.evidence.Add("visibility",r.visibility);
	r.evidence.Add("contributors",contributors);
	r.evidence.Add("envelope", EvidenceEnvelope("evidence-item", c.workspace_id, evidence_id));
	r.operation_id="evidence-"+DataStore::ContentDigest(r.snapshot_id+"\n"+DataStore::ContentDigest(AsJSON(r.evidence,true))).Left(48);
	r.status="ready";
	return r;
}

static bool AddPinned(EvidenceCapture& capture,const EvidenceLocator& locator,const String& content,const String& source_identity,const String& upstream_revision,int64& bytes)
{
	if(!ValidLocator(locator,capture.kind) || content.GetCount()>EvidenceIngestion::MAX_CONTENT_BYTES ||
	   bytes+content.GetCount()>EvidenceIngestion::MAX_CONTENT_BYTES) return false;
	for(int i=0;i<content.GetCount();i++) if(content[i]==0) return false;
	bytes+=content.GetCount(); EvidencePinnedLocation item; item.locator=locator; item.content=content; item.source_identity=source_identity; item.upstream_revision=upstream_revision; capture.pinned_locations.Add(item); return true;
}

static bool Adapt(const EvidencePrReviewValue& value,EvidenceCapture& capture)
{
	if(value.reviews.GetCount()>EvidenceIngestion::MAX_LOCATIONS) return false;
	capture=value.capture; capture.kind=EVIDENCE_PR_REVIEW; capture.pinned_locations.Clear();
	int64 bytes=0; for(const EvidencePrReviewEntry& entry:value.reviews) { EvidenceLocator locator; locator.type="pr-review"; locator.id=entry.id; locator.section=entry.revision; if(entry.source_identity!=capture.source_identity || entry.upstream_revision!=capture.upstream_revision || !AddPinned(capture,locator,entry.content,entry.source_identity,entry.upstream_revision,bytes)) return false; }
	return true;
}
static bool Adapt(const EvidenceCommitDiffValue& value,EvidenceCapture& capture)
{
	if(value.lines.GetCount()>EvidenceIngestion::MAX_LOCATIONS) return false;
	capture=value.capture; capture.kind=EVIDENCE_COMMIT_DIFF; capture.pinned_locations.Clear();
	int64 bytes=0; for(const EvidenceCommitLineEntry& entry:value.lines) { EvidenceLocator locator; locator.type="commit-line"; locator.id=entry.commit_id; locator.path=entry.path; locator.first=entry.first; locator.last=entry.last; if(entry.source_identity!=capture.source_identity || entry.upstream_revision!=capture.upstream_revision || !AddPinned(capture,locator,entry.content,entry.source_identity,entry.upstream_revision,bytes)) return false; }
	return true;
}
static bool Adapt(const EvidenceMessageThreadValue& value,EvidenceCapture& capture)
{
	if(value.messages.GetCount()>EvidenceIngestion::MAX_LOCATIONS) return false;
	capture=value.capture; capture.kind=EVIDENCE_MESSAGE_THREAD; capture.pinned_locations.Clear();
	int64 bytes=0; for(const EvidenceMessageEntry& entry:value.messages) { EvidenceLocator locator; locator.type="message-thread"; locator.id=entry.message_id; locator.section=entry.thread_id; if(entry.source_identity!=capture.source_identity || entry.upstream_revision!=capture.upstream_revision || !AddPinned(capture,locator,entry.content,entry.source_identity,entry.upstream_revision,bytes)) return false; }
	return true;
}
static bool Adapt(const EvidencePaperValue& value,EvidenceCapture& capture)
{
	if(value.sections.GetCount()>EvidenceIngestion::MAX_LOCATIONS) return false;
	capture=value.capture; capture.kind=EVIDENCE_PAPER; capture.pinned_locations.Clear();
	int64 bytes=0; for(const EvidencePaperSectionEntry& entry:value.sections) { EvidenceLocator locator; locator.type="paper-section"; locator.id=entry.paper_id; locator.first=entry.page; locator.section=entry.section; if(entry.source_identity!=capture.source_identity || entry.upstream_revision!=capture.upstream_revision || !AddPinned(capture,locator,entry.content,entry.source_identity,entry.upstream_revision,bytes)) return false; }
	return true;
}

static EvidencePreview PrepareCapture(const DataStore& store,const TrustedPolicyRegistry& policy,const EvidenceCapture& c,EvidenceSourceKind kind,const DataWriteAuthorization& auth)
{
	if(!auth.AllowsWorkspace(c.workspace_id) || !auth.AllowsSource(c.workspace_id,store.GetSourceId())) { EvidencePreview denied; denied.status="denied"; return denied; }
	EvidencePreview p=PreviewCapture(store,policy,c,kind);
	if(p.status!="ready") return SafeEvidenceFailure(p);
	String evidence_id=MapGetString(p.evidence,"id");
	for(const DataDocument& d : store.GetDocuments()) if(d.kind=="evidence-item" && d.workspace_id==c.workspace_id && d.id==evidence_id) {
		if(!d.valid || !d.validated || AsJSON(d.data,true)!=AsJSON(p.evidence,true)) { p.status="evidence_conflict"; return SafeEvidenceFailure(p); }
		p.status="already_committed";
		return SafeEvidenceFailure(p);
	}
	return p;
}

static DataBatchResult ImportCapture(const DataStore& store,const TrustedPolicyRegistry& policy,const EvidenceCapture& c,EvidenceSourceKind kind,const DataWriteAuthorization& auth)
{
	DataBatchResult denied; denied.status="invalid_request";
	EvidencePreview p=PrepareCapture(store,policy,c,kind,auth);
	if(p.status!="ready") { denied.status=p.status; return denied; }
	bool snapshot_present=false, evidence_present=false;
	for(const DataDocument& d : store.GetDocuments()) if(MapGetString(d.data,"workspace_id")==c.workspace_id) {
		if(d.kind=="evidence-snapshot" && d.id==MapGetString(p.snapshot,"id")) {
			if(!d.valid || !d.validated) { denied.status="revision_conflict"; return denied; }
			snapshot_present=true;
		}
		if(d.kind=="evidence-item" && d.id==MapGetString(p.evidence,"id")) {
			if(!d.valid || !d.validated) { denied.status="evidence_conflict"; return denied; }
			if(AsJSON(d.data,true)!=AsJSON(p.evidence,true)) { denied.status="evidence_conflict"; return denied; }
			evidence_present=true;
	}
	}
	if(evidence_present && !snapshot_present) { denied.status="evidence_conflict"; return denied; }
	if(snapshot_present && evidence_present) { denied.status="already_committed"; return denied; }
	DataBatchChange a,b; a.action=b.action=DATA_BATCH_CREATE;
	a.qualified_id=c.workspace_id+":"+MapGetString(p.snapshot,"id"); a.proposed=p.snapshot;
	b.qualified_id=c.workspace_id+":"+MapGetString(p.evidence,"id"); b.proposed=p.evidence;
	Vector<DataBatchChange> changes;
	if(!snapshot_present) changes.Add(a);
	changes.Add(b);
	// DataStore snapshots are immutable per instance; callers must reload before a later import.
	// PublishBatch revalidates identities under its lock and fails closed on stale creates.
	DataBatchResult published=store.PublishBatch(auth,p.operation_id,changes);
	if(published.status!="committed") {
		DataBatchResult safe;
		safe.status=published.status.IsEmpty()?String("invalid_request"):published.status;
		return safe;
	}
	return published;
}

#define EVIDENCE_TYPED_IMPORT(Type, Kind) \
EvidencePreview EvidenceIngestion::PrepareImport(const DataStore& s,const TrustedPolicyRegistry& p,const Type& v,const DataWriteAuthorization& a) { EvidenceCapture c; if(!Adapt(v,c)) { EvidencePreview r; r.status="invalid"; return r; } return PrepareCapture(s,p,c,Kind,a); } \
DataBatchResult EvidenceIngestion::Import(const DataStore& s,const TrustedPolicyRegistry& p,const Type& v,const DataWriteAuthorization& a) { EvidenceCapture c; if(!Adapt(v,c)) { DataBatchResult r; r.status="invalid_request"; return r; } return ImportCapture(s,p,c,Kind,a); }
EVIDENCE_TYPED_IMPORT(EvidencePrReviewValue,EVIDENCE_PR_REVIEW)
EVIDENCE_TYPED_IMPORT(EvidenceCommitDiffValue,EVIDENCE_COMMIT_DIFF)
EVIDENCE_TYPED_IMPORT(EvidenceMessageThreadValue,EVIDENCE_MESSAGE_THREAD)
EVIDENCE_TYPED_IMPORT(EvidencePaperValue,EVIDENCE_PAPER)
#undef EVIDENCE_TYPED_IMPORT

bool EvidenceIngestion::RunSyntheticTests(const DataWriteAuthorization& authorization, const DataWriteAuthorization& wrong_workspace_authorization)
{
	String error;
	// Fixtures use only already-pinned values; URL-shaped text remains inert.
	TrustedPolicyRegistry policy; policy.configured=true;
	PolicyWorkspaceFact wf; wf.identity.workspace_id="test-workspace"; wf.identity.object_id="workspace"; policy.workspaces.Add(wf);
	auto source=[&](const String& id, PolicyClassification cls, bool visible) {
		PolicySourceFact s; s.workspace=wf.identity; s.identity.workspace_id="test-workspace"; s.identity.object_id=id; s.classification=cls; s.visible=visible; policy.sources.Add(s);
	};
	source("pr",POLICY_PUBLIC,true); source("commit",POLICY_PUBLIC,true); source("message",POLICY_PUBLIC,true); source("paper",POLICY_PUBLIC,true);
	source("leaf",POLICY_PUBLIC,true); source("middle",POLICY_PUBLIC,true); source("ancestor",POLICY_PROTECTED,true); source("hidden",POLICY_PUBLIC,false); source("private",POLICY_PRIVATE,true);
	auto edge=[&](const String& child,const String& parent) { PolicyAncestryFact e; e.child.workspace_id=e.parent.workspace_id="test-workspace"; e.child.object_id=child; e.parent.object_id=parent; policy.ancestry.Add(e); };
	edge("leaf","middle"); edge("middle","ancestor");
	PolicyIdentity protected_id; protected_id.workspace_id="test-workspace"; protected_id.object_id="ancestor"; policy.protected_ancestry.Add(protected_id);
	DataStore empty;
	auto make_capture=[&](EvidenceSourceKind kind,const String& sid) {
		EvidenceCapture c; c.kind=kind; c.workspace_id="test-workspace"; c.source_identity=sid; c.upstream_revision="rev-1"; c.source_at=c.captured_at="2026-01-02T03:30:00+02:00"; c.collector="synthetic"; c.policy_source_ids.Add(sid); return c;
	};
	auto add_location=[](EvidenceCapture& c,const EvidenceLocator& loc,const String& text) { EvidencePinnedLocation p; p.locator=loc; p.content=text; p.source_identity=c.source_identity; p.upstream_revision=c.upstream_revision; c.pinned_locations.Add(p); c.locator=loc; };
	auto preview_value=[](const DataStore& s,const TrustedPolicyRegistry& p,const EvidencePrReviewValue& v) { EvidenceCapture c; if(!Adapt(v,c)) { EvidencePreview r; r.status="invalid"; return r; } return PreviewCapture(s,p,c,EVIDENCE_PR_REVIEW); };
	EvidenceLocator pr; pr.type="pr-review"; pr.id="pr-17"; pr.section="review-3";
	EvidencePrReviewValue prv; prv.capture=make_capture(EVIDENCE_PR_REVIEW,"pr"); prv.capture.locator=pr; EvidencePrReviewEntry review; review.id=pr.id; review.revision=pr.section; review.content="review body https://example.invalid/inert"; review.source_identity="pr"; review.upstream_revision="rev-1"; prv.reviews.Add(review);
	EvidencePreview result=preview_value(empty,policy,prv);
	if(result.status!="ready" || result.resolution!="resolved" || MapGetString(result.snapshot,"content").Find("https://example.invalid/inert")<0) return false;
	EvidenceLocator commit; commit.type="commit-line"; commit.id="abc123"; commit.path="src/file.cpp"; commit.first=4; commit.last=8;
	EvidenceCommitDiffValue cv; cv.capture=make_capture(EVIDENCE_COMMIT_DIFF,"commit"); cv.capture.locator=commit; EvidenceCommitLineEntry line; line.commit_id=commit.id; line.path=commit.path; line.first=commit.first; line.last=commit.last; line.content="diff lines"; line.source_identity="commit"; line.upstream_revision="rev-1"; cv.lines.Add(line);
	EvidenceCapture adapted_commit; if(!Adapt(cv,adapted_commit) || PreviewCapture(empty,policy,adapted_commit,EVIDENCE_COMMIT_DIFF).resolution!="resolved") return false;
	EvidenceLocator message; message.type="message-thread"; message.id="msg-4"; message.section="thread-2";
	EvidenceMessageThreadValue mv; mv.capture=make_capture(EVIDENCE_MESSAGE_THREAD,"message"); mv.capture.locator=message; EvidenceMessageEntry message_entry; message_entry.message_id=message.id; message_entry.thread_id=message.section; message_entry.content="thread text"; message_entry.source_identity="message"; message_entry.upstream_revision="rev-1"; mv.messages.Add(message_entry);
	EvidenceCapture adapted_message; if(!Adapt(mv,adapted_message) || PreviewCapture(empty,policy,adapted_message,EVIDENCE_MESSAGE_THREAD).resolution!="resolved") return false;
	EvidenceLocator paper; paper.type="paper-section"; paper.id="paper-9"; paper.first=12; paper.section="methods";
	EvidencePaperValue pv; pv.capture=make_capture(EVIDENCE_PAPER,"paper"); pv.capture.locator=paper; EvidencePaperSectionEntry section; section.paper_id=paper.id; section.page=paper.first; section.section=paper.section; section.content="paper section"; section.source_identity="paper"; section.upstream_revision="rev-1"; pv.sections.Add(section);
	EvidenceCapture adapted_paper; if(!Adapt(pv,adapted_paper) || PreviewCapture(empty,policy,adapted_paper,EVIDENCE_PAPER).resolution!="resolved") return false;
	EvidencePrReviewValue mismatched; mismatched.capture=prv.capture; mismatched.reviews.Add(review); mismatched.reviews[0].source_identity="pr-17";
	if(preview_value(empty,policy,mismatched).resolution=="resolved") return false;
	mismatched=EvidencePrReviewValue(); mismatched.capture=prv.capture; mismatched.reviews.Add(review); mismatched.reviews[0].upstream_revision="rev-2";
	if(preview_value(empty,policy,mismatched).resolution=="resolved") return false;
	EvidenceCommitDiffValue bad_commit; bad_commit.capture=cv.capture; bad_commit.lines.Add(line); bad_commit.lines[0].upstream_revision="rev-2";
	EvidenceMessageThreadValue bad_message; bad_message.capture=mv.capture; bad_message.messages.Add(message_entry); bad_message.messages[0].source_identity="msg-4";
	EvidencePaperValue bad_paper; bad_paper.capture=pv.capture; bad_paper.sections.Add(section); bad_paper.sections[0].upstream_revision="rev-2";
	EvidenceCapture bad_commit_capture,bad_message_capture,bad_paper_capture;
	if((Adapt(bad_commit,bad_commit_capture) && PreviewCapture(empty,policy,bad_commit_capture,EVIDENCE_COMMIT_DIFF).resolution=="resolved") || (Adapt(bad_message,bad_message_capture) && PreviewCapture(empty,policy,bad_message_capture,EVIDENCE_MESSAGE_THREAD).resolution=="resolved") || (Adapt(bad_paper,bad_paper_capture) && PreviewCapture(empty,policy,bad_paper_capture,EVIDENCE_PAPER).resolution=="resolved")) return false;
	EvidenceLocator l=commit;
	if(!ValidLocator(l,EVIDENCE_COMMIT_DIFF)) return false;
	l.path="../secret"; if(ValidLocator(l,EVIDENCE_COMMIT_DIFF)) return false;
	l=commit; l.path="src/fi\nle.cpp"; if(ValidLocator(l,EVIDENCE_COMMIT_DIFF)) return false;
	EvidenceCapture absent=make_capture(EVIDENCE_COMMIT_DIFF,"commit"); absent.locator=commit;
	if(PreviewCapture(empty,policy,absent,EVIDENCE_COMMIT_DIFF).resolution!="unresolved") return false;
	add_location(absent,commit,"first"); EvidencePinnedLocation duplicate=absent.pinned_locations[0]; duplicate.content="second"; absent.pinned_locations.Add(duplicate);
	if(PreviewCapture(empty,policy,absent,EVIDENCE_COMMIT_DIFF).resolution!="ambiguous") return false;
	EvidenceCapture bad=make_capture(EVIDENCE_COMMIT_DIFF,"commit"); bad.locator=commit; EvidencePinnedLocation malformed; malformed.locator=commit; malformed.content="x"; malformed.locator.first=0; bad.pinned_locations.Add(malformed);
	if(PreviewCapture(empty,policy,bad,EVIDENCE_COMMIT_DIFF).status!="invalid") return false;
	bad=make_capture(EVIDENCE_COMMIT_DIFF,"commit"); bad.locator=commit; malformed.locator=commit; malformed.content=String("bad\0content",11); bad.pinned_locations.Add(malformed);
	if(PreviewCapture(empty,policy,bad,EVIDENCE_COMMIT_DIFF).status!="invalid") return false;
	bad=make_capture(EVIDENCE_COMMIT_DIFF,"commit"); bad.locator=commit; malformed.locator=commit; malformed.locator.last=malformed.locator.first+1001; bad.pinned_locations.Add(malformed);
	if(PreviewCapture(empty,policy,bad,EVIDENCE_COMMIT_DIFF).status!="invalid") return false;
	EvidenceCapture nested=make_capture(EVIDENCE_PR_REVIEW,"leaf"); nested.locator=pr; add_location(nested,pr,"protected content");
	result=PreviewCapture(empty,policy,nested,EVIDENCE_PR_REVIEW);
	if(result.classification!="protected") return false;
	EvidenceCapture mixed=make_capture(EVIDENCE_PR_REVIEW,"pr"); mixed.policy_source_ids.Add("private"); mixed.locator=pr; add_location(mixed,pr,"mixed content");
	if(PreviewCapture(empty,policy,mixed,EVIDENCE_PR_REVIEW).classification!="private") return false;
	EvidenceCapture denied=make_capture(EVIDENCE_PR_REVIEW,"pr"); denied.policy_source_ids.Add("hidden"); denied.locator=pr; add_location(denied,pr,"must-not-leak");
	result=PreviewCapture(empty,policy,denied,EVIDENCE_PR_REVIEW);
	if(result.status!="denied" || !result.snapshot.IsEmpty() || !result.evidence.IsEmpty()) return false;
	TrustedPolicyRegistry unconfigured;
	result=PreviewCapture(empty,unconfigured,prv.capture,EVIDENCE_PR_REVIEW);
	if(result.status!="denied" || !result.snapshot.IsEmpty() || !result.evidence.IsEmpty()) return false;
	EvidenceCapture wrong_workspace=make_capture(EVIDENCE_PR_REVIEW,"pr"); wrong_workspace.workspace_id="other-workspace"; wrong_workspace.locator=pr; add_location(wrong_workspace,pr,"no leak");
	result=PreviewCapture(empty,policy,wrong_workspace,EVIDENCE_PR_REVIEW);
	if(result.status!="denied" || !result.snapshot.IsEmpty()) return false;
	EvidenceCapture retracted=make_capture(EVIDENCE_PR_REVIEW,"pr"); retracted.locator=pr; retracted.retracted=true; add_location(retracted,pr,"retracted bytes");
	if(PreviewCapture(empty,policy,retracted,EVIDENCE_PR_REVIEW).status!="lineage_conflict") return false;
	EvidenceCapture huge=make_capture(EVIDENCE_PR_REVIEW,"pr"); huge.locator=pr; EvidencePinnedLocation oversized; oversized.locator=pr; for(int i=0;i<EvidenceIngestion::MAX_CONTENT_BYTES+1;i++) oversized.content.Cat('x'); huge.pinned_locations.Add(oversized);
	if(PreviewCapture(empty,policy,huge,EVIDENCE_PR_REVIEW).status!="invalid") return false;
	EvidenceCapture too_many=make_capture(EVIDENCE_PR_REVIEW,"pr"); too_many.locator=pr; for(int i=0;i<EvidenceIngestion::MAX_LOCATIONS+1;i++) { EvidencePinnedLocation item; item.locator=pr; too_many.pinned_locations.Add(item); }
	if(PreviewCapture(empty,policy,too_many,EVIDENCE_PR_REVIEW).status!="invalid") return false;
	if(!policy.Validate(error)) { Cout()<<"evidence policy fixture invalid: "<<error<<"\n"; return false; }
	if(!authorization.AllowsWorkspace("test-workspace") || authorization.AllowsWorkspace("other-test-workspace") ||
	   wrong_workspace_authorization.AllowsWorkspace("test-workspace")) return false;
	// This empty in-memory store has no registered source identity. Authorization
	// success is covered by the registered-source T59 integration fixture.
	PolicyAncestryFact cycle; cycle.child.workspace_id=cycle.parent.workspace_id="test-workspace"; cycle.child.object_id="ancestor"; cycle.parent.object_id="leaf"; policy.ancestry.Add(cycle);
	if(PreviewCapture(empty,policy,prv.capture,EVIDENCE_PR_REVIEW).status!="denied") return false;
	policy.ancestry.Remove(policy.ancestry.GetCount()-1);
	String normalized;
	if(!NormalizeUtc("2026-01-02T03:30:00+02:00",normalized) || normalized!="2026-01-02T01:30:00Z") { Cout()<<"evidence test: timezone result="<<normalized<<"\n"; return false; }
	String equivalent;
	if(!NormalizeUtc("2026-01-02T03:30:00.1200+02:00",normalized) || normalized!="2026-01-02T01:30:00.12Z" || !NormalizeUtc("2026-01-02T01:30:00.12Z",equivalent) || equivalent!=normalized) return false;
	if(!NormalizeUtc("2026-01-02T01:30:00.120Z",equivalent) || equivalent!=normalized) return false;
	if(!NormalizeUtc("2026-01-02T01:30:00.121Z",equivalent) || equivalent==normalized) return false;
	if(!NormalizeUtc("2024-03-01T00:15:00+01:00",normalized) || normalized!="2024-02-29T23:15:00Z") return false;
	if(NormalizeUtc("0000-01-01T00:00:00+23:00",normalized) || !NormalizeUtc("0000-01-01T00:00:00-01:00",normalized) || normalized!="0000-01-01T01:00:00Z") return false;
	if(NormalizeUtc("9999-12-31T23:59:59-01:00",normalized) || !NormalizeUtc("9999-12-31T23:59:59+01:00",normalized) || normalized!="9999-12-31T22:59:59Z") return false;
	if(NormalizeUtc("not-a-date",normalized)) { Cout()<<"evidence test: malformed time accepted\n"; return false; }
	if(NormalizeUtc("2026-02-31T00:00:00Z",normalized)) return false;
	return true;
}

END_UPP_NAMESPACE
