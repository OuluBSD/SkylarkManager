#include "ManagerSkylark.h"
#include "HostPlatform.h"

NAMESPACE_UPP

// ============================================================================
// SKYLARK Handlers for /host Namespace (Local Workstation)
// ============================================================================

SKYLARK(HostOverviewPage, "host")
{
	String b = HostPlatformService::RenderOverview(http, "local-workstation");
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Host Workstation", "host", b, &http);
}

SKYLARK(HostOverviewDeepPage, "host/overview")
{
	String b = HostPlatformService::RenderOverview(http, "local-workstation");
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Host Workstation", "host", b, &http);
}

SKYLARK(HostFilesPage, "host/files/**")
{
	String rel;
	for(int i = 0; i < http.GetParamCount(); i++) {
		if(i > 0) rel << "/";
		rel << http[i];
	}

	String b = HostPlatformService::RenderFiles(http, "local-workstation", rel);
	if(!b.IsEmpty())
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Home Directory Files", "host_files", b, &http);
}

SKYLARK(HostReposPage, "host/repos")
{
	String b = HostPlatformService::RenderRepos(http, "local-workstation");
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Local Repositories", "host_repos", b, &http);
}

SKYLARK(HostNodesPage, "host/nodes")
{
	String b = HostPlatformService::RenderNodes(http);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Compute Mesh", "host_nodes", b, &http);
}

SKYLARK(HostSettingsPage, "host/settings")
{
	String b = HostPlatformService::RenderSettings(http);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Mesh Settings", "host_settings", b, &http);
}

SKYLARK(HostSettingsAddPost, "host/settings/add:POST_RAW")
{
	String body = http.GetRequestContent();
	VectorMap<String, String> form;
	Vector<String> pairs = Split(body, '&');
	for(const String& p : pairs) {
		int eq = p.Find('=');
		if(eq >= 0)
			form.Add(UrlDecode(p.Left(eq)), UrlDecode(p.Mid(eq + 1)));
	}

	DistributedNode n;
	n.id = form.Get("id", "");
	n.name = form.Get("name", "");
	n.hostname = form.Get("hostname", "");
	n.ssh_user = form.Get("ssh_user", "sblo");
	n.ssh_port = ScanInt(form.Get("ssh_port", "22"));
	n.remote_path = form.Get("remote_path", "~/Dev");
	n.protocol = form.Get("protocol", "ssh");

	if(!n.hostname.IsEmpty())
		HostPlatformService::AddOrUpdateNode(n);

	http.Redirect("/host/settings");
}

SKYLARK(HostSettingsRemovePost, "host/settings/remove:POST_RAW")
{
	String body = http.GetRequestContent();
	VectorMap<String, String> form;
	Vector<String> pairs = Split(body, '&');
	for(const String& p : pairs) {
		int eq = p.Find('=');
		if(eq >= 0)
			form.Add(UrlDecode(p.Left(eq)), UrlDecode(p.Mid(eq + 1)));
	}

	String id = form.Get("id", "");
	if(!id.IsEmpty())
		HostPlatformService::RemoveNode(id);

	http.Redirect("/host/settings");
}

SKYLARK(HostSettingsResetPost, "host/settings/reset:POST_RAW")
{
	String conf_path = AppendFileName(GetHomeDirFile(".config/managerskylark"), "nodes.json");
	if(FileExists(conf_path))
		FileDelete(conf_path);

	HostPlatformService::GetConfiguredNodes(true);
	http.Redirect("/host/settings");
}

SKYLARK(HostStoragePage, "host/storage")
{
	String b = HostPlatformService::RenderStorage(http, "local-workstation");
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Disk Storage", "host_storage", b, &http);
}

// ============================================================================
// SKYLARK Handlers for /node Namespace (Equal Parity Across All Mesh Computers)
// ============================================================================

SKYLARK(NodeOverviewDirect, "node/*")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderOverview(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Overview", "node_" + node_id + "_overview", b, &http);
}

SKYLARK(NodeOverviewPage, "node/*/overview")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderOverview(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Overview", "node_" + node_id + "_overview", b, &http);
}

SKYLARK(NodeFilesRootPage, "node/*/files")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderFiles(http, node_id, "");
	DistributedNode node = HostPlatformService::GetNode(node_id);
	if(!b.IsEmpty())
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Files", "node_" + node_id + "_files", b, &http);
}

SKYLARK(NodeFilesDeepPage, "node/*/files/**")
{
	String node_id = http[0];
	String rel;
	for(int i = 1; i < http.GetParamCount(); i++) {
		if(i > 1) rel << "/";
		rel << http[i];
	}

	String b = HostPlatformService::RenderFiles(http, node_id, rel);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	if(!b.IsEmpty())
		http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Files", "node_" + node_id + "_files", b, &http);
}

SKYLARK(NodeReposPage, "node/*/repos")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderRepos(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Repositories", "node_" + node_id + "_repos", b, &http);
}

