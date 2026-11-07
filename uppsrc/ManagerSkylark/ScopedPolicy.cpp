#include "ScopedPolicy.h"

NAMESPACE_UPP

static const int MAX_POLICY_SOURCES = 256;
static const int MAX_POLICY_ANCESTRY_EDGES = 512;
static const int MAX_POLICY_PROVIDERS = 64;
static const int MAX_POLICY_WORKSPACES = 64;
static const int MAX_POLICY_REQUEST_SOURCES = 256;
static const int MAX_POLICY_ANCESTRY_DEPTH = 32;
static const int MAX_POLICY_ID_COMPONENT_LENGTH = 256;
static const int MAX_PROVIDER_TOKEN_LENGTH = 64;

static bool SafeProviderToken(const String& value)
{
	if(value.IsEmpty() || value.GetCount() > MAX_PROVIDER_TOKEN_LENGTH) return false;
	for(int i = 0; i < value.GetCount(); i++) {
		byte c = value[i];
		if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		     (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
	}
	return true;
}

static bool SafeSecretReference(const String& value)
{
	return value.StartsWith("secretref:") && SafeProviderToken(value.Mid(10));
}

static bool CanonicalIdentityComponent(const String& value)
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

static int Strictness(PolicyClassification value)
{
	switch(value) {
	case POLICY_PUBLIC: return 0;
	case POLICY_PRIVATE: return 1;
	case POLICY_PROTECTED: return 2;
	default: return 3;
	}
}

static String ClassificationName(PolicyClassification value)
{
	switch(value) {
	case POLICY_PUBLIC: return "public";
	case POLICY_PRIVATE: return "private";
	case POLICY_PROTECTED: return "protected";
	default: return "unknown";
	}
}

static bool ValidIdentity(const PolicyIdentity& identity)
{
	return CanonicalIdentityComponent(identity.workspace_id) && CanonicalIdentityComponent(identity.object_id);
}

String PolicyIdentity::Key() const
{
	if(!ValidIdentity(*this)) return String();
	return AsString(workspace_id.GetCount()) + ":" + workspace_id +
	       AsString(object_id.GetCount()) + ":" + object_id;
}

bool TrustedPolicyRegistry::Validate(String& error) const
{
	if(!configured) {
		if(sources.GetCount() || ancestry.GetCount() || providers.GetCount() ||
		   workspaces.GetCount() || protected_ancestry.GetCount()) {
			error = "unconfigured_registry_has_facts";
			return false;
		}
		error.Clear();
		return true;
	}
	if(sources.GetCount() > MAX_POLICY_SOURCES || ancestry.GetCount() > MAX_POLICY_ANCESTRY_EDGES ||
	   providers.GetCount() > MAX_POLICY_PROVIDERS || workspaces.GetCount() > MAX_POLICY_WORKSPACES ||
	   protected_ancestry.GetCount() > MAX_POLICY_SOURCES) {
		error = "registry_budget_exceeded";
		return false;
	}
	Index<String> source_ids;
	for(const PolicySourceFact& source : sources) {
		if(!ValidIdentity(source.identity) || Strictness(source.classification) > 2) {
			error = "invalid_source_fact";
			return false;
		}
		if(source_ids.Find(source.identity.Key()) >= 0) {
			error = "ambiguous_source_identity";
			return false;
		}
		source_ids.Add(source.identity.Key());
	}
	Index<String> provider_ids;
	for(const PolicyProviderFact& provider : providers) {
		if(!ValidIdentity(provider.identity) || provider.training_policy != "eligible" &&
		   provider.training_policy != "no_training" && provider.training_policy != "ineligible") {
			error = "invalid_provider_fact";
			return false;
		}
		if(provider.enabled && (!SafeProviderToken(provider.config_revision) ||
	                        !SafeSecretReference(provider.secret_reference))) {
			error = "invalid_enabled_provider_configuration";
			return false;
		}
		if(!provider.config_revision.IsEmpty() && !SafeProviderToken(provider.config_revision)) {
			error = "invalid_provider_config_revision";
			return false;
		}
		if(!provider.secret_reference.IsEmpty() && !SafeSecretReference(provider.secret_reference)) {
			error = "invalid_provider_secret_reference";
			return false;
		}
		if(provider_ids.Find(provider.identity.Key()) >= 0) {
			error = "ambiguous_provider_identity";
			return false;
		}
		provider_ids.Add(provider.identity.Key());
	}
	Index<String> workspace_ids;
	Index<String> workspace_namespaces;
	for(const PolicyWorkspaceFact& workspace : workspaces) {
		if(!ValidIdentity(workspace.identity)) {
			error = "invalid_workspace_identity";
			return false;
		}
		if(workspace_ids.Find(workspace.identity.Key()) >= 0) {
			error = "ambiguous_workspace_identity";
			return false;
		}
		if(workspace_namespaces.Find(workspace.identity.workspace_id) >= 0) {
			error = "workspace_namespace_collision";
			return false;
		}
		workspace_namespaces.Add(workspace.identity.workspace_id);
		workspace_ids.Add(workspace.identity.Key());
	}
	for(const PolicySourceFact& source : sources) {
		bool workspace_found = false;
		for(const PolicyWorkspaceFact& workspace : workspaces)
			if(workspace.identity.Key() == source.workspace.Key() &&
			   source.identity.workspace_id == source.workspace.workspace_id) workspace_found = true;
		if(!workspace_found) {
			error = "source_workspace_unresolved";
			return false;
		}
	}
	for(const PolicyProviderFact& provider : providers) {
		bool workspace_found = false;
		for(const PolicyWorkspaceFact& workspace : workspaces)
			if(workspace.identity.Key() == provider.workspace.Key() &&
			   provider.identity.workspace_id == provider.workspace.workspace_id) workspace_found = true;
		if(!workspace_found) {
			error = "provider_workspace_unresolved";
			return false;
		}
	}
	Vector<int> indegree;
	Vector<int> depth;
	Vector<int> ready;
	Index<String> edge_ids;
	for(int i = 0; i < sources.GetCount(); i++) {
		indegree.Add(0);
		depth.Add(0);
	}
	for(const PolicyAncestryFact& edge : ancestry) {
		int child_index = source_ids.Find(edge.child.Key());
		int parent_index = source_ids.Find(edge.parent.Key());
		if(!ValidIdentity(edge.child) || !ValidIdentity(edge.parent) ||
		   child_index < 0 || parent_index < 0) {
			error = "unresolved_source_ancestry";
			return false;
		}
		if(sources[child_index].workspace.Key() != sources[parent_index].workspace.Key()) {
			error = "source_ancestry_workspace_mismatch";
			return false;
		}
		String edge_key = AsString(edge.child.Key().GetCount()) + ":" + edge.child.Key() + edge.parent.Key();
		if(edge_ids.Find(edge_key) >= 0) {
			error = "duplicate_source_ancestry_edge";
			return false;
		}
		edge_ids.Add(edge_key);
		indegree[child_index]++;
	}
	for(int i = 0; i < indegree.GetCount(); i++)
		if(indegree[i] == 0) ready.Add(i);
	int processed = 0;
	for(int at = 0; at < ready.GetCount(); at++) {
		int parent = ready[at];
		processed++;
		for(const PolicyAncestryFact& edge : ancestry) {
			if(source_ids.Find(edge.parent.Key()) != parent) continue;
			int child = source_ids.Find(edge.child.Key());
			depth[child] = max(depth[child], depth[parent] + 1);
			if(depth[child] > MAX_POLICY_ANCESTRY_DEPTH) {
				error = "source_ancestry_depth_exceeded";
				return false;
			}
			if(--indegree[child] == 0) ready.Add(child);
		}
	}
	if(processed != sources.GetCount()) {
		error = "cyclic_source_ancestry";
		return false;
	}
	Index<String> protected_ids;
	for(const PolicyIdentity& protected_id : protected_ancestry) {
		if(!ValidIdentity(protected_id) || source_ids.Find(protected_id.Key()) < 0) {
			error = "unresolved_protected_ancestry";
			return false;
		}
		if(protected_ids.Find(protected_id.Key()) >= 0) {
			error = "duplicate_protected_ancestry";
			return false;
		}
		protected_ids.Add(protected_id.Key());
	}
	error.Clear();
	return true;
}

static Value PolicyField(const ValueMap& map, const String& key)
{
	int at = map.Find(key);
	return at >= 0 ? map.GetValue(at) : Value();
}

static bool ExactFields(const ValueMap& map, const char *const *fields, int count)
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

static bool ReadPolicyIdentity(const Value& value, PolicyIdentity& identity)
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

bool TrustedPolicyRegistry::LoadValue(const Value& value, TrustedPolicyRegistry& target, String& error)
{
	static const char *const root_fields[] = {
		"version", "workspaces", "sources", "ancestry", "providers", "protected_ancestry"
	};
	static const char *const source_fields[] = { "workspace", "identity", "classification", "visible" };
	static const char *const edge_fields[] = { "child", "parent" };
	static const char *const provider_fields[] = { "workspace", "identity", "training_policy", "enabled", "config_revision", "secret_reference" };
	if(!value.Is<ValueMap>()) { error = "policy_root_must_be_map"; return false; }
	ValueMap root = value;
	if(!ExactFields(root, root_fields, 6)) { error = "policy_root_fields_invalid"; return false; }
	Value version = PolicyField(root, "version");
	Value workspaces_value = PolicyField(root, "workspaces");
	Value sources_value = PolicyField(root, "sources");
	Value ancestry_value = PolicyField(root, "ancestry");
	Value providers_value = PolicyField(root, "providers");
	Value protected_value = PolicyField(root, "protected_ancestry");
	if(!version.Is<String>() || (String)version != "trusted-policy-v2" ||
	   !workspaces_value.Is<ValueArray>() || !sources_value.Is<ValueArray>() ||
	   !ancestry_value.Is<ValueArray>() || !providers_value.Is<ValueArray>() ||
	   !protected_value.Is<ValueArray>()) {
		error = "policy_root_types_invalid";
		return false;
	}
	ValueArray workspaces = workspaces_value, sources = sources_value, ancestry = ancestry_value;
	ValueArray providers = providers_value, protected_ancestry = protected_value;
	if(workspaces.GetCount() > MAX_POLICY_WORKSPACES || sources.GetCount() > MAX_POLICY_SOURCES ||
	   ancestry.GetCount() > MAX_POLICY_ANCESTRY_EDGES || providers.GetCount() > MAX_POLICY_PROVIDERS ||
	   protected_ancestry.GetCount() > MAX_POLICY_SOURCES) {
		error = "registry_budget_exceeded";
		return false;
	}
	TrustedPolicyRegistry candidate;
	candidate.configured = true;
	for(const Value& item : workspaces) {
		PolicyWorkspaceFact fact;
		if(!ReadPolicyIdentity(item, fact.identity)) { error = "workspace_fact_invalid"; return false; }
		candidate.workspaces.Add(fact);
	}
	for(const Value& item : sources) {
		if(!item.Is<ValueMap>()) { error = "source_fact_must_be_map"; return false; }
		ValueMap map = item;
		if(!ExactFields(map, source_fields, 4)) { error = "source_fact_fields_invalid"; return false; }
		PolicySourceFact fact;
		Value classification = PolicyField(map, "classification");
		Value visible = PolicyField(map, "visible");
		if(!ReadPolicyIdentity(PolicyField(map, "workspace"), fact.workspace) ||
		   !ReadPolicyIdentity(PolicyField(map, "identity"), fact.identity) ||
		   !classification.Is<String>() || !visible.Is<bool>()) {
			error = "source_fact_types_invalid";
			return false;
		}
		String name = classification;
		if(name == "public") fact.classification = POLICY_PUBLIC;
		else if(name == "private") fact.classification = POLICY_PRIVATE;
		else if(name == "protected") fact.classification = POLICY_PROTECTED;
		else { error = "source_classification_invalid"; return false; }
		fact.visible = visible;
		candidate.sources.Add(fact);
	}
	for(const Value& item : ancestry) {
		if(!item.Is<ValueMap>()) { error = "ancestry_fact_must_be_map"; return false; }
		ValueMap map = item;
		PolicyAncestryFact edge;
		if(!ExactFields(map, edge_fields, 2) ||
		   !ReadPolicyIdentity(PolicyField(map, "child"), edge.child) ||
		   !ReadPolicyIdentity(PolicyField(map, "parent"), edge.parent)) {
			error = "ancestry_fact_invalid";
			return false;
		}
		candidate.ancestry.Add(edge);
	}
	for(const Value& item : providers) {
		if(!item.Is<ValueMap>()) { error = "provider_fact_must_be_map"; return false; }
		ValueMap map = item;
		PolicyProviderFact fact;
		Value training = PolicyField(map, "training_policy");
		Value enabled = PolicyField(map, "enabled");
		Value revision = PolicyField(map, "config_revision");
		Value reference = PolicyField(map, "secret_reference");
		if(!ExactFields(map, provider_fields, 6) ||
		   !ReadPolicyIdentity(PolicyField(map, "workspace"), fact.workspace) ||
		   !ReadPolicyIdentity(PolicyField(map, "identity"), fact.identity) || !training.Is<String>() ||
		   !enabled.Is<bool>() || !revision.Is<String>() || !reference.Is<String>()) {
			error = "provider_fact_invalid";
			return false;
		}
		fact.training_policy = training;
		fact.enabled = enabled;
		fact.config_revision = revision;
		fact.secret_reference = reference;
		candidate.providers.Add(fact);
	}
	for(const Value& item : protected_ancestry) {
		PolicyIdentity identity;
		if(!ReadPolicyIdentity(item, identity)) { error = "protected_ancestry_invalid"; return false; }
		candidate.protected_ancestry.Add(identity);
	}
	if(!candidate.Validate(error)) return false;
	target.configured = candidate.configured;
	target.sources = pick(candidate.sources);
	target.ancestry = pick(candidate.ancestry);
	target.providers = pick(candidate.providers);
	target.workspaces = pick(candidate.workspaces);
	target.protected_ancestry = pick(candidate.protected_ancestry);
	error.Clear();
	return true;
}

PolicySettings ScopedPolicyService::Defaults()
{
	return PolicySettings();
}

bool ScopedPolicyService::Validate(const PolicySettings& settings, String& error)
{
	if(!settings.review_granularity.IsEmpty() && settings.review_granularity != "goal" &&
	   settings.review_granularity != "milestone" && settings.review_granularity != "record") {
		error = "invalid_review_granularity";
		return false;
	}
	error.Clear();
	return true;
}

ValueMap EffectivePolicy::ToRedactedValue() const
{
	ValueMap result;
	result.Add("status", status);
	result.Add("origin", origin);
	result.Add("reason", reason);
	result.Add("classification", classification);
	result.Add("publication_allowed", publication_allowed);
	result.Add("source_visible", source_visible);
	result.Add("external_model_allowed", external_model_allowed);
	result.Add("review_confirmed", review_confirmed);
	result.Add("review_granularity", review_granularity);
	result.Add("secret_state", secret_state);
	return result;
}

EffectivePolicy ScopedPolicyService::Resolve(const PolicySettings& instance,
                                             const PolicySettings& workspace,
                                             const PolicySettings& operator_preferences,
                                             const TrustedPolicyRegistry& registry,
                                             const PolicyInput& input)
{
	EffectivePolicy result;
	result.status = "unconfigured";
	result.origin = "default";
	result.reason = "policy_not_configured";
	result.secret_state = "not_set";
	result.review_granularity = "unconfirmed";
	if(input.action != POLICY_ACTION_MODEL && input.action != POLICY_ACTION_PUBLICATION) {
		result.status = "invalid";
		result.reason = "invalid_policy_action";
		return result;
	}
	if(!registry.configured || !instance.configured) return result;
	String error;
	if(!registry.Validate(error) || !Validate(instance, error) ||
	   (workspace.configured && !Validate(workspace, error)) ||
	   (operator_preferences.configured && !Validate(operator_preferences, error))) {
		result.status = "invalid";
		result.reason = "invalid_trusted_policy";
		return result;
	}
	if(!ValidIdentity(input.workspace) || input.sources.IsEmpty() ||
	   input.sources.GetCount() > MAX_POLICY_REQUEST_SOURCES ||
	   !ValidIdentity(input.provider) || input.provider.workspace_id != input.workspace.workspace_id) {
		result.reason = "identity_missing_or_inconsistent";
		return result;
	}
	bool workspace_known = false;
	for(const PolicyWorkspaceFact& fact : registry.workspaces)
		if(fact.identity.Key() == input.workspace.Key()) workspace_known = true;
	if(!workspace_known) { result.reason = "unknown_workspace_identity"; return result; }
	Vector<const PolicySourceFact*> resolved;
	Index<String> requested_ids;
	for(const PolicyIdentity& id : input.sources) {
		if(!ValidIdentity(id) || id.workspace_id != input.workspace.workspace_id) {
			result.reason = "identity_missing_or_inconsistent";
			return result;
		}
		if(requested_ids.Find(id.Key()) >= 0) {
			result.reason = "duplicate_requested_source_identity";
			return result;
		}
		requested_ids.Add(id.Key());
		const PolicySourceFact* match = NULL;
		for(const PolicySourceFact& source : registry.sources)
			if(source.identity.Key() == id.Key()) {
				if(match) { result.reason = "ambiguous_source_identity"; return result; }
				match = &source;
		}
		if(!match) { result.reason = "unknown_source_identity"; return result; }
		if(match->workspace.Key() != input.workspace.Key()) {
			result.reason = "source_workspace_identity_mismatch";
			return result;
		}
		resolved.Add(match);
	}
	const PolicyProviderFact* provider = NULL;
	for(const PolicyProviderFact& fact : registry.providers)
		if(fact.identity.Key() == input.provider.Key()) {
			if(provider) { result.reason = "ambiguous_provider_identity"; return result; }
			provider = &fact;
		}
	if(!provider) { result.reason = "unknown_provider_identity"; return result; }
	if(provider->workspace.Key() != input.workspace.Key()) {
		result.reason = "provider_workspace_identity_mismatch";
		return result;
	}
	result.provider_config_revision = provider->config_revision;
	result.secret_state = provider->secret_reference.IsEmpty() ? "missing_reference" : "referenced_redacted";
	PolicyClassification effective = POLICY_PUBLIC;
	bool visible = true;
	bool protected_source = false;
	Index<String> visited;
	Vector<const PolicySourceFact*> pending;
	Vector<int> pending_depth;
	for(const PolicySourceFact* source : resolved) {
		pending.Add(source);
		pending_depth.Add(0);
	}
	for(int at = 0; at < pending.GetCount(); at++) {
		const PolicySourceFact* current = pending[at];
		int depth = pending_depth[at];
		if(visited.Find(current->identity.Key()) >= 0) continue;
		visited.Add(current->identity.Key());
		visible &= current->visible;
		if(Strictness(current->classification) > Strictness(effective)) effective = current->classification;
		for(const PolicyIdentity& protected_id : registry.protected_ancestry)
			if(protected_id.Key() == current->identity.Key()) protected_source = true;
		for(const PolicyAncestryFact& edge : registry.ancestry) {
			if(edge.child.Key() != current->identity.Key()) continue;
			if(depth >= MAX_POLICY_ANCESTRY_DEPTH) {
				result.status = "invalid";
				result.reason = "source_ancestry_depth_exceeded";
				return result;
			}
			const PolicySourceFact* parent = NULL;
			for(const PolicySourceFact& candidate : registry.sources)
				if(candidate.identity.Key() == edge.parent.Key()) parent = &candidate;
			if(!parent) { result.reason = "inconsistent_source_ancestry"; return result; }
			pending.Add(parent);
			pending_depth.Add(depth + 1);
		}
	}
	if(protected_source) effective = POLICY_PROTECTED;
	result.status = "evaluated";
	result.origin = "trusted_registry";
	result.classification = ClassificationName(effective);
	result.source_visible = visible;
	bool export_allowed = instance.export_enabled && (!workspace.configured || workspace.export_enabled) &&
	                      (!operator_preferences.configured || operator_preferences.export_enabled);
	bool model_enabled = instance.remote_enabled && (!workspace.configured || workspace.remote_enabled) &&
	                     (!operator_preferences.configured || operator_preferences.remote_enabled);
	result.publication_allowed = input.action == POLICY_ACTION_PUBLICATION && visible && export_allowed &&
	                             effective == POLICY_PUBLIC;
	bool training_allows = (effective == POLICY_PUBLIC && provider->training_policy == "eligible") ||
	                       (effective == POLICY_PROTECTED && provider->training_policy == "no_training");
	result.external_model_allowed = input.action == POLICY_ACTION_MODEL && provider->enabled &&
	                               SafeProviderToken(provider->config_revision) &&
	                               SafeSecretReference(provider->secret_reference) && visible && model_enabled &&
	                               training_allows && (effective == POLICY_PUBLIC || effective == POLICY_PROTECTED);
	if(!instance.secret_reference.IsEmpty()) result.secret_state = "referenced_redacted";
	// Review strictness is goal < milestone < record; narrower scopes can
	// strengthen but never weaken a higher-scope requirement.
	auto review_rank = [](const String& granularity) {
		if(granularity == "goal") return 1;
		if(granularity == "milestone") return 2;
		if(granularity == "record") return 3;
		return 0;
	};
	if(review_rank(instance.review_granularity) > 0) {
		result.review_granularity = instance.review_granularity;
		result.review_confirmed = true;
		result.origin = "instance";
	}
	if(workspace.configured && review_rank(workspace.review_granularity) > review_rank(result.review_granularity)) {
		result.review_granularity = workspace.review_granularity;
		result.review_confirmed = true;
		result.origin = "workspace";
	}
	if(operator_preferences.configured && review_rank(operator_preferences.review_granularity) > review_rank(result.review_granularity)) {
		result.review_granularity = operator_preferences.review_granularity;
		result.review_confirmed = true;
		result.origin = "operator";
	}
	if(!visible) result.reason = "source_not_visible";
	else if(effective == POLICY_PRIVATE || effective == POLICY_UNKNOWN) result.reason = "classification_ineligible";
	else if(!training_allows && input.action == POLICY_ACTION_MODEL) result.reason = "provider_not_eligible";
	else if(input.action == POLICY_ACTION_MODEL && !provider->enabled) result.reason = "provider_disabled";
	else if(input.action == POLICY_ACTION_MODEL && !SafeProviderToken(provider->config_revision)) result.reason = "provider_revision_missing";
	else if(input.action == POLICY_ACTION_MODEL && !SafeSecretReference(provider->secret_reference)) result.reason = "provider_reference_missing";
	else if(input.action == POLICY_ACTION_PUBLICATION && !export_allowed) result.reason = "publication_disabled";
	else result.reason = "policy_resolved";
	return result;
}

bool ScopedPolicyService::RunSyntheticTests()
{
	PolicyIdentity collision_a;
	collision_a.workspace_id = "a";
	collision_a.object_id = "b:c";
	PolicyIdentity collision_b;
	collision_b.workspace_id = "a:b";
	collision_b.object_id = "c";
	if(!collision_a.Key().IsEmpty() || !collision_b.Key().IsEmpty()) return false;
	PolicyIdentity canonical_id;
	canonical_id.workspace_id = "repo/main.v1";
	canonical_id.object_id = "record-1_a/sub";
	if(canonical_id.Key().IsEmpty()) return false;
	String max_component;
	for(int i = 0; i < MAX_POLICY_ID_COMPONENT_LENGTH; i++) max_component += "x";
	canonical_id.object_id = max_component;
	if(canonical_id.Key().IsEmpty()) return false;
	String overlong_component = max_component + "x";
	canonical_id.workspace_id = overlong_component;
	if(!canonical_id.Key().IsEmpty()) return false;
	canonical_id.workspace_id = "valid-workspace";
	canonical_id.object_id = overlong_component;
	if(!canonical_id.Key().IsEmpty()) return false;
	canonical_id.workspace_id = " leading-workspace";
	canonical_id.object_id = "valid-object";
	if(!canonical_id.Key().IsEmpty()) return false;
	canonical_id.workspace_id = "valid-workspace";
	canonical_id.object_id = " leading-space";
	if(!canonical_id.Key().IsEmpty()) return false;
	canonical_id.object_id = "control\tchar";
	if(!canonical_id.Key().IsEmpty()) return false;
	canonical_id.object_id = "colon:separator";
	if(!canonical_id.Key().IsEmpty()) return false;
	TrustedPolicyRegistry registry;
	String error;
	if(!registry.Validate(error) || !error.IsEmpty()) return false;
	auto identity_value = [](const PolicyIdentity& identity) {
		ValueMap map;
		map.Add("workspace_id", identity.workspace_id);
		map.Add("object_id", identity.object_id);
		return Value(map);
	};
	PolicyIdentity workspace_identity;
	workspace_identity.workspace_id = "parser-workspace-id";
	workspace_identity.object_id = "workspace-a";
	PolicyIdentity source_identity;
	source_identity.workspace_id = workspace_identity.workspace_id;
	source_identity.object_id = "parser-source";
	PolicyIdentity provider_identity;
	provider_identity.workspace_id = workspace_identity.workspace_id;
	provider_identity.object_id = "parser-provider";
	auto registry_value = [&](const Value& visible, bool duplicate_field, bool omit_protected) {
		ValueMap workspace_fact = identity_value(workspace_identity);
		ValueArray workspaces; workspaces.Add(workspace_fact);
		ValueMap source_fact;
		source_fact.Add("workspace", identity_value(workspace_identity));
		source_fact.Add("identity", identity_value(source_identity));
		source_fact.Add("classification", "public");
		source_fact.Add("visible", visible);
		if(duplicate_field) source_fact.Add("visible", visible);
		ValueArray sources; sources.Add(source_fact);
		ValueMap provider_fact;
		provider_fact.Add("workspace", identity_value(workspace_identity));
		provider_fact.Add("identity", identity_value(provider_identity));
		provider_fact.Add("training_policy", "eligible");
		provider_fact.Add("enabled", true);
		provider_fact.Add("config_revision", "synthetic-r1");
		provider_fact.Add("secret_reference", "secretref:synthetic-provider");
		ValueArray providers; providers.Add(provider_fact);
		ValueMap root;
		root.Add("version", "trusted-policy-v2");
		root.Add("workspaces", workspaces);
		root.Add("sources", sources);
		root.Add("ancestry", ValueArray());
		root.Add("providers", providers);
		if(!omit_protected) root.Add("protected_ancestry", ValueArray());
		return root;
	};
	TrustedPolicyRegistry parsed_registry;
	ValueMap parser_input = registry_value(true, false, false);
	if(!TrustedPolicyRegistry::LoadValue(parser_input, parsed_registry, error) ||
	   !parsed_registry.configured || parsed_registry.sources.GetCount() != 1 ||
	   parsed_registry.sources[0].workspace.Key() != workspace_identity.Key()) return false;
	ValueMap old_version = parser_input; old_version.Set("version", "trusted-policy-v1");
	if(TrustedPolicyRegistry::LoadValue(old_version, parsed_registry, error)) return false;
	ValueArray parser_providers = PolicyField(parser_input, "providers");
	ValueMap bad_provider = parser_providers[0];
	bad_provider.Set("enabled", "true"); parser_providers.Set(0, bad_provider);
	ValueMap wrong_type = parser_input; wrong_type.Set("providers", parser_providers);
	if(TrustedPolicyRegistry::LoadValue(wrong_type, parsed_registry, error)) return false;
	parser_providers = PolicyField(parser_input, "providers"); bad_provider = parser_providers[0];
	bad_provider.Set("secret_reference", "sk-live-secret-marker"); parser_providers.Set(0, bad_provider);
	ValueMap secret_like = parser_input; secret_like.Set("providers", parser_providers);
	if(TrustedPolicyRegistry::LoadValue(secret_like, parsed_registry, error)) return false;
	ValueMap malformed_root = registry_value("true", false, false);
	int original_parsed_source_count = parsed_registry.sources.GetCount();
	if(TrustedPolicyRegistry::LoadValue(malformed_root, parsed_registry, error) ||
	   !parsed_registry.configured || parsed_registry.sources.GetCount() != 1 ||
	   parsed_registry.sources[0].identity.object_id != source_identity.object_id) return false;
	ValueMap duplicate_field_root = registry_value(true, true, false);
	if(TrustedPolicyRegistry::LoadValue(duplicate_field_root, parsed_registry, error)) return false;
	ValueMap missing_field_root = registry_value(true, false, true);
	if(TrustedPolicyRegistry::LoadValue(missing_field_root, parsed_registry, error)) return false;
	ValueMap unknown_field_root = registry_value(true, false, false);
	unknown_field_root.Add("unexpected", true);
	if(TrustedPolicyRegistry::LoadValue(unknown_field_root, parsed_registry, error) ||
	   parsed_registry.sources.GetCount() != original_parsed_source_count) return false;
	ValueMap parser_budget_root = registry_value(true, false, false);
	ValueArray parser_budget_sources = PolicyField(parser_budget_root, "sources");
	Value source_template = parser_budget_sources[0];
	for(int i = 0; i < MAX_POLICY_SOURCES; i++) parser_budget_sources.Add(source_template);
	parser_budget_root.Set("sources", parser_budget_sources);
	if(TrustedPolicyRegistry::LoadValue(parser_budget_root, parsed_registry, error) ||
	   error != "registry_budget_exceeded" || parsed_registry.sources.GetCount() != original_parsed_source_count) return false;
	workspace_identity.object_id = overlong_component;
	ValueMap long_workspace_root = registry_value(true, false, false);
	if(TrustedPolicyRegistry::LoadValue(long_workspace_root, parsed_registry, error)) return false;
	workspace_identity.object_id = "workspace-a";
	source_identity.object_id = overlong_component;
	ValueMap long_source_root = registry_value(true, false, false);
	if(TrustedPolicyRegistry::LoadValue(long_source_root, parsed_registry, error)) return false;
	source_identity.object_id = "parser-source";
	PolicyInput input;
	input.workspace.workspace_id = "synthetic-workspace";
	input.workspace.object_id = "workspace";
	input.provider.workspace_id = input.workspace.workspace_id;
	input.provider.object_id = "provider";
	input.sources.Add(PolicyIdentity());
	if(Resolve(PolicySettings(), PolicySettings(), PolicySettings(), registry, input).status != "unconfigured") return false;

	registry.configured = true;
	PolicyWorkspaceFact registered_workspace;
	registered_workspace.identity = input.workspace;
	registry.workspaces.Add(registered_workspace);
	PolicyWorkspaceFact colliding_namespace;
	colliding_namespace.identity.workspace_id = input.workspace.workspace_id;
	colliding_namespace.identity.object_id = "different-workspace-object";
	registry.workspaces.Add(colliding_namespace);
	if(registry.Validate(error) || error != "workspace_namespace_collision") return false;
	registry.workspaces.Remove(registry.workspaces.GetCount() - 1);
	PolicySourceFact public_source;
	public_source.workspace = registered_workspace.identity;
	public_source.identity.workspace_id = input.workspace.workspace_id;
	public_source.identity.object_id = "public";
	public_source.classification = POLICY_PUBLIC;
	public_source.visible = true;
	registry.sources.Add(public_source);
	PolicySourceFact private_source;
	private_source.workspace = registered_workspace.identity;
	private_source.identity.workspace_id = input.workspace.workspace_id;
	private_source.identity.object_id = "private";
	private_source.classification = POLICY_PRIVATE;
	private_source.visible = true;
	registry.sources.Add(private_source);
	PolicySourceFact protected_parent;
	protected_parent.workspace = registered_workspace.identity;
	protected_parent.identity.workspace_id = input.workspace.workspace_id;
	protected_parent.identity.object_id = "protected-parent";
	protected_parent.classification = POLICY_PROTECTED;
	protected_parent.visible = true;
	registry.sources.Add(protected_parent);
	PolicySourceFact descendant;
	descendant.workspace = registered_workspace.identity;
	descendant.identity.workspace_id = input.workspace.workspace_id;
	descendant.identity.object_id = "descendant";
	descendant.classification = POLICY_PUBLIC;
	descendant.visible = true;
	registry.sources.Add(descendant);
	PolicySourceFact intermediate;
	intermediate.workspace = registered_workspace.identity;
	intermediate.identity.workspace_id = input.workspace.workspace_id;
	intermediate.identity.object_id = "intermediate";
	intermediate.classification = POLICY_PUBLIC;
	intermediate.visible = true;
	registry.sources.Add(intermediate);
	PolicyAncestryFact lineage;
	lineage.child = descendant.identity;
	lineage.parent = protected_parent.identity;
	registry.ancestry.Add(lineage);
	PolicyAncestryFact second_parent;
	second_parent.child = descendant.identity;
	second_parent.parent = intermediate.identity;
	registry.ancestry.Add(second_parent);
	PolicyAncestryFact multi_level;
	multi_level.child = intermediate.identity;
	multi_level.parent = private_source.identity;
	registry.ancestry.Add(multi_level);
	PolicySourceFact hidden;
	hidden.workspace = registered_workspace.identity;
	hidden.identity.workspace_id = input.workspace.workspace_id;
	hidden.identity.object_id = "hidden";
	hidden.classification = POLICY_PUBLIC;
	hidden.visible = false;
	registry.sources.Add(hidden);
	registry.protected_ancestry.Add(protected_parent.identity);
	PolicyProviderFact eligible;
	eligible.workspace = registered_workspace.identity;
	eligible.identity = input.provider;
	eligible.training_policy = "eligible";
	eligible.enabled = true;
	eligible.config_revision = "synthetic-r1";
	eligible.secret_reference = "secretref:synthetic-provider";
	registry.providers.Add(eligible);
	PolicySettings instance;
	instance.configured = true;
	instance.remote_enabled = true;
	instance.export_enabled = true;
	PolicySettings workspace;
	workspace.configured = true;
	workspace.remote_enabled = true;
	workspace.export_enabled = true;
	PolicySettings operator_preferences = workspace;
	input.sources.Clear();
	input.sources.Add(public_source.identity);
	if(!registry.Validate(error)) return false;
	input.action = POLICY_ACTION_MODEL;
	input.action = (PolicyAction)99;
	EffectivePolicy invalid_action = Resolve(instance, workspace, operator_preferences, registry, input);
	if(invalid_action.status != "invalid" || invalid_action.reason != "invalid_policy_action" ||
	   invalid_action.external_model_allowed || invalid_action.publication_allowed) return false;
	input.action = POLICY_ACTION_MODEL;
	EffectivePolicy initial_public = Resolve(instance, workspace, operator_preferences, registry, input);
	if(initial_public.status != "evaluated" || !initial_public.external_model_allowed ||
	   initial_public.provider_config_revision != "synthetic-r1") return false;
	PolicyProviderFact disabled_provider = eligible; disabled_provider.enabled = false;
	registry.providers.Clear(); registry.providers.Add(disabled_provider);
	if(Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	PolicyProviderFact missing_revision = eligible; missing_revision.config_revision.Clear();
	registry.providers.Clear(); registry.providers.Add(missing_revision);
	if(registry.Validate(error) || Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	PolicyProviderFact missing_reference = eligible; missing_reference.secret_reference.Clear();
	registry.providers.Clear(); registry.providers.Add(missing_reference);
	if(registry.Validate(error)) return false;
	registry.providers.Clear(); registry.providers.Add(eligible);
	input.action = POLICY_ACTION_PUBLICATION;
	if(!Resolve(instance, workspace, operator_preferences, registry, input).publication_allowed) return false;
	instance.remote_enabled = false;
	instance.export_enabled = false;
	if(Resolve(instance, workspace, operator_preferences, registry, input).publication_allowed) return false;
	input.action = POLICY_ACTION_MODEL;
	if(Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	instance.remote_enabled = true;
	instance.export_enabled = true;
	workspace.remote_enabled = false;
	workspace.export_enabled = false;
	if(Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	input.action = POLICY_ACTION_PUBLICATION;
	if(Resolve(instance, workspace, operator_preferences, registry, input).publication_allowed) return false;
	workspace.remote_enabled = true;
	workspace.export_enabled = true;
	input.sources.Clear();
	input.sources.Add(descendant.identity);
	input.action = POLICY_ACTION_MODEL;
	if(Resolve(instance, workspace, operator_preferences, registry, input).classification != "protected" ||
	   Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	PolicyProviderFact no_training = eligible;
	no_training.training_policy = "no_training";
	registry.providers.Clear(); registry.providers.Add(no_training);
	if(!Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	input.sources.Clear(); input.sources.Add(public_source.identity);
	input.sources.Add(public_source.identity);
	if(Resolve(instance, workspace, operator_preferences, registry, input).reason != "duplicate_requested_source_identity") return false;
	input.sources.Clear();
	input.sources.Add(public_source.identity);
	input.sources.Add(private_source.identity);
	if(Resolve(instance, workspace, operator_preferences, registry, input).classification != "private" ||
	   Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	input.sources.Clear(); input.sources.Add(public_source.identity);
	input.action = POLICY_ACTION_PUBLICATION;
	if(!Resolve(instance, workspace, operator_preferences, registry, input).publication_allowed) return false;
	input.sources.Clear(); input.sources.Add(hidden.identity);
	if(Resolve(instance, workspace, operator_preferences, registry, input).publication_allowed ||
	   Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	input.sources.Clear(); input.sources.Add(public_source.identity);
	PolicyProviderFact ineligible = no_training; ineligible.training_policy = "ineligible";
	registry.providers.Clear(); registry.providers.Add(ineligible);
	input.action = POLICY_ACTION_MODEL;
	if(Resolve(instance, workspace, operator_preferences, registry, input).external_model_allowed) return false;
	input.provider.object_id = "unknown";
	if(Resolve(instance, workspace, operator_preferences, registry, input).reason != "unknown_provider_identity") return false;
	input.provider = eligible.identity;
	input.workspace.object_id = "unregistered-workspace";
	if(Resolve(instance, workspace, operator_preferences, registry, input).reason != "unknown_workspace_identity") return false;
	input.workspace = registered_workspace.identity;
	input.sources[0].object_id = "unknown";
	if(Resolve(instance, workspace, operator_preferences, registry, input).reason != "unknown_source_identity") return false;
	input.sources.Clear(); input.sources.Add(public_source.identity);
	PolicyWorkspaceFact alternate_workspace;
	alternate_workspace.identity.workspace_id = "different-namespace";
	alternate_workspace.identity.object_id = "different-workspace-object";
	PolicySourceFact misbound_source;
	misbound_source.workspace = alternate_workspace.identity;
	misbound_source.identity.workspace_id = alternate_workspace.identity.workspace_id;
	misbound_source.identity.object_id = "misbound-source";
	misbound_source.classification = POLICY_PUBLIC;
	misbound_source.visible = true;
	registry.sources.Add(misbound_source);
	if(registry.Validate(error) || error != "source_workspace_unresolved") return false;
	registry.sources.Remove(registry.sources.GetCount() - 1);
	PolicyProviderFact misbound_provider;
	misbound_provider.workspace = alternate_workspace.identity;
	misbound_provider.identity.workspace_id = alternate_workspace.identity.workspace_id;
	misbound_provider.identity.object_id = "misbound-provider";
	misbound_provider.training_policy = "eligible";
	registry.providers.Add(misbound_provider);
	if(registry.Validate(error) || error != "provider_workspace_unresolved") return false;
	registry.providers.Remove(registry.providers.GetCount() - 1);
	PolicySourceFact duplicate = public_source;
	registry.sources.Add(duplicate);
	if(registry.Validate(error) || error != "ambiguous_source_identity") return false;
	registry.sources.Remove(registry.sources.GetCount() - 1);
	PolicyAncestryFact cycle;
	cycle.child = descendant.identity;
	cycle.parent = descendant.identity;
	registry.ancestry.Add(cycle);
	if(registry.Validate(error) || error != "cyclic_source_ancestry") return false;
	registry.ancestry.Remove(registry.ancestry.GetCount() - 1);
	instance.review_granularity = "goal";
	workspace.review_granularity = "record";
	operator_preferences.review_granularity = "milestone";
	EffectivePolicy strict_review = Resolve(instance, workspace, operator_preferences, registry, input);
	if(strict_review.review_granularity != "record" || strict_review.origin != "workspace") return false;
	instance.review_granularity = "record";
	workspace.review_granularity = "goal";
	operator_preferences.review_granularity = "goal";
	strict_review = Resolve(instance, workspace, operator_preferences, registry, input);
	if(strict_review.review_granularity != "record" || strict_review.origin != "instance") return false;
	instance.review_granularity.Clear();
	workspace.review_granularity.Clear();
	operator_preferences.review_granularity.Clear();
	TrustedPolicyRegistry budget;
	budget.configured = true;
	budget.workspaces.Add(registered_workspace);
	budget.providers.Add(eligible);
	for(int i = 0; i < MAX_POLICY_SOURCES; i++) {
		PolicySourceFact fact;
		fact.workspace = registered_workspace.identity;
		fact.identity.workspace_id = input.workspace.workspace_id;
		fact.identity.object_id = "budget-" + AsString(i);
		fact.classification = POLICY_PUBLIC;
		fact.visible = true;
		budget.sources.Add(fact);
	}
	if(!budget.Validate(error)) return false;
	PolicySourceFact overflow_fact;
	overflow_fact.identity.workspace_id = input.workspace.workspace_id;
	overflow_fact.workspace = registered_workspace.identity;
	overflow_fact.identity.object_id = "budget-overflow";
	overflow_fact.classification = POLICY_PUBLIC;
	overflow_fact.visible = true;
	budget.sources.Add(overflow_fact);
	if(budget.Validate(error) || error != "registry_budget_exceeded") return false;
	budget.sources.Remove(budget.sources.GetCount() - 1);
	for(const PolicySourceFact& fact : budget.sources)
		budget.protected_ancestry.Add(fact.identity);
	if(!budget.Validate(error)) return false;
	budget.protected_ancestry.Add(budget.sources[0].identity);
	if(budget.Validate(error) || error != "registry_budget_exceeded") return false;
	TrustedPolicyRegistry depth_boundary;
	depth_boundary.configured = true;
	depth_boundary.workspaces.Add(registered_workspace);
	depth_boundary.providers.Add(eligible);
	for(int i = 0; i <= MAX_POLICY_ANCESTRY_DEPTH; i++) {
		PolicySourceFact fact;
		fact.workspace = registered_workspace.identity;
		fact.identity.workspace_id = input.workspace.workspace_id;
		fact.identity.object_id = "depth-" + AsString(i);
		fact.classification = POLICY_PUBLIC;
		fact.visible = true;
		depth_boundary.sources.Add(fact);
		if(i > 0) {
			PolicyAncestryFact edge;
			edge.child = fact.identity;
			edge.parent = depth_boundary.sources[i - 1].identity;
			depth_boundary.ancestry.Add(edge);
		}
	}
	if(!depth_boundary.Validate(error)) return false;
	PolicySourceFact too_deep;
	too_deep.identity.workspace_id = input.workspace.workspace_id;
	too_deep.workspace = registered_workspace.identity;
	too_deep.identity.object_id = "depth-overflow";
	too_deep.classification = POLICY_PUBLIC;
	too_deep.visible = true;
	depth_boundary.sources.Add(too_deep);
	PolicyAncestryFact over_depth_edge;
	over_depth_edge.child = too_deep.identity;
	over_depth_edge.parent = depth_boundary.sources[depth_boundary.sources.GetCount() - 2].identity;
	depth_boundary.ancestry.Add(over_depth_edge);
	if(depth_boundary.Validate(error) || error != "source_ancestry_depth_exceeded") return false;
	instance.secret_reference = "synthetic-secret";
	String redacted = AsJSON(Resolve(instance, workspace, operator_preferences, registry, input).ToRedactedValue());
	if(redacted.Find("synthetic-secret") >= 0 || redacted.Find("synthetic-workspace") >= 0 ||
	   redacted.Find("secretref:synthetic-provider") >= 0 || redacted.Find("secret_reference") >= 0 ||
	   redacted.Find("synthetic-r1") >= 0 || redacted.Find("config_revision") >= 0 ||
	   redacted.Find("provider_config_revision") >= 0) return false;
	if(Resolve(PolicySettings(), workspace, operator_preferences, registry, input).status != "unconfigured") return false;
	return true;
}

END_UPP_NAMESPACE
