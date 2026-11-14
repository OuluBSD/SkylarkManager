#include "ScopedPolicyInternal.h"

NAMESPACE_UPP

bool WalkSourceAncestry(const TrustedPolicyRegistry& registry, const Vector<const PolicySourceFact*>& resolved,
                        EffectivePolicy& result, PolicyClassification& effective, bool& visible, bool& protected_source)
{
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
				return false;
			}
			const PolicySourceFact* parent = NULL;
			for(const PolicySourceFact& candidate : registry.sources)
				if(candidate.identity.Key() == edge.parent.Key()) parent = &candidate;
			if(!parent) { result.reason = "inconsistent_source_ancestry"; return false; }
			pending.Add(parent);
			pending_depth.Add(depth + 1);
		}
	}
	return true;
}

END_UPP_NAMESPACE