SKYLARK(NodeStoragePage, "node/*/storage")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderStorage(http, node_id);
	DistributedNode node = HostPlatformService::GetNode(node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage(node.name + " Disk Storage", "node_" + node_id + "_storage", b, &http);
}

SKYLARK(ApiHostStatus, "api/host/status")
{
	ValueMap res;
	res.Set("hostname", GetComputerName());
	res.Set("cpu_cores", CPU_Cores());
	res.Set("repos_count", HostPlatformService::GetLocalRepositories().GetCount());
	res.Set("nodes_count", HostPlatformService::GetConfiguredNodes().GetCount());
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiHostRepos, "api/host/repos")
{
	Vector<HostRepoInfo> repos = HostPlatformService::GetLocalRepositories();
	ValueArray va;
	for(int i = 0; i < repos.GetCount(); i++) {
		ValueMap m;
		m.Set("name", repos[i].name);
		m.Set("branch", repos[i].branch);
		m.Set("has_git", repos[i].has_git);
		m.Set("has_upp", repos[i].has_upp);
		m.Set("commit", repos[i].last_commit_hash);
		va.Add(m);
	}
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiNodeRepos, "api/node/*/repos")
{
	String node_id = http[0];
	Vector<HostRepoInfo> repos = HostPlatformService::GetRepositoriesForNode(node_id);
	ValueArray va;
	for(int i = 0; i < repos.GetCount(); i++) {
		ValueMap m;
		m.Set("name", repos[i].name);
		m.Set("branch", repos[i].branch);
		m.Set("has_git", repos[i].has_git);
		m.Set("has_upp", repos[i].has_upp);
		m.Set("commit", repos[i].last_commit_hash);
		m.Set("msg", repos[i].last_commit_msg);
		va.Add(m);
	}
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiNodeStatus, "api/node/*/status")
{
	String node_id = http[0];
	DistributedNode node = HostPlatformService::GetNode(node_id);
	HostPlatformService::ProbeNode(node);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(node));
}

SKYLARK(ApiHostNodesList, "api/host/nodes")
{
	bool probe = (http["probe"] == "1" || http["probe"] == "true");
	Vector<DistributedNode> nodes = HostPlatformService::GetConfiguredNodes(probe);
	ValueArray va;
	for(int i = 0; i < nodes.GetCount(); i++)
		va.Add(StoreAsJsonValue(nodes[i]));
	http.ContentType("application/json") << AsJSON(va);
}

SKYLARK(ApiHostNodesAdd, "api/host/nodes/add:POST_RAW")
{
	String body = http.GetRequestContent();
	DistributedNode node;
	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		LoadFromJsonValue(node, v);
	} else {
		// Form encoded fallback
		Vector<String> pairs = Split(body, '&');
		for(const String& p : pairs) {
			int eq = p.Find('=');
			if(eq >= 0) {
				String k = UrlDecode(p.Left(eq));
				String val = UrlDecode(p.Mid(eq + 1));
				if(k == "id") node.id = val;
				else if(k == "name") node.name = val;
				else if(k == "hostname") node.hostname = val;
				else if(k == "ssh_user") node.ssh_user = val;
				else if(k == "ssh_port") node.ssh_port = ScanInt(val);
				else if(k == "remote_path") node.remote_path = val;
				else if(k == "protocol") node.protocol = val;
			}
		}
	}

	if(node.hostname.IsEmpty()) {
		ValueMap err;
		err.Set("status", "error");
		err.Set("message", "Missing hostname / IP");
		http.ContentType("application/json") << AsJSON(err);
		return;
	}

	HostPlatformService::AddOrUpdateNode(node);
	ValueMap res;
	res.Set("status", "ok");
	res.Set("node_id", node.id);
	res.Set("latency_ms", node.latency_ms);
	res.Set("node_status", node.status);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiHostNodesRemove, "api/host/nodes/remove:POST_RAW")
{
	String body = http.GetRequestContent();
	String node_id;
	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		node_id = v["id"];
		if(node_id.IsEmpty()) node_id = v["node_id"];
	} else {
		Vector<String> pairs = Split(body, '&');
		for(const String& p : pairs) {
			int eq = p.Find('=');
			if(eq >= 0 && UrlDecode(p.Left(eq)) == "id")
				node_id = UrlDecode(p.Mid(eq + 1));
		}
	}

	bool ok = HostPlatformService::RemoveNode(node_id);
	ValueMap res;
	res.Set("status", ok ? "ok" : "not_found");
	res.Set("node_id", node_id);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiHostNodesProbe, "api/host/nodes/probe/*")
{
	String node_id = http[0];
	Vector<DistributedNode> nodes = HostPlatformService::GetConfiguredNodes(false);
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node_id || nodes[i].hostname == node_id) {
			HostPlatformService::ProbeNode(nodes[i]);
			HostPlatformService::SaveNodes(nodes);
			http.ContentType("application/json") << AsJSON(StoreAsJsonValue(nodes[i]));
			return;
		}
	}

	ValueMap err;
	err.Set("status", "not_found");
	err.Set("node_id", node_id);
	http.ContentType("application/json") << AsJSON(err);
}

