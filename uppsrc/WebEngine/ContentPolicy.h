#ifndef _WebEngine_ContentPolicy_h_
#define _WebEngine_ContentPolicy_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Hierarchical content visibility (task 053). A policy names who may see a piece of content. It is attached
// at three levels (site default, module, record); the most specific explicit policy wins.
//
// Levels:
//   public       anyone, signed in or not
//   protected    named users, groups (roles) or a secret link token; not visible to anyone else
//   private      named users, groups or admins only
//   public-only  anonymous visitors only; hidden from signed-in users
//
// A link token is kept only as a keyed hash. The plain token is never stored, logged or shown after creation.

enum ContentLevel {
	CONTENT_UNSET,
	CONTENT_PUBLIC,
	CONTENT_PROTECTED,
	CONTENT_PRIVATE,
	CONTENT_PUBLIC_ONLY
};

String ContentLevelName(ContentLevel level);
bool   ContentLevelParse(const String& name, ContentLevel& level);

struct ContentGrant : Moveable<ContentGrant> {
	String kind;     // "user" | "group" | "link"
	String value;    // username, group (role) name, or the keyed hash of a link token
	String label;    // a human label for a link; never the token
	bool   revoked = false;
};

struct ContentPolicy : Moveable<ContentPolicy> {
	ContentLevel          level = CONTENT_UNSET;
	Vector<ContentGrant>  grants;

	// Explicit copy support: U++ Vector members are pick-only.
	ContentPolicy() {}
	ContentPolicy(const ContentPolicy& s) { Copy(s); }
	void operator=(const ContentPolicy& s) { Copy(s); }
	void Copy(const ContentPolicy& s) { level = s.level; grants.Clear(); grants.Append(s.grants); }

	ValueMap ToValue() const;
	static ContentPolicy FromValue(const ValueMap& m);
};

// Who is asking. A link token is the plain secret as presented; it is hashed before comparison.
struct ContentPrincipal {
	bool           authenticated = false;
	String         username;
	Vector<String> groups;
	bool           admin = false;
	String         link_token;
};

// The keyed hash stored for a link token.
String ContentLinkHash(const String& token);

// Visibility of content governed by one effective policy.
bool ContentVisible(const ContentPolicy& effective, const ContentPrincipal& who);

// The effective policy for content: the first explicit policy in the chain (record first, then module, then
// site) wins. With no explicit policy, the fallback level applies with no grants.
ContentPolicy ContentResolve(const Vector<ContentPolicy>& chain, ContentLevel fallback);

// The persisted policies of one site: a site default, module policies and record policies. The file is
// versioned, and a missing or unreadable file gives an empty store, which resolves to the fallback.
class ContentPolicyStore {
public:
	static const int VERSION = 1;

	bool Load(const String& data_root, String& error);
	bool Save(String& error) const;

	const ContentPolicy& Site() const { return site; }
	ContentPolicy&       Site()       { return site; }
	ContentPolicy        Module(const String& name) const;
	ContentPolicy        Record(const String& id) const;
	void                 SetModule(const String& name, const ContentPolicy& p);
	void                 SetRecord(const String& id, const ContentPolicy& p);

	// The effective policy for a record: record, then its module, then the site.
	ContentPolicy Effective(const String& module, const String& record_id, ContentLevel fallback) const;

private:
	String                root;
	ContentPolicy         site;
	VectorMap<String, ContentPolicy> modules;
	VectorMap<String, ContentPolicy> records;
};

// The store for a site's data root, loaded on first use and reloaded when the file changes.
ContentPolicyStore& ContentPolicyFor(const String& data_root);

END_UPP_NAMESPACE

#endif
