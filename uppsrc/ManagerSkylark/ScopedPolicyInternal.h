#ifndef _ManagerSkylark_ScopedPolicyInternal_h_
#define _ManagerSkylark_ScopedPolicyInternal_h_

#include "ScopedPolicy.h"

NAMESPACE_UPP

// Limits and helpers shared by the ScopedPolicy*.cpp translation units. This is
// not public API; ScopedPolicy.h remains the interface.

static const int MAX_POLICY_SOURCES = 256;
static const int MAX_POLICY_ANCESTRY_EDGES = 512;
static const int MAX_POLICY_PROVIDERS = 64;
static const int MAX_POLICY_WORKSPACES = 64;
static const int MAX_POLICY_REQUEST_SOURCES = 256;
static const int MAX_POLICY_ANCESTRY_DEPTH = 32;
static const int MAX_POLICY_ID_COMPONENT_LENGTH = 256;
static const int MAX_PROVIDER_TOKEN_LENGTH = 64;

bool   SafeProviderToken(const String& value);
bool   SafeSecretReference(const String& value);
bool   CanonicalIdentityComponent(const String& value);
int    Strictness(PolicyClassification value);
String ClassificationName(PolicyClassification value);
bool   ValidIdentity(const PolicyIdentity& identity);
Value  PolicyField(const ValueMap& map, const String& key);
bool   ExactFields(const ValueMap& map, const char *const *fields, int count);
bool   ReadPolicyIdentity(const Value& value, PolicyIdentity& identity);
bool   ResolveRequestSources(const TrustedPolicyRegistry& registry, const PolicyInput& input,
                             Vector<const PolicySourceFact*>& resolved, EffectivePolicy& result);
bool   ResolveProviderFact(const TrustedPolicyRegistry& registry, const PolicyInput& input,
                           const PolicyProviderFact*& provider, EffectivePolicy& result);
bool   WalkSourceAncestry(const TrustedPolicyRegistry& registry, const Vector<const PolicySourceFact*>& resolved,
                          EffectivePolicy& result, PolicyClassification& effective, bool& visible, bool& protected_source);

END_UPP_NAMESPACE

#endif
