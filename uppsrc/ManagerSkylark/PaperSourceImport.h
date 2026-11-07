#ifndef _ManagerSkylark_PaperSourceImport_h_
#define _ManagerSkylark_PaperSourceImport_h_

#include "EvidenceIngestion.h"

NAMESPACE_UPP

struct PublicPaperSourceRule : Moveable<PublicPaperSourceRule> {
	String rule_id, hostname, path_prefix;
	Vector<String> allowed_license_ids;
	int max_bytes = 0, max_sections = 0;
	String internal_use_mode;
	PublicPaperSourceRule() {}
	PublicPaperSourceRule(const PublicPaperSourceRule& o) : rule_id(o.rule_id), hostname(o.hostname), path_prefix(o.path_prefix), allowed_license_ids(clone(o.allowed_license_ids)), max_bytes(o.max_bytes), max_sections(o.max_sections), internal_use_mode(o.internal_use_mode) {}
	PublicPaperSourceRule& operator=(const PublicPaperSourceRule& o) { if(this != &o) { rule_id=o.rule_id; hostname=o.hostname; path_prefix=o.path_prefix; allowed_license_ids=clone(o.allowed_license_ids); max_bytes=o.max_bytes; max_sections=o.max_sections; internal_use_mode=o.internal_use_mode; } return *this; }
};

struct PaperFetchSection : Moveable<PaperFetchSection> {
	String section, text, locator;
	int page = 0;
};

class CurlPaperTransport;
class PaperTransportAttestation : Moveable<PaperTransportAttestation> {
	String binding, tool_version;
	PaperTransportAttestation(const String& digest, const String& version) : binding(digest), tool_version(version) {}
	friend class CurlPaperTransport;
public:
	PaperTransportAttestation() {}
	bool Matches(const String& source_url, const String& final_url, const Vector<String>& redirects, const String& body, const String& mime_type) const;
	String GetToolVersion() const { return binding.IsEmpty() ? String() : tool_version; }
};

// Synthetic snapshots have no transport evidence; verified snapshots carry only
// a private attestation bound to the transport response and URL chain.
struct PaperFetchSnapshot : Moveable<PaperFetchSnapshot> {
	String workspace_id, source_identity, external_version, source_url;
	Vector<String> redirect_chain;
	String final_url, peer_address_claim, license_id, license_evidence_ref;
	String source_bytes, source_sha256, captured_at, extractor, extractor_version, config_revision;
	String mime_type;
	PaperTransportAttestation transport_attestation;
	Vector<PaperFetchSection> sections;
	PaperFetchSnapshot() {}
	PaperFetchSnapshot(const PaperFetchSnapshot& o) : workspace_id(o.workspace_id), source_identity(o.source_identity), external_version(o.external_version), source_url(o.source_url), redirect_chain(clone(o.redirect_chain)), final_url(o.final_url), peer_address_claim(o.peer_address_claim), license_id(o.license_id), license_evidence_ref(o.license_evidence_ref), source_bytes(o.source_bytes), source_sha256(o.source_sha256), captured_at(o.captured_at), extractor(o.extractor), extractor_version(o.extractor_version), config_revision(o.config_revision), mime_type(o.mime_type), transport_attestation(o.transport_attestation), sections(clone(o.sections)) {}
	PaperFetchSnapshot& operator=(const PaperFetchSnapshot& o) { if(this != &o) { workspace_id=o.workspace_id; source_identity=o.source_identity; external_version=o.external_version; source_url=o.source_url; redirect_chain=clone(o.redirect_chain); final_url=o.final_url; peer_address_claim=o.peer_address_claim; license_id=o.license_id; license_evidence_ref=o.license_evidence_ref; source_bytes=o.source_bytes; source_sha256=o.source_sha256; captured_at=o.captured_at; extractor=o.extractor; extractor_version=o.extractor_version; config_revision=o.config_revision; mime_type=o.mime_type; transport_attestation=o.transport_attestation; sections=clone(o.sections); } return *this; }
};

struct PaperSourceImportResult : Moveable<PaperSourceImportResult> {
	String status, operation_id;
	Vector<String> record_ids;
};

class PaperSourceImport {
public:
	// No source rule and no transport is installed by default.
	Vector<PublicPaperSourceRule> rules;
	static bool Validate(const PaperFetchSnapshot& value, const Vector<PublicPaperSourceRule>& rules, String& error);
	static PaperSourceImportResult Import(const DataStore& store, const Vector<PublicPaperSourceRule>& rules,
	                                     const TrustedPolicyRegistry& policy, const PaperFetchSnapshot& value,
	                                     const DataWriteAuthorization& authorization);
	static PaperSourceImportResult Import(const DataStore& store, const Vector<PublicPaperSourceRule>& rules,
	                                     const TrustedPolicyRegistry& policy, const PaperFetchSnapshot& value,
	                                     const PaperTransportAttestation& attestation,
	                                     const DataWriteAuthorization& authorization);
	static bool RunSyntheticTests();
};

END_UPP_NAMESPACE

#endif
