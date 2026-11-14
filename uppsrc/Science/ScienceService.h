#ifndef _Science_ScienceService_h_
#define _Science_ScienceService_h_

#include <Core/Core.h>
#include <WebBroker/WebBroker.h>
#include "ScienceModel.h"

NAMESPACE_UPP

// Broker surface for cross-site callers (TaskFoundry): the corpus without the
// corpus directory. Reads need scope science.read; put-article needs
// science.author (the same scope the HTTP authoring path accepts through a
// grant). Deny by default: a call without its scope is refused with 403.
class ScienceService : public IBrokerService {
public:
	explicit ScienceService(ScienceCorpus& corpus) : corpus(corpus) {}

	BrokerResult Handle(const BrokerCallContext& ctx, const BrokerCall& call) override;
	String AuditOperation(const String& op) const override;

	static const char *READ_SCOPE()   { return "science.read"; }
	static const char *AUTHOR_SCOPE() { return "science.author"; }

private:
	ScienceCorpus& corpus;
};

END_UPP_NAMESPACE

#endif
