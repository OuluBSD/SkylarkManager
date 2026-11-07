#include "PaperHttpTransport.h"
#ifdef PLATFORM_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif

NAMESPACE_UPP

#ifdef PLATFORM_WIN32
static void AppendEnvironmentEntry(Vector<WCHAR>& block,const char *key,const WCHAR *value)
{
	for(const char *p=key;*p;p++) block.Add((WCHAR)*p);
	block.Add('=');
	for(const WCHAR *p=value;*p;p++) block.Add(*p);
	block.Add(0);
}
#endif

static String SanitizedPaperEnvironment()
{
#ifdef PLATFORM_WIN32
	WCHAR windows_dir[32768],temp_dir[32768];
	UINT windows_len=GetWindowsDirectoryW(windows_dir,sizeof(windows_dir)/sizeof(windows_dir[0]));
	DWORD temp_len=GetTempPathW(sizeof(temp_dir)/sizeof(temp_dir[0]),temp_dir);
	if(!windows_len||windows_len>=sizeof(windows_dir)/sizeof(windows_dir[0])||!temp_len||temp_len>=sizeof(temp_dir)/sizeof(temp_dir[0])) return String();
	Vector<WCHAR> block;
	AppendEnvironmentEntry(block,"HOME",temp_dir);
	Vector<WCHAR> path; for(UINT i=0;i<windows_len;i++) path.Add(windows_dir[i]);
	if(windows_len&&windows_dir[windows_len-1]!='\\') path.Add('\\');
	for(const WCHAR *p=L"System32";*p;p++) path.Add(*p);
	path.Add(0); AppendEnvironmentEntry(block,"PATH",path.Begin());
	AppendEnvironmentEntry(block,"SystemRoot",windows_dir);
	AppendEnvironmentEntry(block,"TEMP",temp_dir);
	block.Add(0);
	String env; env.Cat((const char *)block.Begin(),block.GetCount()*sizeof(WCHAR)); return env;
#else
	String env,entry="HOME=/nonexistent"; env.Cat(entry); env.Cat(0);
	entry="PATH=/usr/bin:/bin"; env.Cat(entry); env.Cat(0); env.Cat(0);
	return env;
#endif
}

class LocalPaperHttpRunner : public PaperHttpRunner {
	static void StopAndReap(LocalProcess& process) {
#ifdef PLATFORM_WIN32
		HANDLE handle=process.GetProcessHandle();
		if(handle&&process.IsRunning()) { TerminateProcess(handle,ERROR_CANCELLED); WaitForSingleObject(handle,INFINITE); }
#endif
		process.Kill();
	}
public:
	bool Run(const String& executable, const Vector<String>& args, const String& env, int timeout_ms,
	         int max_output, String& output, bool& timed_out) override {
		output.Clear(); timed_out=false;
		if(env.IsEmpty()) return false;
		LocalProcess process; process.NoConvertCharset();
		if(!process.Start2(executable,args,env.Begin())) return false;
		int started=GetTickCount(); bool overflow=false, killed=false;
		for(;;) {
			String chunk, diagnostics;
			bool active=process.Read2(chunk,diagnostics);
			if(!chunk.IsEmpty() && !overflow) {
				if(chunk.GetCount()>max_output-output.GetCount()) {
					overflow=true; StopAndReap(process); killed=true; output.Clear();
				}
				else output.Cat(chunk);
			}
			if(!process.IsRunning() && !active) break;
			if(!killed && GetTickCount()-started>=timeout_ms) {
				timed_out=true; StopAndReap(process); killed=true; output.Clear();
			}
			Sleep(5);
		}
		if(overflow || timed_out) return false;
		return process.GetExitCode()==0;
	}
};

class IpPaperDnsResolver : public PaperDnsResolver {
public:
	bool Resolve(const String& hostname, Vector<String>& addresses) override {
		addresses.Clear(); IpAddrInfo info;
		info.Start(hostname,443,IpAddrInfo::FAMILY_ANY);
		int started=GetTickCount();
		while(info.InProgress()) { if(GetTickCount()-started>=5000) { info.Clear(); return false; } Sleep(5); }
		if(!info.GetResult()) return false;
		for(addrinfo *ai=info.GetResult(); ai; ai=ai->ai_next) {
			char text[INET6_ADDRSTRLEN]; const void *src=NULL;
			if(ai->ai_family==AF_INET) src=&((sockaddr_in *)ai->ai_addr)->sin_addr;
			else if(ai->ai_family==AF_INET6) src=&((sockaddr_in6 *)ai->ai_addr)->sin6_addr;
			else return false;
#ifdef PLATFORM_WIN32
			if(!InetNtopA(ai->ai_family,(void *)src,text,sizeof(text))) return false;
#else
			if(!inet_ntop(ai->ai_family,src,text,sizeof(text))) return false;
#endif
			addresses.Add(text);
			if(addresses.GetCount()>32) return false;
		}
		return !addresses.IsEmpty();
	}
};

