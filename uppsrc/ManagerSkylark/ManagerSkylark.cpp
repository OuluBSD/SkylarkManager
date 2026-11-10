#include "ManagerSkylark.h"
#include "EngineComposition.h"
#include "SystemAuth.h"
#include "AssessmentViews.h"
#include <GitHubForge/GitHubForge.h>
#include "CommandManifestRoutes.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include "HostPlatform.h"

NAMESPACE_UPP

ManagerSkylarkApp* ManagerSkylarkApp::s_instance = NULL;

ManagerSkylarkApp::ManagerSkylarkApp()
	: test_mode(false)
	, demo_site(false)
	, custom_site_dir(false)
	, custom_port(8081)
{
	s_instance = this;
	root = "";
	port = 8081;
	ip = "0.0.0.0";
	threads = 2;
	prefork = 0;
	use_caching = false;
	site_dir = GetHomeDirFile(".config/managerskylark");
	instance_policy = ScopedPolicyService::Defaults();
}

ManagerSkylarkApp::~ManagerSkylarkApp()
{
	if(demo_site && custom_site_dir && DirectoryExists(site_dir))
		DeleteFolderDeep(site_dir);
}

ManagerSkylarkApp& ManagerSkylarkApp::Get()
{
	ASSERT(s_instance);
	return *s_instance;
}

void ManagerSkylarkApp::StopServer()
{
	Quit();
	for(int i = 0; i < threads + 2; i++) {
		HttpRequest req(Format("http://127.0.0.1:%d/quit", port));
		req.Timeout(200);
		req.Execute();
	}
}

// Password for a composed-site account: the environment variable when set,
// otherwise a hidden prompt. The buffer is wiped in both cases.
static bool ReadEnginePassword(const char *env, const char *prompt, String& password)
{
	password = GetEnv(env);
	if(!password.IsEmpty())
		return true;
	char buffer[4096] = {};
	if(EVP_read_pw_string(buffer, sizeof(buffer), prompt, 1) != 0) {
		OPENSSL_cleanse(buffer, sizeof(buffer));
		Cerr() << "Could not read the password securely.\n";
		return false;
	}
	password = buffer;
	OPENSSL_cleanse(buffer, sizeof(buffer));
	return true;
}