SKYLARK(ApiHostRemoteGit, "api/host/remotegit:POST_RAW")
{
	String body = http.GetRequestContent();
	String node_id = "local-workstation";
	String repo = "ai-upp";
	String args = "status -s";

	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		if(!v["node_id"].IsNull()) node_id = v["node_id"];
		if(!v["repo"].IsNull()) repo = v["repo"];
		if(!v["args"].IsNull()) args = v["args"];
	} else {
		Vector<String> pairs = Split(body, '&');
		for(const String& p : pairs) {
			int eq = p.Find('=');
			if(eq >= 0) {
				String k = UrlDecode(p.Left(eq));
				String val = UrlDecode(p.Mid(eq + 1));
				if(k == "node_id") node_id = val;
				else if(k == "repo") repo = val;
				else if(k == "args") args = val;
			}
		}
	}

	String out = HostPlatformService::ExecRemoteGit(node_id, repo, args);
	ValueMap res;
	res.Set("status", "ok");
	res.Set("node_id", node_id);
	res.Set("repo", repo);
	res.Set("args", args);
	res.Set("output", out);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiHostSync, "api/host/sync:POST_RAW")
{
	String body = http.GetRequestContent();
	String src = "local-workstation";
	String dst = "mediacenter-gentoo";
	String repo = "ai-upp";

	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		if(!v["source"].IsNull()) src = v["source"];
		if(!v["target"].IsNull()) dst = v["target"];
		if(!v["repo"].IsNull()) repo = v["repo"];
	} else {
		Vector<String> pairs = Split(body, '&');
		for(const String& p : pairs) {
			int eq = p.Find('=');
			if(eq >= 0) {
				String k = UrlDecode(p.Left(eq));
				String val = UrlDecode(p.Mid(eq + 1));
				if(k == "source") src = val;
				else if(k == "target") dst = val;
				else if(k == "repo") repo = val;
			}
		}
	}

	String out = HostPlatformService::TriggerSync(src, dst, repo);
	ValueMap res;
	res.Set("status", "ok");
	res.Set("source", src);
	res.Set("target", dst);
	res.Set("repo", repo);
	res.Set("output", out);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(HostFleetPage, "host/fleet")
{
	String b = HostPlatformService::RenderFleet(http, "local-workstation");
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Fleet Sync & Build", "host_fleet", b, &http);
}

SKYLARK(NodeFleetPage, "node/*/fleet")
{
	String node_id = http[0];
	String b = HostPlatformService::RenderFleet(http, node_id);
	http.ContentType("text/html; charset=utf-8") << ManagerSkylarkApp::RenderHtmlPage("Fleet Sync & Build - " + node_id, "node_" + node_id + "_fleet", b, &http);
}

SKYLARK(ApiHostFleetStatus, "api/host/fleet/status")
{
	String repo = http.GetParamCount() > 0 ? (String)http[0] : "ai-upp";
	Vector<FleetGitNodeStatus> fleet = HostPlatformService::GetFleetGitStatus(repo);
	ValueArray va;
	for(int i = 0; i < fleet.GetCount(); i++)
		va.Add(StoreAsJsonValue(fleet[i]));
	http.ContentType("application/json") << AsJSON(va, true);
}

SKYLARK(ApiHostFleetSync, "api/host/fleet/sync:POST_RAW")
{
	String body = http.GetRequestContent();
	String src = "local-workstation";
	String target = "all";
	String repo = "ai-upp";
	String mode = "git_pull";

	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		if(!v["source"].IsNull()) src = v["source"];
		if(!v["target_node"].IsNull()) target = v["target_node"];
		if(!v["repo"].IsNull()) repo = v["repo"];
		if(!v["mode"].IsNull()) mode = v["mode"];
	}

	String out = HostPlatformService::FleetSync(src, target, repo, mode);
	ValueMap res;
	res.Set("status", "ok");
	res.Set("source", src);
	res.Set("target_node", target);
	res.Set("repo", repo);
	res.Set("mode", mode);
	res.Set("output", out);
	http.ContentType("application/json") << AsJSON(res);
}

SKYLARK(ApiHostFleetBuild, "api/host/fleet/build:POST_RAW")
{
	String body = http.GetRequestContent();
	String node_id = "local-workstation";
	String pkg = "reference/ManagerSkylark";
	String method = "CLANG";
	String flags = "-r";

	Value v = ParseJSON(body);
	if(v.Is<ValueMap>()) {
		if(!v["node_id"].IsNull()) node_id = v["node_id"];
		if(!v["package"].IsNull()) pkg = v["package"];
		if(!v["method"].IsNull()) method = v["method"];
		if(!v["flags"].IsNull()) flags = v["flags"];
	}

	FleetBuildResult res = HostPlatformService::ExecRemoteBuild(node_id, pkg, method, flags);
	http.ContentType("application/json") << AsJSON(StoreAsJsonValue(res));
}

END_UPP_NAMESPACE