static String PaperAttestationDigest(const String& source, const String& final, const Vector<String>& redirects, const String& body, const String& mime)
{
	String data=source+"\n"+final+"\n";
	for(const String& url:redirects) data<<url<<"\n";
	data<<DataStore::ContentDigest(body)<<"\n"<<mime;
	return DataStore::ContentDigest(data);
}

static bool PaperUrl(const String& url, const PublicPaperSourceRule& rule, String& host, String& path)
{
	if(rule.hostname.IsEmpty()||rule.hostname!=ToLower(rule.hostname)||rule.hostname.Find('.')<0||rule.hostname.Find(':')>=0||rule.hostname.GetCount()>253||rule.max_bytes<=0||rule.path_prefix.IsEmpty()||rule.path_prefix[0]!='/'||rule.path_prefix.Find("//")>=0||rule.path_prefix.Find('%')>=0||rule.path_prefix.Find('\\')>=0||rule.path_prefix.Find('?')>=0||rule.path_prefix.Find('#')>=0) return false;
	for(const String& label:Split(rule.hostname,'.')) {
		if(label.IsEmpty()||label.GetCount()>63||label[0]=='-'||label[label.GetCount()-1]=='-') return false;
		bool numeric=true,hex=label.StartsWith("0x")&&label.GetCount()>2;
		for(int j=0;j<label.GetCount();j++) { char c=label[j]; if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) return false; if(!(c>='0'&&c<='9')) numeric=false; if(j>=2&&!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) hex=false; }
		if(numeric||hex) return false;
	}
	if(rule.hostname=="localhost"||rule.hostname.EndsWith(".localhost")||rule.hostname.EndsWith(".local")||rule.hostname.EndsWith(".internal")||rule.hostname.EndsWith(".home")||rule.hostname.EndsWith(".lan")||rule.hostname.EndsWith(".test")||rule.hostname.EndsWith(".invalid")||rule.hostname.EndsWith(".example")||rule.hostname.EndsWith(".onion")||rule.hostname.EndsWith(".home.arpa")||rule.hostname.EndsWith(".localdomain")||rule.hostname.EndsWith(".intranet")||rule.hostname.EndsWith(".corp")||rule.hostname.EndsWith(".private")) return false;
	for(const String& component:Split(rule.path_prefix,'/')) if(component=="."||component=="..") return false;
	for(int i=0;i<rule.path_prefix.GetCount();i++) if(!((rule.path_prefix[i]>='a'&&rule.path_prefix[i]<='z')||(rule.path_prefix[i]>='A'&&rule.path_prefix[i]<='Z')||(rule.path_prefix[i]>='0'&&rule.path_prefix[i]<='9')||rule.path_prefix[i]=='/'||rule.path_prefix[i]=='-'||rule.path_prefix[i]=='_'||rule.path_prefix[i]=='.'||rule.path_prefix[i]=='~')) return false;
	if(url.GetCount()>2048 || !url.StartsWith("https://")) return false;
	for(int i=0;i<url.GetCount();i++) if((byte)url[i]<=32 || (byte)url[i]>=127 || url[i]=='\\' || url[i]=='#' || url[i]=='?') return false;
	int slash=url.Find('/',8); String authority=slash<0?url.Mid(8):url.Mid(8,slash-8);
	path=slash<0?String("/"):url.Mid(slash);
	if(authority!=rule.hostname || authority.Find('@')>=0 || authority.Find(':')>=0 || authority.Find('%')>=0 || path.Find('%')>=0 || path.Find("//")>=0) return false;
	for(const String& component:Split(path,'/')) if(component=="."||component=="..") return false;
	for(int i=0;i<path.GetCount();i++) if(!((path[i]>='a'&&path[i]<='z')||(path[i]>='A'&&path[i]<='Z')||(path[i]>='0'&&path[i]<='9')||path[i]=='/'||path[i]=='-'||path[i]=='_'||path[i]=='.'||path[i]=='~')) return false;
	bool match=path==rule.path_prefix || (rule.path_prefix=="/" ? true : rule.path_prefix.EndsWith("/") ? path.StartsWith(rule.path_prefix) : path.StartsWith(rule.path_prefix)&&path.GetCount()>rule.path_prefix.GetCount()&&path[rule.path_prefix.GetCount()]=='/');
	if(!match) return false;
	host=authority; return true;
}