bool ManagerSkylarkApp::Initialize(const Vector<String>& args)
{
	CommandLineArguments cla;
	cla.SetDescription("ManagerSkylark - Web application following ../Manager/v2 design");
	cla.AddArg("help", 'h', "Show help options", false);
	cla.AddArg("port", 'p', "HTTP server port", true, "port");
	cla.AddArg("site-dir", 0, "Site installation directory (auth.json, data-root.json, v2/, mgr/)", true, "directory");
	cla.AddArg("set-site-password", 0, "Create/update a site account with hidden prompt (or MANAGERSKYLARK_SITE_PASSWORD)", true, "username");
	cla.AddArg("demo-site", 0, "Run an isolated synthetic, read-only demo site", false);
	cla.AddArg("test", 0, "Run automated HTTP and store verification", false);
	cla.AddArg("privatehub-port", 0, "PrivateHub engine site port (default 18211)", true, "port");
	cla.AddArg("science-port", 0, "Science engine site port (default 18212)", true, "port");
	cla.AddArg("science-corpus", 0, "Science corpus directory (default ~/Dev/AnthologyPrivate/site/data)", true, "directory");
	cla.AddArg("privatehub-workspace", 0, "PrivateHub workspace root (default ~/Dev)", true, "directory");
	cla.AddArg("taskfoundry-port", 0, "TaskFoundry engine site port (default 18213)", true, "port");
	cla.AddArg("company-root", 0, "Composed Company store root (default <site-dir>/engine/company)", true, "directory");
	cla.AddArg("company-repos", 0, "Comma-separated repositories Company may propose against (default none)", true, "owner/repo,...");
	cla.AddArg("approve-taskfoundry-grant", 0, "Approve a requested TaskFoundry grant by id (wag_...) and exit", true, "grant-id");
	cla.AddArg("revoke-taskfoundry-grant", 0, "Revoke an active TaskFoundry grant by id and exit", true, "grant-id");
	cla.AddArg("set-taskfoundry-user", 0, "Create/update a TaskFoundry account with hidden prompt (or MANAGERSKYLARK_TASKFOUNDRY_PASSWORD)", true, "username");
	cla.AddArg("set-privatehub-user", 0, "Create/update a PrivateHub account with hidden prompt (or MANAGERSKYLARK_PRIVATEHUB_PASSWORD)", true, "username");
	cla.AddArg("privatehub-role", 0, "Role for --set-privatehub-user (default member; only member may author)", true, "role");
	cla.AddArg("github-backend", 0, "GitHub backend: gh (argv-only gh CLI, default) or emulated (local file-backed emulation, no network)", true, "gh|emulated");
	cla.AddArg("github-store", 0, "Emulated GitHub store directory (default <site-dir>/github)", true, "directory");
	cla.AddArg("set-science-user", 0, "Create/update a Science author account with hidden prompt (or MANAGERSKYLARK_SCIENCE_PASSWORD)", true, "username");

	if(!cla.Parse(args))
		return false;
	if(cla.IsArg('h') || cla.IsArg("help")) {
		cla.PrintHelp();
		return false;
	}

	test_mode = cla.IsArg("test");
	demo_site = cla.IsArg("demo-site");
	String set_site_user = cla.GetArg("set-site-password");
	if(demo_site && !set_site_user.IsEmpty()) {
		Cerr() << "--set-site-password cannot be combined with --demo-site.\n";
		return false;
	}
	String configured_site_dir = cla.GetArg("site-dir");
	if(demo_site) {
		if(!configured_site_dir.IsEmpty())
			site_dir = AppendFileName(NormalizePath(configured_site_dir), "demo-" + Format(Uuid::Create()));
		else
			site_dir = GetTempFileName("ManagerSkylarkDemo");
		custom_site_dir = true;
	}
	else if(!configured_site_dir.IsEmpty()) {
		site_dir = NormalizePath(configured_site_dir);
		custom_site_dir = true;
	}
	if(custom_site_dir)
		RealizeDirectory(site_dir);
	if(custom_site_dir && !DirectoryExists(site_dir))
		return false;
	GitHubForgeBackend::Config github_cfg;
	String github_backend = cla.GetArg("github-backend");
	if(!github_backend.IsEmpty() && !GitHubForgeBackend::ParseKind(github_backend, github_cfg.kind)) {
		Cerr() << "--github-backend must be gh or emulated.\n";
		return false;
	}
	github_cfg.store_dir = cla.GetArg("github-store");
	if(github_cfg.store_dir.IsEmpty() && custom_site_dir)
		github_cfg.store_dir = AppendFileName(site_dir, "github");
	GitHubForgeBackend::Configure(github_cfg);
	RegisterGitHubCommands();
	SystemAuthService::ConfigureSite(site_dir, demo_site);
	DataStore::SetRootRegistrationPath(custom_site_dir ? AppendFileName(site_dir, "data-root.json") : String());
	if(!set_site_user.IsEmpty()) {
		String password = GetEnv("MANAGERSKYLARK_SITE_PASSWORD");
		if(password.IsEmpty()) {
			char buffer[4096] = {};
			if(EVP_read_pw_string(buffer, sizeof(buffer), "New site password: ", 1) != 0) {
				OPENSSL_cleanse(buffer, sizeof(buffer));
				Cerr() << "Could not read the site password securely.\n";
				return false;
			}
			password = buffer;
			OPENSSL_cleanse(buffer, sizeof(buffer));
		}
		if(!SystemAuthService::SetSitePassword(site_dir, set_site_user, password)) {
			Cerr() << "Failed to write site credentials.\n";
			return false;
		}
		Cout() << "Configured site account '" << set_site_user << "' in " << AppendFileName(site_dir, "auth.json") << "\n";
		return false;
	}
	if(!demo_site && !FileExists(AppendFileName(site_dir, "auth.json")))
		RLOG("No site credentials configured; use --set-site-password USER to create an account.");

	String p_str = cla.GetArg('p');
	if(p_str.IsEmpty())
		p_str = cla.GetArg("port");

	if(!p_str.IsEmpty()) {
		custom_port = ScanInt(p_str);
		if(custom_port > 0 && custom_port < 65536)
			port = custom_port;
	}

	// Grant decisions read and write the grant store directly, so they run while
	// the server is up. Composition would bind the engine ports, so it is skipped.
	String grant_approve = cla.GetArg("approve-taskfoundry-grant");
	String grant_revoke = cla.GetArg("revoke-taskfoundry-grant");
	if(!grant_approve.IsEmpty() || !grant_revoke.IsEmpty()) {
		bool approve = !grant_approve.IsEmpty();
		String gid = approve ? grant_approve : grant_revoke;
		String grants_root = AppendFileName(AppendFileName(site_dir, "engine"), "grants");
		RealizeDirectory(grants_root);
		WebGrants grants("meta", grants_root);
		WebAccountGrant g;
		if(!grants.GetGrant(gid, g) || g.client_site != TASKFOUNDRY_SITE_ID) {
			Cerr() << "No TaskFoundry grant " << gid << " in " << grants_root << "\n";
			return false;
		}
		StoreResult st = approve ? grants.Decide(gid, true) : grants.Revoke(gid);
		if(!st) {
			Cerr() << "Grant " << gid << " not changed: " << st.error << "\n";
			return false;
		}
		Cout() << (approve ? "Approved" : "Revoked") << " TaskFoundry grant " << gid << "\n";
		fflush(stdout);
		_exit(0);
	}

	if(!test_mode && !demo_site) {
		EngineComposition cfg = EngineComposition::Defaults();
		if(!cla.GetArg("privatehub-port").IsEmpty())
			cfg.privatehub_port = ScanInt(cla.GetArg("privatehub-port"));
		if(!cla.GetArg("science-port").IsEmpty())
			cfg.science_port = ScanInt(cla.GetArg("science-port"));
		if(!cla.GetArg("science-corpus").IsEmpty())
			cfg.science_corpus = NormalizePath(cla.GetArg("science-corpus"));
		if(!cla.GetArg("privatehub-workspace").IsEmpty())
			cfg.privatehub_workspace = NormalizePath(cla.GetArg("privatehub-workspace"));
		if(!cla.GetArg("taskfoundry-port").IsEmpty())
			cfg.taskfoundry_port = ScanInt(cla.GetArg("taskfoundry-port"));
		if(!cla.GetArg("company-root").IsEmpty())
			cfg.company_root = NormalizePath(cla.GetArg("company-root"));
		if(!cla.GetArg("company-repos").IsEmpty())
			cfg.company_repos = Split(cla.GetArg("company-repos"), ',', false);
		String compose_error;
		if(!ComposeEngineSites(*this, engine_backends, site_dir, cfg, compose_error)) {
			Cerr() << "Engine composition failed: " << compose_error << "\n";
			return false;
		}
		engine_composed = true;

		String tf_user = cla.GetArg("set-taskfoundry-user");
		if(!tf_user.IsEmpty()) {
			String password;
			if(!ReadEnginePassword("MANAGERSKYLARK_TASKFOUNDRY_PASSWORD", "New TaskFoundry password: ", password))
				return false;
			String error;
			if(!SetTaskFoundryPassword(tf_user, password, error)) {
				Cerr() << "Failed to write TaskFoundry credentials: " << error << "\n";
				return false;
			}
			Cout() << "Configured TaskFoundry account '" << tf_user << "'\n";
			fflush(stdout);
			_exit(0);
		}

		String ph_user = cla.GetArg("set-privatehub-user");
		if(!ph_user.IsEmpty()) {
			String password;
			if(!ReadEnginePassword("MANAGERSKYLARK_PRIVATEHUB_PASSWORD", "New PrivateHub password: ", password))
				return false;
			String error;
			String ph_role = cla.GetArg("privatehub-role");
			if(ph_role.IsEmpty())
				ph_role = "member";
			if(!SetPrivateHubPassword(ph_user, password, error, ph_role)) {
				Cerr() << "Failed to write PrivateHub credentials: " << error << "\n";
				return false;
			}
			Cout() << "Configured PrivateHub account '" << ph_user << "'\n";
			fflush(stdout);
			_exit(0);
		}

		String sci_user = cla.GetArg("set-science-user");
		if(!sci_user.IsEmpty()) {
			String password;
			if(!ReadEnginePassword("MANAGERSKYLARK_SCIENCE_PASSWORD", "New Science password: ", password))
				return false;
			String error;
			if(!SetScienceUserPassword(sci_user, password, error)) {
				Cerr() << "Failed to write Science credentials: " << error << "\n";
				return false;
			}
			Cout() << "Configured Science author account '" << sci_user << "'\n";
			fflush(stdout);
			_exit(0);
		}
	}

	InitApp();
	return true;
}

END_UPP_NAMESPACE
