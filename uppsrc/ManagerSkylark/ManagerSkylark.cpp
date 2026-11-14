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
	cla.AddArg("meta-port", 0, "MetaAccount engine site port (default 18214)", true, "port");
	cla.AddArg("channels-port", 0, "Channels engine site port (default 18215)", true, "port");
	cla.AddArg("agentipc-direct", 0, "AgentIPC node address for the Channels site (default 127.0.0.1:7440)", true, "host:port");
	cla.AddArg("agentipc-server", 0, "AgentIPC virtual server for the Channels site (default skylark)", true, "server");
	cla.AddArg("agentipc-user", 0, "AgentIPC identity the Channels site reads as (default channels)", true, "user");
	cla.AddArg("agentipc-python", 0, "Python running the agentipc client (default ~/.venvs/agentipc/bin/python)", true, "path");
	cla.AddArg("agentipc-client", 0, "agentipc client script (default ~/OuluLinux/AgentIPC/bin/agentipc)", true, "path");
	cla.AddArg("agentipc-home", 0, "AgentIPC node home (default ~/.agentipc-skylark)", true, "directory");
	cla.AddArg("financials-port", 0, "Financials engine site port (default 18216)", true, "port");
	cla.AddArg("atlas-port", 0, "Atlas engine site port (default 18217)", true, "port");
	cla.AddArg("youtubeplanner-port", 0, "YouTubePlanner engine site port (default 18218)", true, "port");
	cla.AddArg("enable-meta-sso", 0, "Opt in to MetaAccount single sign-on across composed sites (default off)", false);
	cla.AddArg("atlas-role", 0, "Role for --set-atlas-user (default admin: may edit maps; other roles read only)", true, "role");
	cla.AddArg("set-atlas-user", 0, "Create/update an Atlas account with hidden prompt (or MANAGERSKYLARK_ATLAS_PASSWORD)", true, "username");
	cla.AddArg("docs-repos", 0, "Documentation repositories for the /docs portal (label=path,...; default: SkylarkManager, CompanySkylark, AgentIPC)", true, "list");
	cla.AddArg("financials-role", 0, "Role for --set-financials-user (default viewer; finance sees ledger rows and contracts)", true, "role");
	cla.AddArg("company-root", 0, "Composed Company store root (default <site-dir>/engine/company)", true, "directory");
	cla.AddArg("company-repos", 0, "Comma-separated repositories Company may propose against (default none)", true, "owner/repo,...");
	cla.AddArg("approve-taskfoundry-grant", 0, "Approve a requested TaskFoundry grant by id (wag_...) and exit", true, "grant-id");
	cla.AddArg("revoke-taskfoundry-grant", 0, "Revoke an active TaskFoundry grant by id and exit", true, "grant-id");
	cla.AddArg("set-taskfoundry-user", 0, "Create/update a TaskFoundry account with hidden prompt (or MANAGERSKYLARK_TASKFOUNDRY_PASSWORD)", true, "username");
	cla.AddArg("set-meta-user", 0, "Create/update a MetaAccount account with hidden prompt (or MANAGERSKYLARK_META_PASSWORD)", true, "username");
	cla.AddArg("set-channels-user", 0, "Create/update a Channels account with hidden prompt (or MANAGERSKYLARK_CHANNELS_PASSWORD)", true, "username");
	cla.AddArg("set-financials-user", 0, "Create/update a Financials account with hidden prompt (or MANAGERSKYLARK_FINANCIALS_PASSWORD)", true, "username");
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
	DocsReposSpec() = cla.GetArg("docs-repos");
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
		if(!cla.GetArg("meta-port").IsEmpty())
			cfg.meta_port = ScanInt(cla.GetArg("meta-port"));
		if(!cla.GetArg("channels-port").IsEmpty())
			cfg.channels_port = ScanInt(cla.GetArg("channels-port"));
		if(!cla.GetArg("financials-port").IsEmpty())
			cfg.financials_port = ScanInt(cla.GetArg("financials-port"));
		if(!cla.GetArg("atlas-port").IsEmpty())
			cfg.atlas_port = ScanInt(cla.GetArg("atlas-port"));
		if(!cla.GetArg("youtubeplanner-port").IsEmpty())
			cfg.youtubeplanner_port = ScanInt(cla.GetArg("youtubeplanner-port"));
		cfg.enable_meta_sso = cla.IsArg("enable-meta-sso");
		TheWebEngine().SetHostPort(port);
		if(!cla.GetArg("agentipc-direct").IsEmpty())
			cfg.agentipc_direct = cla.GetArg("agentipc-direct");
		if(!cla.GetArg("agentipc-server").IsEmpty())
			cfg.agentipc_server = cla.GetArg("agentipc-server");
		if(!cla.GetArg("agentipc-user").IsEmpty())
			cfg.agentipc_user = cla.GetArg("agentipc-user");
		if(!cla.GetArg("agentipc-python").IsEmpty())
			cfg.agentipc_python = NormalizePath(cla.GetArg("agentipc-python"));
		if(!cla.GetArg("agentipc-client").IsEmpty())
			cfg.agentipc_client = NormalizePath(cla.GetArg("agentipc-client"));
		if(!cla.GetArg("agentipc-home").IsEmpty())
			cfg.agentipc_home = NormalizePath(cla.GetArg("agentipc-home"));
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

		String fin_user = cla.GetArg("set-financials-user");
		if(!fin_user.IsEmpty()) {
			String password;
			if(!ReadEnginePassword("MANAGERSKYLARK_FINANCIALS_PASSWORD", "New Financials password: ", password))
				return false;
			String error;
			if(!SetFinancialsPassword(fin_user, cla.GetArg("financials-role"), password, error)) {
				Cerr() << "Failed to write Financials credentials: " << error << "\n";
				return false;
			}
			Cout() << "Configured Financials account '" << fin_user << "'\n";
			fflush(stdout);
			_exit(0);
		}

		String atl_user = cla.GetArg("set-atlas-user");
		if(!atl_user.IsEmpty()) {
			String password;
			if(!ReadEnginePassword("MANAGERSKYLARK_ATLAS_PASSWORD", "New Atlas password: ", password))
				return false;
			String error;
			String atl_role = cla.GetArg("atlas-role").IsEmpty() ? String("admin") : cla.GetArg("atlas-role");
			if(!SetAtlasPassword(atl_user, atl_role, password, error)) {
				Cerr() << "Failed to write Atlas credentials: " << error << "\n";
				return false;
			}
			Cout() << "Configured Atlas account '" << atl_user << "'\n";
			fflush(stdout);
			_exit(0);
		}

		String ch_user = cla.GetArg("set-channels-user");
		if(!ch_user.IsEmpty()) {
			String password;
			if(!ReadEnginePassword("MANAGERSKYLARK_CHANNELS_PASSWORD", "New Channels password: ", password))
				return false;
			String error;
			if(!SetChannelsPassword(ch_user, password, error)) {
				Cerr() << "Failed to write Channels credentials: " << error << "\n";
				return false;
			}
			Cout() << "Configured Channels account '" << ch_user << "'\n";
			fflush(stdout);
			_exit(0);
		}

		String meta_user = cla.GetArg("set-meta-user");
		if(!meta_user.IsEmpty()) {
			String password;
			if(!ReadEnginePassword("MANAGERSKYLARK_META_PASSWORD", "New MetaAccount password: ", password))
				return false;
			String error;
			if(!SetMetaAccountPassword(meta_user, password, error)) {
				Cerr() << "Failed to write MetaAccount credentials: " << error << "\n";
				return false;
			}
			Cout() << "Configured MetaAccount account '" << meta_user << "'\n";
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