static bool IsV4(const String& s, byte out[16]) {
#ifdef PLATFORM_WIN32
	return InetPtonA(AF_INET,s,out)==1;
#else
	return inet_pton(AF_INET,s,out)==1;
#endif
}
static bool IsV6(const String& s, byte out[16]) {
#ifdef PLATFORM_WIN32
	return InetPtonA(AF_INET6,s,out)==1;
#else
	return inet_pton(AF_INET6,s,out)==1;
#endif
}

bool CurlPaperTransport::IsPublicAddress(const String& address)
{
	byte a[16];
	if(IsV4(address,a)) {
		unsigned x=a[0],y=a[1],z=a[2];
		return !(x==0||x==10||x==127||x>=224||(x==100&&y>=64&&y<=127)||(x==169&&y==254)||(x==172&&y>=16&&y<=31)||(x==192&&(y==0||y==168))||(x==192&&y==88&&z==99)||(x==198&&(y==18||y==19||y==51&&z==100))||(x==203&&y==0&&z==113));
	}
	if(IsV6(address,a)) {
		bool mapped=true; for(int i=0;i<10;i++) if(a[i]) mapped=false; mapped=mapped&&a[10]==255&&a[11]==255;
		bool compatible=true; for(int i=0;i<12;i++) if(a[i]) compatible=false;
		bool documentation=a[0]==0x20&&a[1]==0x01&&a[2]==0x0d&&a[3]==0xb8;
		bool transition=(a[0]==0x20&&a[1]==0x02)||(a[0]==0x20&&a[1]==0x01&&a[2]==0&&a[3]==0);
		bool isatap=(a[8]==0&&a[9]==0||a[8]==2&&a[9]==0)&&a[10]==0x5e&&a[11]==0xfe;
		bool reserved_2001=a[0]==0x20&&a[1]==0x01&&a[2]<=1;
		bool nat64=a[0]==0x00&&a[1]==0x64&&a[2]==0xff&&a[3]==0x9b;
		bool documentation3fff=a[0]==0x3f&&a[1]==0xff&&(a[2]&0xf0)==0;
		bool deprecated_6bone=a[0]==0x3f&&a[1]==0xfe;
		return a[0]>=0x20&&a[0]<=0x3f&&!mapped&&!compatible&&!documentation&&!transition&&!isatap&&!reserved_2001&&!nat64&&!documentation3fff&&!deprecated_6bone;
	}
	return false;
}

CurlPaperTransport::CurlPaperTransport(const PublicPaperSourceRule& r,const String& path,PaperHttpRunner& p,PaperDnsResolver& d)
	: rule(r),executable(path),runner(&p),resolver(&d) {}

static LocalPaperHttpRunner& PaperDefaultRunner() { static LocalPaperHttpRunner runner; return runner; }
static IpPaperDnsResolver& PaperDefaultResolver() { static IpPaperDnsResolver resolver; return resolver; }
CurlPaperTransport::CurlPaperTransport(const PublicPaperSourceRule& r,const String& path)
	: rule(r),executable(path),runner(&PaperDefaultRunner()),resolver(&PaperDefaultResolver()) {}
CurlPaperTransport::CurlPaperTransport(const PublicPaperSourceRule& r,const String& path,PaperHttpRunner& p)
	: rule(r),executable(path),runner(&p),resolver(&PaperDefaultResolver()) {}
CurlPaperTransport::CurlPaperTransport(const PublicPaperSourceRule& r,const String& path,PaperDnsResolver& d)
	: rule(r),executable(path),runner(&PaperDefaultRunner()),resolver(&d) {}

