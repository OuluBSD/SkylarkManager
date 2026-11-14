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
	// a literal path before the engine catch-all (stock "login" shadowed the
	// meta site's own login form, task 036 audit). The gateway routes by the raw
	// request target, so every path goes to the site's own router. The site
	// answers only with its own routes; no stock page renders on an engine port.
	if(WebEngine::CurrentListenerPort() > 0) {
		if(IsEngineGatewayHandler(http.GetHandlerId()))
			return true;
		TheWebEngine().Gateway(http);
		return false;
	}
	if(test_mode)
		return true;
	UserSession session = SystemAuthService::GetCurrentSession(http);
	int rejection_status = 0;
	bool allowed = SystemAuthService::AuthorizeRequest(http, &session, &rejection_status);
	SystemAuthService::SetRootAuditContext(http, session, allowed);
	return allowed;
}

void ManagerSkylarkApp::RequestComplete(Http& http, int duration_ms)
{
	if(test_mode || WebEngine::CurrentListenerPort() > 0)
		return;
	SystemAuthService::RecordRootRequestComplete(http, duration_ms);
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
