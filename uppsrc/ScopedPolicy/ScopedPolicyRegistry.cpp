#include "ScopedPolicyInternal.h"

NAMESPACE_UPP

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

END_UPP_NAMESPACE
