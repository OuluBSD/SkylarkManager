#include "WebAccountsGrants.h"

NAMESPACE_UPP

WebGrants::WebGrants(const String& site_id, const String& data_root)
{
	ASSERT_(site_id.GetCount(), "WebGrants needs a site id");
	site = site_id;
	grants_store.Create(site, data_root, "grants");
}

JsonFileStore& WebGrants::Store()
{
	return *grants_store;
}

StoreResult WebGrants::RequestGrant(const String& subject_user_id, const String& client_site,
                                    const String& client_id, const Vector<String>& scopes,
                                    String& grant_id, bool& existed)
{
	return RequestGrantAs(subject_user_id, String(), client_site, client_id, scopes, grant_id, existed);
}

StoreResult WebGrants::RequestGrantAs(const String& subject_user_id, const String& client_user_id,
                                      const String& client_site, const String& client_id,
                                      const Vector<String>& scopes, String& grant_id, bool& existed)
{
	existed = false;
	if(subject_user_id.IsEmpty() || client_site.IsEmpty())
		return StoreResult("grant request needs subject and client site");
	if(scopes.IsEmpty())
		return StoreResult("grant request needs at least one scope");
	mutex.Enter();
	Vector<String> ids;
	Store().List(ids);
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			WebAccountGrant g;
			LoadFromJsonValue(g, v);
			if(g.subject_user_id == subject_user_id && g.client_site == client_site
			&& g.client_id == client_id && g.client_user_id == client_user_id
			&& (g.IsActive() || g.state == "requested")) {
				existed = true;
				grant_id = g.grant_id;
				mutex.Leave();
				return StoreResult(true);
			}
		}
		catch(...) {}
	}
	WebAccountGrant g;
	g.grant_id = "wag_" + AsString(Uuid::Create());
	g.subject_user_id = subject_user_id;
	g.client_site = client_site;
	g.client_id = client_id;
	g.client_user_id = client_user_id;
	g.scopes <<= scopes;
	g.state = "requested";
	g.requested_at = GetSysTime();
	String new_rev;
	StoreResult r = Store().Put(g.grant_id, StoreAsJson(g, true), String(), new_rev);
	if(r.ok)
		grant_id = g.grant_id;
	mutex.Leave();
	return r;
}

bool WebGrants::GetGrant(const String& grant_id, WebAccountGrant& out)
{
	String json, rev;
	if(!Store().Get(grant_id, json, rev).ok)
		return false;
	try {
		Value v = ParseJSON(json);
		if(!v.Is<ValueMap>())
			return false;
		LoadFromJsonValue(out, v);
		return true;
	}
	catch(...) {
		return false;
	}
}

bool WebGrants::FindActive(const String& subject_user_id, const String& client_site,
                           const Vector<String>& scopes, WebAccountGrant& out)
{
	mutex.Enter();
	Vector<String> ids;
	Store().List(ids);
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			WebAccountGrant g;
			LoadFromJsonValue(g, v);
			if(g.subject_user_id == subject_user_id && g.client_site == client_site
			&& g.IsActive() && g.HasScopes(scopes)) {
				out = g;
				mutex.Leave();
				return true;
			}
		}
		catch(...) {}
	}
	mutex.Leave();
	return false;
}

StoreResult WebGrants::ListForSubject(const String& subject_user_id, const String& state_filter,
                                      Vector<WebAccountGrant>& out)
{
	out.Clear();
	mutex.Enter();
	Vector<String> ids;
	Store().List(ids);
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			WebAccountGrant g;
			LoadFromJsonValue(g, v);
			if(g.subject_user_id != subject_user_id)
				continue;
			if(state_filter.GetCount() && g.state != state_filter)
				continue;
			out.Add(g);
		}
		catch(...) {}
	}
	mutex.Leave();
	return StoreResult(true);
}

StoreResult WebGrants::ListForSite(const String& client_site, const String& state_filter,
                                   Vector<WebAccountGrant>& out)
{
	out.Clear();
	mutex.Enter();
	Vector<String> ids;
	Store().List(ids);
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			WebAccountGrant g;
			LoadFromJsonValue(g, v);
			if(g.client_site != client_site)
				continue;
			if(state_filter.GetCount() && g.state != state_filter)
				continue;
			out.Add(g);
		}
		catch(...) {}
	}
	mutex.Leave();
	return StoreResult(true);
}

StoreResult WebGrants::Decide(const String& grant_id, bool approve)
{
	WebAccountGrant g;
	if(!GetGrant(grant_id, g))
		return StoreResult("unknown grant");
	if(g.state != "requested")
		return StoreResult("grant is not pending");
	g.state = approve ? "granted" : "denied";
	g.decided_at = GetSysTime();
	String json, rev;
	Store().Get(grant_id, json, rev);
	return Store().Put(grant_id, StoreAsJson(g, true), rev, rev);
}

StoreResult WebGrants::Revoke(const String& grant_id)
{
	WebAccountGrant g;
	if(!GetGrant(grant_id, g))
		return StoreResult("unknown grant");
	if(g.state != "granted")
		return StoreResult("grant is not active");
	g.state = "revoked";
	g.decided_at = GetSysTime();
	String json, rev;
	Store().Get(grant_id, json, rev);
	return Store().Put(grant_id, StoreAsJson(g, true), rev, rev);
}

StoreResult WebGrants::ExpireStale()
{
	mutex.Enter();
	Time now = GetSysTime();
	Vector<String> ids;
	Store().List(ids);
	int expired = 0;
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			WebAccountGrant g;
			LoadFromJsonValue(g, v);
			if(!IsNull(g.expires_at) && g.expires_at < now && g.IsActive()) {
				g.state = "revoked";
				g.decided_at = now;
				Store().Put(id, StoreAsJson(g, true), rev, rev);
				expired++;
			}
		}
		catch(...) {}
	}
	mutex.Leave();
	return StoreResult(true);
}

END_UPP_NAMESPACE
