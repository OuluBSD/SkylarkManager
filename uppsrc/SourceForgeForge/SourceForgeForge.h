#ifndef _SourceForgeForge_SourceForgeForge_h_
#define _SourceForgeForge_SourceForgeForge_h_

#include <Core/Core.h>
#include <ForgeCore/ForgeCore.h>

NAMESPACE_UPP

// Minimal HTTP transport seam: method + path + form-encoded body.
struct SfHttpResponse {
	bool   ok = false;
	int    status = 0;
	String body;
	String error;
};

struct ISfTransport {
	virtual ~ISfTransport() {}
	virtual SfHttpResponse Request(const String& method, const String& path,
	                                const String& form_body) = 0;
};

// Real transport over HttpRequest (SSL-capable). base_url like
// "https://sourceforge.net" (no trailing slash).
class SfHttpTransport : public ISfTransport {
public:
	SfHttpTransport(const String& base_url) : base_url(base_url) {}
	SfHttpResponse Request(const String& method, const String& path,
	                       const String& form_body) override;

private:
	String base_url;
};

// SourceForge project access configuration.
struct SfConfig {
	String project;      // project slug
	String tool_mount = "bugs"; // tracker tool mount name
	String api_key;      // empty = read-only
	String api_token;
	String author = "sf-adapter"; // authoritative local author for writes
};

// Official-SourceForge adapter over the Allura REST API.
class SourceForgeForgeBackend : public IForgeBackend {
public:
	SourceForgeForgeBackend(const SfConfig& config, ISfTransport* transport);
	// Takes ownership of the transport and installs the default
	// HttpRequest-based transport when none is supplied.
	SourceForgeForgeBackend(const SfConfig& config, const String& base_url = "https://sourceforge.net");

	String          BackendId() const override { return "sourceforge"; }
	const ForgeCapabilities& Capabilities() const override { return caps; }

	ForgeResult ListIssues(const String& project, Vector<ForgeIssue>& out) override;
	ForgeResult GetIssue(const String& project, const String& issue_id, ForgeIssue& out) override;
	ForgeResult CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out) override;
	ForgeResult AddComment(const String& project, const String& issue_id,
	                       const ForgeComment& comment, ForgeComment& out) override;
	ForgeResult SetIssueState(const String& project, const String& issue_id,
	                          bool open, ForgeIssue& out) override;
	ForgeResult ListComments(const String& project, const String& issue_id,
	                         Vector<ForgeComment>& out) override;

	static bool      IsValidTicketId(const String& id);
	// Path-safe project slug: [a-z0-9._-], not '.' or '..', at most 64 characters.
	static bool      ValidProject(const String& project);
	static Time      ParseAlluraTime(const String& iso);
	static ForgeIssue TicketFromJson(const ValueMap& ticket, const String& project);
	static void      ThreadPostsFromJson(const Value& thread, Vector<ForgeComment>& out,
	                                     const String& issue_id);
	static String    FormEncode(const VectorMap<String, String>& fields);

private:
	ForgeResult GateWrite(const char *op, const String& project);
	ForgeResult JsonFail(const char *op, const String& project, const String& error);
	ForgeResult TransportFail(const char *op, const String& project, const SfHttpResponse& res);
	// Reads the ticket+thread document for one ticket.
	ForgeResult FetchTicketDoc(const String& project, const String& issue_id, ValueMap& doc);

	SfConfig        cfg;
	One<ISfTransport> transport;
	ForgeCapabilities caps;
};

END_UPP_NAMESPACE

#endif
