#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "AssessmentViews.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include "HostPlatform.h"
#include "EngineComposition.h"

NAMESPACE_UPP

bool ManagerSkylarkApp::FilterHandler(Http& http)
{
	// Engine listeners serve only their composed sites: stock routes are never
	// reachable there, whatever the path, so every request goes through the
	// engine gateway and the site's own session.
	if(WebEngine::CurrentListenerPort() > 0) {
		if(IsEngineGatewayHandler(http.GetHandlerId()))
			return true;
		TheWebEngine().Gateway(http);
		return false;
	}
	return test_mode || SystemAuthService::AuthorizeRequest(http);
}

EffectivePolicy ManagerSkylarkApp::ResolveEffectivePolicy(const PolicyInput& input) const
{
	return ScopedPolicyService::Resolve(instance_policy, workspace_policy, operator_policy,
	                                    trusted_policy_registry, input);
}

bool ManagerSkylarkApp::IsAssessmentSourceVisible(const String& workspace_id, const String& source_identity) const
{
	if(workspace_id.IsEmpty() || source_identity.IsEmpty()) return false;
	for(const PolicyProviderFact& provider : trusted_policy_registry.providers) {
		PolicyInput input;
		input.workspace.workspace_id = workspace_id;
		input.workspace.object_id = workspace_id;
		PolicyIdentity source;
		source.workspace_id = workspace_id;
		source.object_id = source_identity;
		input.sources.Add(source);
		input.provider = provider.identity;
		EffectivePolicy policy = ResolveEffectivePolicy(input);
		if(policy.source_visible && policy.status == "evaluated")
			return true;
	}
	return false;
}

END_UPP_NAMESPACE
