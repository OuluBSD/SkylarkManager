#include "ScopedPolicyInternal.h"

NAMESPACE_UPP

int Strictness(PolicyClassification value)
{
	switch(value) {
	case POLICY_PUBLIC: return 0;
	case POLICY_PRIVATE: return 1;
	case POLICY_PROTECTED: return 2;
	default: return 3;
	}
}

String ClassificationName(PolicyClassification value)
{
	switch(value) {
	case POLICY_PUBLIC: return "public";
	case POLICY_PRIVATE: return "private";
	case POLICY_PROTECTED: return "protected";
	default: return "unknown";
	}
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
	if(!ResolveRequestSources(registry, input, resolved, result)) return result;
	const PolicyProviderFact* provider = NULL;
	if(!ResolveProviderFact(registry, input, provider, result)) return result;
	result.provider_config_revision = provider->config_revision;
	result.secret_state = provider->secret_reference.IsEmpty() ? "missing_reference" : "referenced_redacted";
	PolicyClassification effective = POLICY_PUBLIC;
	bool visible = true;
	bool protected_source = false;
	if(!WalkSourceAncestry(registry, resolved, result, effective, visible, protected_source)) return result;
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

END_UPP_NAMESPACE