static bool ParsePaperHttpResponse(const String& raw, int max_header, int max_body, int& status, String& mime, String& location, String& body)
{
	int end=raw.Find("\r\n\r\n"); if(end<0||end>max_header) return false;
	String header=raw.Left(end); body=raw.Mid(end+4); if(body.GetCount()>max_body) return false;
	Vector<String> lines=Split(header,'\n'); if(lines.IsEmpty()||lines.GetCount()>129) return false;
	auto line_text=[](const String& line)->String { return line.EndsWith("\r")?line.Left(line.GetCount()-1):line; };
	String status_line=line_text(lines[0]); int space=status_line.Find(' ');
	if(space<=0) return false;
	String version=status_line.Left(space);
	if(version!="HTTP/1.0"&&version!="HTTP/1.1"&&version!="HTTP/2"&&version!="HTTP/2.0"&&version!="HTTP/3"&&version!="HTTP/3.0") return false;
	if(status_line.GetCount()<space+4) return false;
	for(int i=space+1;i<space+4;i++) if(status_line[i]<'0'||status_line[i]>'9') return false;
	if(status_line.GetCount()>space+4&&status_line[space+4]!=' ') return false;
	for(int i=0;i<status_line.GetCount();i++) if((byte)status_line[i]<32||(byte)status_line[i]>=127) return false;
	status=atoi(status_line.Mid(space+1,3)); int locations=0,content_types=0,content_lengths=0,transfer_encodings=0;
	int64 content_length=-1; Index<String> header_names;
	for(int i=1;i<lines.GetCount();i++) {
		String line=line_text(lines[i]); int colon=line.Find(':'); if(colon<=0||line[0]==' '||line[0]=='\t') return false;
		for(int k=0;k<colon;k++) { char c=line[k]; if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_')) return false; }
		String key=ToLower(line.Left(colon)); if(header_names.Find(key)>=0) return false; header_names.Add(key);
		String value=TrimBoth(line.Mid(colon+1));
		for(int k=0;k<value.GetCount();k++) if((byte)value[k]<32&&value[k]!='\t'||(byte)value[k]>=127) return false;
		if(key=="location") { locations++; location=value; }
		if(key=="content-type") { if(++content_types>1) return false; int semi=value.Find(';'); mime=ToLower(TrimBoth(semi<0?value:value.Left(semi))); }
		if(key=="content-length") {
			content_lengths++; if(content_lengths>1||value.IsEmpty()) return false;
			content_length=0;
			for(int k=0;k<value.GetCount();k++) { char c=value[k]; if(c<'0'||c>'9'||content_length>(INT64_MAX-(c-'0'))/10) return false; content_length=content_length*10+(c-'0'); }
			if(content_length>max_body) return false;
		}
		if(key=="transfer-encoding") { if(++transfer_encodings>1||ToLower(value)!="chunked") return false; }
	}
	if(content_lengths&&transfer_encodings) return false;
	if(content_lengths&&content_length!=body.GetCount()) return false;
	if(locations>1) return false;
	if((status>=300&&status<400)&&locations!=1) return false;
	return true;
}

static bool ResolveRedirect(const String& current,const String& location,const PublicPaperSourceRule& rule,String& target)
{
	if(location.StartsWith("https://")) target=location;
	else if(location.StartsWith("/")&&!location.StartsWith("//")) target="https://"+rule.hostname+location;
	else if(!location.IsEmpty()&&location[0]!='/'&&location.Find(':')<0) {
		int slash=current.Find('/',8); String parent=slash<0?String("/"):current.Left(current.ReverseFind('/')+1);
		target="https://"+rule.hostname+parent+location;
	}
	else return false;
	String h,p; return PaperUrl(target,rule,h,p);
}

bool CurlPaperTransport::Fetch(const String& initial,PaperTransportResponse& result,String& error) const
{
	error="paper_transport_failed"; if(!runner||!resolver||executable.IsEmpty()) return false;
#ifdef PLATFORM_WIN32
	if(executable.GetCount()<3||executable[1]!=':'||executable[2]!='\\') return false;
#else
	if(executable[0]!='/') return false;
#endif
	String current=initial,host,path; Vector<String> redirects; String body,mime; int fetch_started=GetTickCount();
	for(int hop=0;hop<=3;hop++) {
		int elapsed=GetTickCount()-fetch_started; if(elapsed>=60000) return false;
		if(!PaperUrl(current,rule,host,path)) return false;
	Vector<String> addresses; if(!resolver->Resolve(host,addresses)||addresses.IsEmpty()||addresses.GetCount()>32) return false;
		elapsed=GetTickCount()-fetch_started; if(elapsed>=60000) return false;
	for(const String& address:addresses) if(!IsPublicAddress(address)) return false;
	String pinned=addresses[0]; if(pinned.Find(':')>=0) pinned="["+pinned+"]";
	String pin=host+":443:"+pinned;
	Vector<String> args; args.Add("--disable"); args.Add("--proxy"); args.Add(""); args.Add("--noproxy"); args.Add("*");
	int remaining=60000-elapsed,request_timeout=min(20000,remaining);
	args.Add("--max-redirs"); args.Add("0"); args.Add("--proto"); args.Add("=https"); args.Add("--proto-redir"); args.Add("=https"); args.Add("--connect-timeout"); args.Add("5"); args.Add("--max-time"); args.Add(AsString(max(1,(request_timeout+999)/1000)));
	args.Add("--max-filesize"); args.Add(AsString(min(rule.max_bytes,2*1024*1024))); args.Add("--resolve"); args.Add(pin);
	args.Add("--silent"); args.Add("--show-error"); args.Add("--include"); args.Add("--output"); args.Add("-"); args.Add(current);
	String raw; bool timed=false; int cap=min(rule.max_bytes,2*1024*1024)+16388;
	String env=SanitizedPaperEnvironment();
	if(env.IsEmpty()||!runner->Run(executable,args,env,request_timeout,cap,raw,timed)||timed||GetTickCount()-fetch_started>=60000||raw.GetCount()>cap) return false;
	int status=0; String response_mime,location,response_body;
	if(!ParsePaperHttpResponse(raw,16384,rule.max_bytes,status,response_mime,location,response_body)) return false;
	if(status>=300&&status<400) {
		if(hop>=3) return false;
		String next; if(!ResolveRedirect(current,location,rule,next)) return false;
		redirects.Add(next); current=next; continue;
	}
	if(status!=200||(response_mime!="application/pdf"&&response_mime!="text/plain")) return false;
	body=response_body; mime=response_mime; break;
	}
	if(body.IsEmpty()) return false;
	result.url=initial; result.final_url=current; result.redirects=clone(redirects); result.mime_type=mime; result.body=body;
	String binding=PaperAttestationDigest(initial,current,redirects,body,mime);
	result.attestation=PaperTransportAttestation(binding,"curl-https-v1");
	error.Clear(); return true;
}

bool CurlPaperTransport::RunSyntheticTests()
{
	PublicPaperSourceRule r; r.rule_id="fake"; r.hostname="papers.synthetic-example.org"; r.path_prefix="/approved"; r.max_bytes=1024;
	String h,p;
	PublicPaperSourceRule reserved=r; reserved.hostname="papers.example.invalid";
	PublicPaperSourceRule numeric=r; numeric.hostname="0x7f000001.example.org";
	if(PaperUrl("https://papers.example.invalid/approved/paper",reserved,h,p)||PaperUrl("https://0x7f000001.example.org/approved/paper",numeric,h,p)) return false;
	return PaperUrl("https://papers.synthetic-example.org/approved/paper",r,h,p)&&
		!PaperUrl("https://papers.synthetic-example.org/approved-evil/paper",r,h,p)&&
		!PaperUrl("https://user@papers.synthetic-example.org/approved/paper",r,h,p)&&
		!PaperUrl("https://papers.synthetic-example.org:443/approved/paper",r,h,p)&&
		!PaperUrl("https://papers.synthetic-example.org/approved/%2e%2e/private",r,h,p)&&
		!PaperUrl("https://papers.synthetic-example.org/approved/../private",r,h,p)&&
		!PaperUrl("https://papers.synthetic-example.org/approved\\private",r,h,p)&&
		!PaperUrl("https://papers.synthetic-example.org/approved//private",r,h,p)&&
		!PaperUrl("https://papers.synthetic-example.org/approved/page?q=x",r,h,p)&&
		IsPublicAddress("8.8.8.8")&&!IsPublicAddress("127.0.0.1")&&!IsPublicAddress("10.0.0.1")&&
		!IsPublicAddress("169.254.1.1")&&!IsPublicAddress("192.168.1.1")&&!IsPublicAddress("192.0.2.1")&&
		!IsPublicAddress("192.88.99.1")&&!IsPublicAddress("198.18.0.1")&&!IsPublicAddress("198.51.100.1")&&
		!IsPublicAddress("203.0.113.1")&&!IsPublicAddress("100.64.0.1")&&!IsPublicAddress("100.127.255.254")&&
		!IsPublicAddress("224.0.0.1")&&!IsPublicAddress("239.255.255.255")&&!IsPublicAddress("240.0.0.1")&&!IsPublicAddress("255.255.255.255")&&
		!IsPublicAddress("::1")&&!IsPublicAddress("fe80::1")&&!IsPublicAddress("fc00::1")&&
		!IsPublicAddress("::ffff:8.8.8.8")&&!IsPublicAddress("64:ff9b::808:808")&&!IsPublicAddress("2001:4860:4860::5efe:808:808")&&
		!IsPublicAddress("2001::1")&&!IsPublicAddress("2001:1ff::1")&&!IsPublicAddress("2001:db8::1")&&
		!IsPublicAddress("2002::1")&&!IsPublicAddress("3fff::1")&&IsPublicAddress("2001:4860:4860::8888");
}

END_UPP_NAMESPACE
