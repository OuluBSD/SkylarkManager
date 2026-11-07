#ifndef _ManagerSkylark_PaperHttpTransport_h_
#define _ManagerSkylark_PaperHttpTransport_h_

#include "PaperSourceImport.h"

NAMESPACE_UPP

struct PaperTransportResponse : Moveable<PaperTransportResponse> {
	String url, final_url, mime_type, body;
	Vector<String> redirects;
	PaperTransportAttestation attestation;
};

class PaperHttpRunner {
public:
	virtual bool Run(const String& executable, const Vector<String>& args, const String& env,
	                 int timeout_ms, int max_output, String& output, bool& timed_out) = 0;
	virtual ~PaperHttpRunner() {}
};

class PaperDnsResolver {
public:
	virtual bool Resolve(const String& hostname, Vector<String>& numeric_addresses) = 0;
	virtual ~PaperDnsResolver() {}
};

class CurlPaperTransport {
	PublicPaperSourceRule rule;
	String executable;
	PaperHttpRunner *runner;
	PaperDnsResolver *resolver;
public:
	CurlPaperTransport(const PublicPaperSourceRule& enabled_rule, const String& absolute_curl_path);
	CurlPaperTransport(const PublicPaperSourceRule& enabled_rule, const String& absolute_curl_path, PaperHttpRunner& process_runner);
	CurlPaperTransport(const PublicPaperSourceRule& enabled_rule, const String& absolute_curl_path, PaperDnsResolver& dns_resolver);
	CurlPaperTransport(const PublicPaperSourceRule& enabled_rule, const String& absolute_curl_path, PaperHttpRunner& process_runner, PaperDnsResolver& dns_resolver);
	bool Fetch(const String& url, PaperTransportResponse& result, String& redacted_error) const;
	static bool IsPublicAddress(const String& address);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
