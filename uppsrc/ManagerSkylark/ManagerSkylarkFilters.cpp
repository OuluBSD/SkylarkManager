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
	// Engine listeners serve only their composed sites. A stock route can claim
	// a path before the engine catch-all: the gateway then rebuilds the path
	// from the route's wildcard arguments, which drops literal segments, so it
	// would serve the wrong page. Only the root route is safe to hand over: its
	// pattern matches exactly "/", the one path it can be reached with.
	if(WebEngine::CurrentListenerPort() > 0) {
		String id = http.GetHandlerId();
		if(IsEngineGatewayHandler(id))
			return true;
		if(id == "PortalShellPage") {
			TheWebEngine().Gateway(http);
			return false;
		}
		http << "not found";
		http.Response(404, "Not found");
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
