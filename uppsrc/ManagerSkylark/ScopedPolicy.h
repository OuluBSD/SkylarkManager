#ifndef _ManagerSkylark_ScopedPolicy_h_
#define _ManagerSkylark_ScopedPolicy_h_

#include <Core/Core.h>

NAMESPACE_UPP

enum PolicyClassification {
	POLICY_PUBLIC,
	POLICY_PRIVATE,
	POLICY_PROTECTED,
	POLICY_UNKNOWN
};

enum PolicyAction {
	POLICY_ACTION_MODEL,
	POLICY_ACTION_PUBLICATION
};

struct PolicyIdentity : Moveable<PolicyIdentity> {
	// T08 ID syntax: ASCII [A-Za-z0-9][A-Za-z0-9._/-]*, at most 256 bytes;
	// components are identifiers, not filesystem paths.
	String workspace_id;
	String object_id;
	String Key() const;
};

struct PolicySettings {
	bool configured = false;
	bool remote_enabled = false;
	bool export_enabled = false;
	String review_granularity;
	String secret_reference;
};

struct PolicySourceFact : Moveable<PolicySourceFact> {
	PolicyIdentity workspace;
	PolicyIdentity identity;
	PolicyClassification classification = POLICY_UNKNOWN;
	bool visible = false;
};

struct PolicyAncestryFact : Moveable<PolicyAncestryFact> {
	PolicyIdentity child;
	PolicyIdentity parent;
};

struct PolicyProviderFact : Moveable<PolicyProviderFact> {
	PolicyIdentity workspace;
	PolicyIdentity identity;
	String training_policy;
	bool enabled = false;
	String config_revision;
	String secret_reference;
};

struct PolicyWorkspaceFact : Moveable<PolicyWorkspaceFact> {
	PolicyIdentity identity;
};

class TrustedPolicyRegistry {
public:
	bool configured = false;
	Vector<PolicySourceFact> sources;
	Vector<PolicyAncestryFact> ancestry;
	Vector<PolicyProviderFact> providers;
	Vector<PolicyWorkspaceFact> workspaces;
	Vector<PolicyIdentity> protected_ancestry;

	bool Validate(String& error) const;
	static bool LoadValue(const Value& value, TrustedPolicyRegistry& target, String& error);
};

struct PolicyInput : Moveable<PolicyInput> {
	PolicyIdentity workspace;
	Vector<PolicyIdentity> sources;
	PolicyIdentity provider;
	PolicyAction action = POLICY_ACTION_MODEL;
};

class JobService;
class ScopedPolicyService;

struct EffectivePolicy {
	String status; // "evaluated" describes evaluation only; action allow flags are the grants.
	String origin;
	String reason;
	String classification;
	bool publication_allowed = false;
	bool source_visible = false;
	bool external_model_allowed = false;
	bool review_confirmed = false;
	String review_granularity;
	String secret_state;
	ValueMap ToRedactedValue() const;

private:
	String provider_config_revision;
	friend class ScopedPolicyService;
	friend class JobService;
};

class ScopedPolicyService {
public:
	static PolicySettings Defaults();
	static bool Validate(const PolicySettings& settings, String& error);
	static EffectivePolicy Resolve(const PolicySettings& instance,
	                               const PolicySettings& workspace,
	                               const PolicySettings& operator_preferences,
	                               const TrustedPolicyRegistry& registry,
	                               const PolicyInput& input);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
