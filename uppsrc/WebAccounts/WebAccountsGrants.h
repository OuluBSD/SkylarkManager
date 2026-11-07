#ifndef _WebAccounts_WebAccountsGrants_h_
#define _WebAccounts_WebAccountsGrants_h_

#include <Core/Core.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// Consent grants: a meta-account user explicitly grants a client webapp (by
// site id + client id) a set of scopes. Requests are pending until the user
// decides; grants are revocable at any time. Persisted per meta-site.
// ---------------------------------------------------------------------------

struct WebAccountGrant : Moveable<WebAccountGrant> {
	String grant_id;        // "wag_" prefixed
	String subject_user_id; // meta-account user granting access
	String client_site;     // requesting site id
	String client_id;       // app identity within the site
	Vector<String> scopes;
	String state;           // "requested" | "granted" | "denied" | "revoked"
	Time   requested_at;
	Time   decided_at;
	Time   expires_at;      // Null = no expiry

	WebAccountGrant() {}
	WebAccountGrant(const WebAccountGrant& o)
		: grant_id(o.grant_id), subject_user_id(o.subject_user_id)
		, client_site(o.client_site), client_id(o.client_id)
		, scopes(clone(o.scopes)), state(o.state)
		, requested_at(o.requested_at), decided_at(o.decided_at), expires_at(o.expires_at) {}
	WebAccountGrant& operator=(const WebAccountGrant& o) {
		if(this != &o) {
			grant_id = o.grant_id;
			subject_user_id = o.subject_user_id;
			client_site = o.client_site;
			client_id = o.client_id;
			scopes = clone(o.scopes);
			state = o.state;
			requested_at = o.requested_at;
			decided_at = o.decided_at;
			expires_at = o.expires_at;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("grant_id", grant_id)("subject_user_id", subject_user_id)
		  ("client_site", client_site)("client_id", client_id)("scopes", scopes)
		  ("state", state)("requested_at", requested_at)("decided_at", decided_at)
		  ("expires_at", expires_at);
	}

	bool HasScopes(const Vector<String>& needed) const {
		for(const String& s : needed)
			if(FindIndex(scopes, s) < 0)
				return false;
		return true;
	}
	bool IsActive() const { return state == "granted"; }
};

class WebGrants {
public:
	WebGrants(const String& site_id, const String& data_root);

	// Creates a pending request; idempotent per (subject, client_site,
	// client_id): a live grant or pending request returns its existing id.
	StoreResult RequestGrant(const String& subject_user_id, const String& client_site,
	                         const String& client_id, const Vector<String>& scopes,
	                         String& grant_id, bool& existed);
	bool        GetGrant(const String& grant_id, WebAccountGrant& out);
	bool        FindActive(const String& subject_user_id, const String& client_site,
	                       const Vector<String>& scopes, WebAccountGrant& out);
	StoreResult ListForSubject(const String& subject_user_id, const String& state_filter,
	                           Vector<WebAccountGrant>& out);
	StoreResult Decide(const String& grant_id, bool approve); // requested -> granted/denied
	StoreResult Revoke(const String& grant_id);               // granted -> revoked
	StoreResult ExpireStale();                                // expiry sweep

private:
	JsonFileStore& Store();

	String      site;
	One<JsonFileStore> grants_store;
	StaticMutex mutex;
};

END_UPP_NAMESPACE

#endif
