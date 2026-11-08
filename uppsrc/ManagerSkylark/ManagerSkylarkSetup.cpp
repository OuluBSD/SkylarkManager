#include "ManagerSkylark.h"
#include "ManagerSkylarkSeed.h"
#include "SystemAuth.h"
#include "AssessmentViews.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include "HostPlatform.h"

NAMESPACE_UPP

static bool ReadPolicySettings(const Value& value, PolicySettings& settings)
{
	if(!value.Is<ValueMap>()) return false;
	ValueMap map = value;
	if(map.GetCount() != 5 || map.Find("configured") < 0 || map.Find("remote_enabled") < 0 ||
	   map.Find("export_enabled") < 0 || map.Find("review_granularity") < 0 || map.Find("secret_reference") < 0)
		return false;
	Value configured = map["configured"], remote = map["remote_enabled"], export_enabled = map["export_enabled"];
	Value review = map["review_granularity"], secret = map["secret_reference"];
	if(!configured.Is<bool>() || !remote.Is<bool>() || !export_enabled.Is<bool>() ||
	   !IsString(review) || !IsString(secret))
		return false;
	settings.configured = configured;
	settings.remote_enabled = remote;
	settings.export_enabled = export_enabled;
	settings.review_granularity = review;
	settings.secret_reference = secret;
	String error;
	return ScopedPolicyService::Validate(settings, error);
}

void ManagerSkylarkApp::LoadSitePolicy()
{
	instance_policy = ScopedPolicyService::Defaults();
	workspace_policy = ScopedPolicyService::Defaults();
	operator_policy = ScopedPolicyService::Defaults();
	trusted_policy_registry = TrustedPolicyRegistry();
	if(!custom_site_dir) return;
	String path = AppendFileName(site_dir, "policy.json");
	if(!FileExists(path)) return;
	int64 size = GetFileLength(path);
	if(size < 0 || size > 64 * 1024) {
		RLOG("Site policy configuration rejected by size limit; sensitive-source visibility remains denied.");
		return;
	}
	Value root;
	try { root = ParseJSON(LoadFile(path)); }
	catch(Exc) { RLOG("Site policy configuration is invalid; sensitive-source visibility remains denied."); return; }
	if(!root.Is<ValueMap>()) { RLOG("Site policy configuration is invalid; sensitive-source visibility remains denied."); return; }
	ValueMap map = root;
	if(map.GetCount() != 2 || map.Find("instance") < 0 || map.Find("registry") < 0) {
		RLOG("Site policy configuration fields are invalid; sensitive-source visibility remains denied.");
		return;
	}
	PolicySettings instance;
	TrustedPolicyRegistry registry;
	String error;
	if(!ReadPolicySettings(map["instance"], instance) || !instance.configured ||
	   !TrustedPolicyRegistry::LoadValue(map["registry"], registry, error)) {
		RLOG("Site policy configuration did not validate; sensitive-source visibility remains denied.");
		return;
	}
	instance_policy = instance;
	trusted_policy_registry.configured = registry.configured;
	trusted_policy_registry.sources = pick(registry.sources);
	trusted_policy_registry.ancestry = pick(registry.ancestry);
	trusted_policy_registry.providers = pick(registry.providers);
	trusted_policy_registry.workspaces = pick(registry.workspaces);
	trusted_policy_registry.protected_ancestry = pick(registry.protected_ancestry);
}

void ManagerSkylarkApp::InitApp()
{
	if(custom_site_dir) {
		v2_store.Load(AppendFileName(site_dir, "v2"));
		local_store.SetRootDir(AppendFileName(site_dir, "mgr"));
		local_store.LoadAll();
	}
	else {
		v2_store.Load();
		local_store.LoadAll();
	}
	if(demo_site) {
		String data_root = AppendFileName(site_dir, "data");
		RealizeDirectory(AppendFileName(data_root, "candidates"));
		ValueMap paper;
		paper.Add("id", "demo-paper");
		paper.Add("kind", "paper");
		paper.Add("title", "Synthetic Research Paper");
		paper.Add("source", "demo");
		paper.Add("status", "draft");
		paper.Add("envelope", MakeDemoEnvelope("paper", "demo-workspace", "demo-paper", "public", "synthetic-demo"));
		SaveFile(AppendFileName(data_root, "paper.json"), AsJSON(paper, true));
		SaveFile(AppendFileName(AppendFileName(data_root, "candidates"), "visible.json"), AsJSON(MakeDemoCandidate("demo-candidate"), true));
		SaveFile(AppendFileName(AppendFileName(data_root, "candidates"), "hidden.json"), AsJSON(MakeDemoCandidate("hidden-candidate", "private-source"), true));
		SeedSyntheticCandidateWorkflow(data_root, "demo-workspace", "demo-candidate", "demo-source");
		SeedSyntheticReviewData(data_root, "demo-workspace", "demo-source");
		ValueMap registration;
		registration.Add("root", data_root);
		SaveFile(AppendFileName(site_dir, "data-root.json"), AsJSON(registration, true));
		SaveFile(AppendFileName(site_dir, "policy.json"), AsJSON(MakeDemoPolicyConfig(), true));
		DataStore::SetRootRegistrationPath(AppendFileName(site_dir, "data-root.json"));
	}
	LoadSitePolicy();
	data_store.Load();
	if(custom_site_dir && (demo_site || test_mode) &&
	   !SeedSyntheticAssessmentFiles(site_dir, demo_site ? "demo-workspace" : "synthetic-workspace",
	                                demo_site ? "demo-candidate" : "synthetic-candidate", "demo-evidence-item"))
		RLOG("Synthetic assessment fixtures could not be written; assessment views remain unavailable.");
	if(demo_site && local_store.Projects().IsEmpty()) {
		Project project;
		project.slug = "demo-project";
		project.title = "Synthetic Demo Project";
		project.status = "active";
		project.description = "Example content isolated to this temporary demo installation.";
		local_store.SaveProject(project);
		Phase phase;
		phase.project = project.slug;
		phase.seq = 1;
		phase.slug = "demo-phase";
		phase.title = "Example Phase";
		phase.status = "open";
		local_store.SavePhase(phase);
		Task task;
		task.project = project.slug;
		task.phase_seq = phase.seq;
		task.seq = 1;
		task.slug = "demo-task";
		task.title = "Example Task";
		task.status = "open";
		local_store.SaveTask(task);
		local_store.LoadAll();
	}
}

END_UPP_NAMESPACE
