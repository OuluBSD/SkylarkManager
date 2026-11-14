#include "ScopedPolicyInternal.h"

NAMESPACE_UPP

bool SafeProviderToken(const String& value)
{
	if(value.IsEmpty() || value.GetCount() > MAX_PROVIDER_TOKEN_LENGTH) return false;
	for(int i = 0; i < value.GetCount(); i++) {
		byte c = value[i];
		if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		     (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
	}
	return true;
}

bool SafeSecretReference(const String& value)
{
	return value.StartsWith("secretref:") && SafeProviderToken(value.Mid(10));
}

bool CanonicalIdentityComponent(const String& value)
{
	if(value.IsEmpty() || value.GetCount() > MAX_POLICY_ID_COMPONENT_LENGTH) return false;
	auto alnum = [](byte c) {
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
	};
	if(!alnum((byte)value[0])) return false;
	for(int i = 1; i < value.GetCount(); i++) {
		byte c = value[i];
		if(!alnum(c) && c != '.' && c != '_' && c != '/' && c != '-') return false;
	}
	return true;
}

bool ValidIdentity(const PolicyIdentity& identity)
{
	return CanonicalIdentityComponent(identity.workspace_id) && CanonicalIdentityComponent(identity.object_id);
}

String PolicyIdentity::Key() const
{
	if(!ValidIdentity(*this)) return String();
	return AsString(workspace_id.GetCount()) + ":" + workspace_id +
	       AsString(object_id.GetCount()) + ":" + object_id;
}

Value PolicyField(const ValueMap& map, const String& key)
{
	int at = map.Find(key);
	return at >= 0 ? map.GetValue(at) : Value();
}

bool ExactFields(const ValueMap& map, const char *const *fields, int count)
{
	if(map.GetCount() != count) return false;
	Vector<int> seen;
	for(int i = 0; i < count; i++) seen.Add(0);
	for(int i = 0; i < map.GetCount(); i++) {
		Value key = map.GetKey(i);
		if(!key.Is<String>()) return false;
		String name = key;
		int expected = -1;
		for(int j = 0; j < count; j++)
			if(name == fields[j]) expected = j;
		if(expected < 0 || seen[expected]) return false;
		seen[expected] = 1;
	}
	return true;
}

bool ReadPolicyIdentity(const Value& value, PolicyIdentity& identity)
{
	static const char *const fields[] = { "workspace_id", "object_id" };
	if(!value.Is<ValueMap>()) return false;
	ValueMap map = value;
	if(!ExactFields(map, fields, 2)) return false;
	Value workspace = PolicyField(map, "workspace_id");
	Value object = PolicyField(map, "object_id");
	if(!workspace.Is<String>() || !object.Is<String>()) return false;
	identity.workspace_id = workspace;
	identity.object_id = object;
	return ValidIdentity(identity);
}

bool ResolveRequestSources(const TrustedPolicyRegistry& registry, const PolicyInput& input,
                           Vector<const PolicySourceFact*>& resolved, EffectivePolicy& result)
{
	Index<String> requested_ids;
	for(const PolicyIdentity& id : input.sources) {
		if(!ValidIdentity(id) || id.workspace_id != input.workspace.workspace_id) {
			result.reason = "identity_missing_or_inconsistent";
			return false;
		}
		if(requested_ids.Find(id.Key()) >= 0) {
			result.reason = "duplicate_requested_source_identity";
			return false;
		}
		requested_ids.Add(id.Key());
		const PolicySourceFact* match = NULL;
		for(const PolicySourceFact& source : registry.sources)
			if(source.identity.Key() == id.Key()) {
				if(match) { result.reason = "ambiguous_source_identity"; return false; }
				match = &source;
		}
		if(!match) { result.reason = "unknown_source_identity"; return false; }
		if(match->workspace.Key() != input.workspace.Key()) {
			result.reason = "source_workspace_identity_mismatch";
			return false;
		}
		resolved.Add(match);
	}
	return true;
}

bool ResolveProviderFact(const TrustedPolicyRegistry& registry, const PolicyInput& input,
                         const PolicyProviderFact*& provider, EffectivePolicy& result)
{
	for(const PolicyProviderFact& fact : registry.providers)
		if(fact.identity.Key() == input.provider.Key()) {
			if(provider) { result.reason = "ambiguous_provider_identity"; return false; }
			provider = &fact;
		}
	if(!provider) { result.reason = "unknown_provider_identity"; return false; }
	if(provider->workspace.Key() != input.workspace.Key()) {
		result.reason = "provider_workspace_identity_mismatch";
		return false;
	}
	return true;
}

END_UPP_NAMESPACE
