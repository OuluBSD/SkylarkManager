#ifndef _ForgeCore_ForgeCore_h_
#define _ForgeCore_ForgeCore_h_

#include <Core/Core.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// ForgeCore: canonical domain suite shared by the GitHub, SourceForge, and
// PrivateHub webapps. Core-only: no Skylark, no network, no persistence
// policy. Adapters implement IForgeBackend per hosting service.
// ---------------------------------------------------------------------------

enum class ForgeBackend { GitHub, SourceForge, PrivateHub };

const char  *ForgeBackendName(ForgeBackend b);
ForgeBackend ForgeBackendFromName(const String& name);

// Backend-qualified reference to one repository/project, optionally to a
// native record inside it (issue id, PR id, ...).
struct ForgeRef : Moveable<ForgeRef> {
	ForgeBackend backend = ForgeBackend::PrivateHub;
	String project;   // owner/repo, sourceforge project slug, local org/name
	String native_id; // "" = the repository itself

	ForgeRef() {}
	ForgeRef(const ForgeRef& o)
		: backend(o.backend), project(o.project), native_id(o.native_id) {}
	ForgeRef& operator=(const ForgeRef& o) {
		if(this != &o) {
			backend = o.backend;
			project = o.project;
			native_id = o.native_id;
		}
		return *this;
	}
	bool operator==(const ForgeRef& o) const {
		return backend == o.backend && project == o.project && native_id == o.native_id;
	}

	void Jsonize(JsonIO& io) {
		String b = io.IsLoading() ? String("") : String(ForgeBackendName(backend));
		io("backend", b)("project", project)("native_id", native_id);
		if(io.IsLoading())
			backend = ForgeBackendFromName(b);
	}
	String ToString() const {
		return String(ForgeBackendName(backend)) + ":" + project + (native_id.GetCount() ? "#" + native_id : String());
	}
};

enum class ForgeProvenance { Remote, LocalPersistent, Snapshot, Synthetic, Unavailable };

const char  *ForgeProvenanceName(ForgeProvenance p);
ForgeProvenance ForgeProvenanceFromName(const String& name);

struct ForgeResult : Moveable<ForgeResult> {
	bool            ok = false;
	String          op;
	ForgeRef        ref;
	String          error;        // diagnostic when !ok
	ForgeProvenance provenance = ForgeProvenance::Unavailable;

	ForgeResult() {}
	ForgeResult(const ForgeResult& o)
		: ok(o.ok), op(o.op), ref(o.ref), error(o.error), provenance(o.provenance) {}
	ForgeResult& operator=(const ForgeResult& o) {
		if(this != &o) {
			ok = o.ok;
			op = o.op;
			ref = o.ref;
			error = o.error;
			provenance = o.provenance;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		String p = io.IsLoading() ? String("") : String(ForgeProvenanceName(provenance));
		io("ok", ok)("op", op)("ref", ref)("error", error)("provenance", p);
		if(io.IsLoading())
			provenance = ForgeProvenanceFromName(p);
	}
};

struct ForgeIssue : Moveable<ForgeIssue> {
	String issue_id;      // native id at the backend
	String project;       // repository/project this issue belongs to
	String title;
	String body;
	String author;
	bool   open = true;
	Time   created;
	Time   updated;
	Vector<String> labels;

	ForgeIssue() {}
	ForgeIssue(const ForgeIssue& o)
		: issue_id(o.issue_id), project(o.project), title(o.title), body(o.body)
		, author(o.author), open(o.open), created(o.created), updated(o.updated)
		, labels(clone(o.labels)) {}
	ForgeIssue& operator=(const ForgeIssue& o) {
		if(this != &o) {
			issue_id = o.issue_id;
			project = o.project;
			title = o.title;
			body = o.body;
			author = o.author;
			open = o.open;
			created = o.created;
			updated = o.updated;
			labels = clone(o.labels);
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("issue_id", issue_id)("project", project)("title", title)("body", body)
		  ("author", author)("open", open)("created", created)("updated", updated)
		  ("labels", labels);
	}
};

struct ForgeComment : Moveable<ForgeComment> {
	String comment_id;
	String issue_id;
	String author;
	String body;
	Time   created;

	ForgeComment() {}
	ForgeComment(const ForgeComment& o)
		: comment_id(o.comment_id), issue_id(o.issue_id)
		, author(o.author), body(o.body), created(o.created) {}
	ForgeComment& operator=(const ForgeComment& o) {
		if(this != &o) {
			comment_id = o.comment_id;
			issue_id = o.issue_id;
			author = o.author;
			body = o.body;
			created = o.created;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("comment_id", comment_id)("issue_id", issue_id)
		  ("author", author)("body", body)("created", created);
	}
};

// Capability flags declared by each backend; unsupported operations must be
// rejected explicitly instead of faked.
struct ForgeCapabilities {
	bool list_issues = false;
	bool get_issue = false;
	bool create_issue = false;
	bool add_comment = false;
	bool set_issue_state = false;
	bool list_comments = false;
};

class IForgeBackend {
public:
	virtual ~IForgeBackend() {}
	virtual String          BackendId() const = 0;
	virtual const ForgeCapabilities& Capabilities() const = 0;

	virtual ForgeResult ListIssues(const String& project, Vector<ForgeIssue>& out) = 0;
	virtual ForgeResult GetIssue(const String& project, const String& issue_id, ForgeIssue& out) = 0;
	virtual ForgeResult CreateIssue(const String& project, const ForgeIssue& draft, ForgeIssue& out) = 0;
	virtual ForgeResult AddComment(const String& project, const String& issue_id,
	                               const ForgeComment& comment, ForgeComment& out) = 0;
	virtual ForgeResult SetIssueState(const String& project, const String& issue_id,
	                                  bool open, ForgeIssue& out) = 0;
	virtual ForgeResult ListComments(const String& project, const String& issue_id,
	                                 Vector<ForgeComment>& out) = 0;

	// Shared result factories (public: free helpers in adapter packages use them).
	static ForgeResult Unsupported(const char *op, const String& project);
	static ForgeResult Failed(const char *op, const String& project, const String& error);
	static ForgeResult Ok(const char *op, const String& project, ForgeProvenance prov);
};

// In-memory reference backend (tests, PrivateHub-style local stores).
class MemoryForgeBackend : public IForgeBackend {
public:
	MemoryForgeBackend(ForgeBackend backend, ForgeProvenance provenance = ForgeProvenance::LocalPersistent);

	String          BackendId() const override { return ForgeBackendName(backend); }
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

private:
	Mutex            mutex;
	ForgeBackend     backend;
	ForgeProvenance  provenance;
	ForgeCapabilities caps;
	VectorMap<String, ForgeIssue>    issues;    // key: project + "\x01" + issue_id
	VectorMap<String, ForgeComment>  comments;  // key: project + "\x01" + issue_id + "\x01" + comment_id
	int              next_issue = 1;
	int              next_comment = 1;
};

END_UPP_NAMESPACE

#endif
