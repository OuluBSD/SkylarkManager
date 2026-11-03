#ifndef _reference_ManagerSkylark_HostPlatform_h_
#define _reference_ManagerSkylark_HostPlatform_h_

#include "ManagerSkylark.h"
#include "SystemAuth.h"

NAMESPACE_UPP

struct HostRepoInfo : Moveable<HostRepoInfo> {
	String name;
	String owner;
	String path;
	String branch;
	String last_commit_hash;
	String last_commit_msg;
	Time   last_modified;
	bool   has_git = false;
	bool   is_dirty = false;
	bool   has_upp = false;
	Vector<String> upp_packages;

	HostRepoInfo() {}
	HostRepoInfo(const HostRepoInfo& o, int) {
		name = o.name; owner = o.owner; path = o.path; branch = o.branch;
		last_commit_hash = o.last_commit_hash; last_commit_msg = o.last_commit_msg;
		last_modified = o.last_modified; has_git = o.has_git; is_dirty = o.is_dirty;
		has_upp = o.has_upp; upp_packages <<= o.upp_packages;
	}
	HostRepoInfo(const HostRepoInfo& o) {
		name = o.name; owner = o.owner; path = o.path; branch = o.branch;
		last_commit_hash = o.last_commit_hash; last_commit_msg = o.last_commit_msg;
		last_modified = o.last_modified; has_git = o.has_git; is_dirty = o.is_dirty;
		has_upp = o.has_upp; upp_packages <<= o.upp_packages;
	}
	HostRepoInfo& operator=(const HostRepoInfo& o) {
		name = o.name; owner = o.owner; path = o.path; branch = o.branch;
		last_commit_hash = o.last_commit_hash; last_commit_msg = o.last_commit_msg;
		last_modified = o.last_modified; has_git = o.has_git; is_dirty = o.is_dirty;
		has_upp = o.has_upp; upp_packages <<= o.upp_packages;
		return *this;
	}
	HostRepoInfo(HostRepoInfo&&) = default;
	HostRepoInfo& operator=(HostRepoInfo&&) = default;
};

struct DistributedNode : Moveable<DistributedNode> {
	String id;
	String name;
	String hostname;
	String ssh_user;
	int    ssh_port = 22;
	String remote_path;
	String protocol; // "local", "ssh", "rsync"
	String status;   // "online", "syncing", "unreachable", "idle"
	bool   is_local = false;
	String os_info;
	String uptime;
	String load_avg;
	int    repo_count = 0;
	int    latency_ms = 0;
	Time   last_sync;
	Time   last_checked;

	DistributedNode() {
		ssh_user = "sblo";
		ssh_port = 22;
		remote_path = "~/Dev";
		protocol = "ssh";
		status = "idle";
	}

	void Jsonize(JsonIO& io) {
		io("id", id)("name", name)("hostname", hostname)("ssh_user", ssh_user)
		  ("ssh_port", ssh_port)("remote_path", remote_path)("protocol", protocol)
		  ("status", status)("is_local", is_local)("os_info", os_info)
		  ("uptime", uptime)("load_avg", load_avg)("repo_count", repo_count)
		  ("latency_ms", latency_ms)("last_sync", last_sync)("last_checked", last_checked);
	}
};

struct FleetGitNodeStatus : Moveable<FleetGitNodeStatus> {
	String node_id;
	String name;
	String hostname;
	String status; // "online", "unreachable"
	int    latency_ms = 0;
	String branch;
	String commit_hash;
	String commit_msg;
	String commit_time;
	bool   is_dirty = false;
	int    dirty_count = 0;
	String sync_state; // "synced", "ahead", "behind", "diverged", "unreachable"

	void Jsonize(JsonIO& io) {
		io("node_id", node_id)("name", name)("hostname", hostname)
		  ("status", status)("latency_ms", latency_ms)("branch", branch)
		  ("commit_hash", commit_hash)("commit_msg", commit_msg)
		  ("commit_time", commit_time)("is_dirty", is_dirty)
		  ("dirty_count", dirty_count)("sync_state", sync_state);
	}
};

struct FleetBuildResult : Moveable<FleetBuildResult> {
	String node_id;
	String package_name;
	String method;
	String flags;
	String command;
	String output;
	int    exit_code = 0;
	int    elapsed_ms = 0;
	bool   success = false;

	void Jsonize(JsonIO& io) {
		io("node_id", node_id)("package_name", package_name)("method", method)
		  ("flags", flags)("command", command)("output", output)
		  ("exit_code", exit_code)("elapsed_ms", elapsed_ms)("success", success);
	}
};

class HostPlatformService {
public:
	static Vector<HostRepoInfo>       GetLocalRepositories(const String& dev_dir = "");
	static Vector<HostRepoInfo>       GetRepositoriesForNode(const String& node_id);
	static Vector<DistributedNode>    GetConfiguredNodes(bool probe_all = false);
	static DistributedNode            GetNode(const String& node_id);
	static void                       SaveNodes(const Vector<DistributedNode>& nodes);
	static bool                       AddOrUpdateNode(const DistributedNode& node);
	static bool                       RemoveNode(const String& node_id);
	static bool                       ProbeNode(DistributedNode& node);
	static String                     ExecRemoteGit(const String& node_id, const String& repo_name, const String& git_args);
	static String                     TriggerSync(const String& source_node_id, const String& target_node_id, const String& repo_name);
	static Vector<FleetGitNodeStatus> GetFleetGitStatus(const String& repo_name = "ai-upp");
	static String                     FleetSync(const String& source_node_id, const String& target_node_id, const String& repo_name = "ai-upp", const String& mode = "git_pull");
	static FleetBuildResult           ExecRemoteBuild(const String& node_id, const String& package_name, const String& method = "CLANG", const String& flags = "-r");

	static String RenderSubNav(const String& active_tab, const String& node_id = "local-workstation");
	static String RenderOverview(Http& http, const String& node_id = "local-workstation");
	static String RenderFiles(Http& http, const String& node_id, const String& rel_path);
	static String RenderRepos(Http& http, const String& node_id = "local-workstation");
	static String RenderFleet(Http& http, const String& node_id = "local-workstation");
	static String RenderNodes(Http& http);
	static String RenderStorage(Http& http, const String& node_id = "local-workstation");
	static String RenderSettings(Http& http);
};

END_UPP_NAMESPACE

#endif
