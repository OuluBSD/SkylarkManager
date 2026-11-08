#include "HostPlatform.h"
#include "HostExec.h"
#include <sys/stat.h>
#ifdef PLATFORM_POSIX
#include <sys/statvfs.h>
#endif

NAMESPACE_UPP

// "git -C <r_dir> <tail> 2>&1" on a remote node, through ssh. Every part is checked first.
static String RunRemoteGit(int port, const String& user, const String& host, const String& r_dir, const String& tail, int connect_timeout)
{
	if(!HostExec::SafeUser(user) || !HostExec::SafeHost(host) || !HostExec::SafeRemotePath(r_dir))
		return "Error: invalid node address.";
	Vector<String> ssh_args = HostExec::SshArgs(port, user, host, connect_timeout);
	ssh_args.Add("git -C " + r_dir + " " + tail + " 2>&1");
	return HostExec::Run("ssh", ssh_args, false);
}


Vector<HostRepoInfo> HostPlatformService::GetLocalRepositories(const String& dev_dir)
{
	Vector<HostRepoInfo> repos;
	String base_dir = dev_dir;
	if(base_dir.IsEmpty())
		base_dir = AppendFileName(GetHomeDirFile(""), "Dev");

	if(!DirectoryExists(base_dir))
		return repos;

	FindFile ff(AppendFileName(base_dir, "*"));
	while(ff) {
		if(ff.IsFolder() && !ff.GetName().StartsWith(".")) {
			HostRepoInfo& info = repos.Add();
			info.name = ff.GetName();
			info.path = ff.GetPath();
			info.last_modified = ff.GetLastWriteTime();

			// Git inspection
			String git_dir = AppendFileName(info.path, ".git");
			if(DirectoryExists(git_dir)) {
				info.has_git = true;
				String cfg = LoadFile(AppendFileName(git_dir, "config"));
				if(cfg.Find("OuluBSD") >= 0)
					info.owner = "OuluBSD";
				else if(cfg.Find("SeppoPakonen") >= 0)
					info.owner = "SeppoPakonen";
				String head_file = AppendFileName(git_dir, "HEAD");
				if(FileExists(head_file)) {
					String head_content = TrimBoth(LoadFile(head_file));
					if(head_content.StartsWith("ref: refs/heads/")) {
						info.branch = head_content.Mid(16);
						String ref_file = AppendFileName(git_dir, head_content.Mid(5));
						if(FileExists(ref_file))
							info.last_commit_hash = TrimBoth(LoadFile(ref_file)).Left(8);
					} else {
						info.branch = "HEAD (detached)";
						info.last_commit_hash = head_content.Left(8);
					}
				}
			}

			// U++ inspection
			FindFile uff(AppendFileName(info.path, "*/*.upp"));
			while(uff) {
				info.has_upp = true;
				info.upp_packages.Add(GetFileTitle(uff.GetName()));
				uff.Next();
			}
		}
		ff.Next();
	}

	Sort(repos, [](const HostRepoInfo& a, const HostRepoInfo& b) {
		return a.name < b.name;
	});

	return repos;
}

Vector<DistributedNode> HostPlatformService::GetConfiguredNodes(bool probe_all)
{
	Vector<DistributedNode> nodes;
	String conf_path = AppendFileName(GetHomeDirFile(".config/managerskylark"), "nodes.json");
	bool loaded = false;

	if(FileExists(conf_path)) {
		Value v = ParseJSON(LoadFile(conf_path));
		if(v.Is<ValueArray>()) {
			ValueArray va = v;
			for(int i = 0; i < va.GetCount(); i++) {
				DistributedNode node;
				LoadFromJsonValue(node, va[i]);
				nodes.Add(node);
			}
			if(!nodes.IsEmpty())
				loaded = true;
		}
	}

	if(!loaded) {
		// Default verified cluster fleet: 192.168.1.106, 192.168.1.105, 192.168.1.169
		DistributedNode local;
		local.id = "local-workstation";
		local.name = "Primary Workstation";
		local.hostname = "192.168.1.106";
		local.ssh_user = "sblo";
		local.ssh_port = 22;
		local.protocol = "local";
		local.remote_path = AppendFileName(GetHomeDirFile(""), "Dev");
		local.status = "online";
		local.is_local = true;
		local.latency_ms = 0;
		local.last_sync = GetSysTime();
		nodes.Add(local);

		DistributedNode gentoo;
		gentoo.id = "mediacenter-gentoo";
		gentoo.name = "MediaCenter FX-8350";
		gentoo.hostname = "192.168.1.105";
		gentoo.ssh_user = "sblo";
		gentoo.ssh_port = 22;
		gentoo.protocol = "ssh";
		gentoo.remote_path = "~/Dev";
		gentoo.status = "online";
		gentoo.is_local = false;
		gentoo.last_sync = GetSysTime() - 3600;
		nodes.Add(gentoo);

		DistributedNode gen7;
		gen7.id = "gen7-lan";
		gen7.name = "Gen7 Server FX-6300";
		gen7.hostname = "192.168.1.169";
		gen7.ssh_user = "sblo";
		gen7.ssh_port = 22;
		gen7.protocol = "ssh";
		gen7.remote_path = "~/Dev";
		gen7.status = "online";
		gen7.is_local = false;
		gen7.last_sync = GetSysTime() - 7200;
		nodes.Add(gen7);

		SaveNodes(nodes);
	}

	if(probe_all) {
		for(int i = 0; i < nodes.GetCount(); i++)
			ProbeNode(nodes[i]);
		SaveNodes(nodes);
	}

	return nodes;
}

DistributedNode HostPlatformService::GetNode(const String& node_id)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node_id || nodes[i].hostname == node_id)
			return nodes[i];
	}
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].is_local)
			return nodes[i];
	}
	DistributedNode def;
	def.id = "local-workstation";
	def.name = "Primary Workstation";
	def.hostname = "192.168.1.106";
	def.is_local = true;
	return def;
}

struct CachedNodeRepos : Moveable<CachedNodeRepos> {
	int64 timestamp = 0;
	Vector<HostRepoInfo> repos;
};
static VectorMap<String, CachedNodeRepos> s_node_repo_cache;

Vector<HostRepoInfo> HostPlatformService::GetRepositoriesForNode(const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	if(node.is_local || node.protocol == "local" || node.hostname == "192.168.1.106" || node.hostname == "127.0.0.1") {
		return GetLocalRepositories(node.remote_path);
	}

	int q = s_node_repo_cache.Find(node_id);
	int64 now = msecs();
	if(q >= 0 && (now - s_node_repo_cache[q].timestamp) < 60000) {
		return clone(s_node_repo_cache[q].repos);
	}

	Vector<HostRepoInfo> repos;
	String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
	int port = node.ssh_port > 0 ? node.ssh_port : 22;
	String remote_dev = node.remote_path.IsEmpty() ? "~/Dev" : node.remote_path;

	if(!HostExec::SafeHost(node.hostname) || !HostExec::SafeUser(user) || !HostExec::SafeRemotePath(remote_dev)) return repos;
	String script = "for d in " + remote_dev + "/*; do if [ -d \"$d/.git\" ]; then "
	                "b=$(git -C \"$d\" branch --show-current 2>/dev/null || echo master); "
	                "h=$(git -C \"$d\" log -1 --format='%h' 2>/dev/null || echo '-'); "
	                "m=$(git -C \"$d\" log -1 --format='%s' 2>/dev/null || echo '-'); "
	                "u=$(find \"$d\" -maxdepth 2 -name '*.upp' 2>/dev/null | wc -l); "
	                "echo \"$(basename \"$d\")|$b|$h|$m|$u\"; "
	                "fi; done";
	Vector<String> ssh_args = HostExec::SshArgs(port, user, node.hostname, 3);
	ssh_args.Add(script);
	String out = HostExec::Run("ssh", ssh_args, false);
	Vector<String> lines = Split(out, '\n');
	for(int i = 0; i < lines.GetCount(); i++) {
		Vector<String> parts = Split(lines[i], '|');
		if(parts.GetCount() >= 4) {
			HostRepoInfo& info = repos.Add();
			info.name = parts[0];
			info.path = remote_dev + "/" + parts[0];
			info.branch = parts[1];
			info.last_commit_hash = parts[2];
			info.last_commit_msg = parts[3];
			info.has_git = true;
			int upp_cnt = (parts.GetCount() > 4) ? ScanInt(parts[4]) : 0;
			if(upp_cnt > 0) {
				info.has_upp = true;
				for(int k = 0; k < upp_cnt; k++)
					info.upp_packages.Add("pkg");
			}
		}
	}

	Sort(repos, [](const HostRepoInfo& a, const HostRepoInfo& b) {
		return a.name < b.name;
	});

	CachedNodeRepos& c = s_node_repo_cache.GetAdd(node_id);
	c.timestamp = now;
	c.repos = clone(repos);

	return repos;
}

void HostPlatformService::SaveNodes(const Vector<DistributedNode>& nodes)
{
	String dir = GetHomeDirFile(".config/managerskylark");
	RealizeDirectory(dir);
	String path = AppendFileName(dir, "nodes.json");

	ValueArray va;
	for(int i = 0; i < nodes.GetCount(); i++)
		va.Add(StoreAsJsonValue(nodes[i]));

	SaveFile(path, AsJSON(va, true));
}

bool HostPlatformService::ProbeNode(DistributedNode& node)
{
	int64 t0 = msecs();
	node.last_checked = GetSysTime();

	if(node.is_local || node.protocol == "local" || node.hostname == "127.0.0.1" || node.hostname == "localhost" || node.hostname == "192.168.1.106") {
		node.is_local = true;
		node.latency_ms = 0;
		node.status = "online";

		if(FileExists("/proc/version")) {
			String kv = LoadFile("/proc/version");
			int sp1 = kv.Find("version ");
			if(sp1 >= 0) {
				int sp2 = kv.Find(" ", sp1 + 8);
				if(sp2 > sp1)
					node.os_info = "Linux " + kv.Mid(sp1 + 8, sp2 - sp1 - 8);
			}
		}
		if(node.os_info.IsEmpty()) node.os_info = "Linux x86_64";

		if(FileExists("/proc/uptime")) {
			double up_secs = ScanDouble(LoadFile("/proc/uptime"));
			int hours = (int)(up_secs / 3600);
			int mins = (int)((up_secs - hours * 3600) / 60);
			int days = hours / 24;
			hours = hours % 24;
			if(days > 0)
				node.uptime = Format("%d`d %d`h %d`m", days, hours, mins);
			else
				node.uptime = Format("%d`h %d`m", hours, mins);
		}

		if(FileExists("/proc/loadavg")) {
			String la = LoadFile("/proc/loadavg");
			Vector<String> parts = Split(la, ' ');
			if(parts.GetCount() >= 3)
				node.load_avg = parts[0] + " " + parts[1] + " " + parts[2];
		}

		node.repo_count = GetLocalRepositories().GetCount();
		return true;
	}

	// Remote SSH Node Probe
	String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
	int port = node.ssh_port > 0 ? node.ssh_port : 22;
	String remote_dev = node.remote_path.IsEmpty() ? "~/Dev" : node.remote_path;

	if(!HostExec::SafeHost(node.hostname) || !HostExec::SafeUser(user) || !HostExec::SafeRemotePath(remote_dev)) {
		node.status = "unreachable";
		return false;
	}
	Vector<String> ssh_args = HostExec::SshArgs(port, user, node.hostname, 2);
	ssh_args.Add("echo PING_OK && uname -s -r -m && uptime && (ls -1 " + remote_dev + " 2>/dev/null | wc -l)");
	String out = HostExec::Run("ssh", ssh_args, false);
	int64 elapsed = msecs() - t0;
	node.latency_ms = (int)max((int64)1, elapsed);

	Vector<String> lines = Split(out, '\n');
	if(lines.GetCount() > 0 && lines[0] == "PING_OK") {
		node.status = "online";
		if(lines.GetCount() > 1) node.os_info = TrimBoth(lines[1]);
		if(lines.GetCount() > 2) {
			String up_line = lines[2];
			int up_idx = up_line.Find("up ");
			if(up_idx >= 0) {
				int comma = up_line.Find(",", up_idx);
				if(comma > up_idx) {
					int comma2 = up_line.Find(",", comma + 1);
					if(comma2 > comma && up_line.Mid(comma, comma2 - comma).Find("user") < 0)
						node.uptime = TrimBoth(up_line.Mid(up_idx + 3, comma2 - up_idx - 3));
					else
						node.uptime = TrimBoth(up_line.Mid(up_idx + 3, comma - up_idx - 3));
				}
			}
			int load_idx = up_line.Find("load average:");
			if(load_idx >= 0)
				node.load_avg = TrimBoth(up_line.Mid(load_idx + 13));
		}
		if(lines.GetCount() > 3)
			node.repo_count = ScanInt(TrimBoth(lines[3]));

		return true;
	} else {
		node.status = "unreachable";
		return false;
	}
}

bool HostPlatformService::AddOrUpdateNode(const DistributedNode& new_node)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	DistributedNode node = new_node;
	if(node.id.IsEmpty())
		node.id = ToLower(node.hostname);

	ProbeNode(node);

	bool found = false;
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node.id || nodes[i].hostname == node.hostname) {
			nodes[i] = node;
			found = true;
			break;
		}
	}
	if(!found)
		nodes.Add(node);

	SaveNodes(nodes);
	return true;
}

bool HostPlatformService::RemoveNode(const String& node_id)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node_id || nodes[i].hostname == node_id) {
			nodes.Remove(i);
			SaveNodes(nodes);
			return true;
		}
	}
	return false;
}

String HostPlatformService::ExecRemoteGit(const String& node_id, const String& repo_name, const String& git_args)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	const DistributedNode* target = nullptr;
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == node_id || nodes[i].hostname == node_id) {
			target = &nodes[i];
			break;
		}
	}
	if(!target)
		return "Error: Node '" + node_id + "' not found in cluster configuration.";

	if(target->is_local || target->protocol == "local" || target->hostname == "192.168.1.106" || target->hostname == "127.0.0.1") {
		if(!HostExec::SafeRepoName(repo_name)) return "Error: invalid repository name.";
		String repo_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name);
		if(!DirectoryExists(repo_dir))
			return "Error: Repository '" + repo_name + "' does not exist locally at " + repo_dir;
		Vector<String> git_argv;
		if(!HostExec::SafeGitArgs(git_args, git_argv)) return "Error: invalid git arguments.";
		Vector<String> argv;
		argv.Add("-C");
		argv.Add(repo_dir);
		argv.Append(git_argv);
		return HostExec::Run("git", argv, true);
	}

	String user = target->ssh_user.IsEmpty() ? "sblo" : target->ssh_user;
	int port = target->ssh_port > 0 ? target->ssh_port : 22;
	String remote_repo = target->remote_path + "/" + repo_name;
	if(!HostExec::SafeUser(user) || !HostExec::SafeHost(target->hostname) || !HostExec::SafeRepoName(repo_name) || !HostExec::SafeRemotePath(remote_repo))
		return "Error: invalid node address or repository name.";
	Vector<String> git_argv;
	if(!HostExec::SafeGitArgs(git_args, git_argv)) return "Error: invalid git arguments.";
	Vector<String> ssh_args = HostExec::SshArgs(port, user, target->hostname, 3);
	ssh_args.Add("git -C " + remote_repo + " " + Join(git_argv, " ") + " 2>&1");
	return HostExec::Run("ssh", ssh_args, false);
}

String HostPlatformService::TriggerSync(const String& source_node_id, const String& target_node_id, const String& repo_name)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	const DistributedNode* src = nullptr;
	const DistributedNode* dst = nullptr;
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].id == source_node_id || nodes[i].hostname == source_node_id) src = &nodes[i];
		if(nodes[i].id == target_node_id || nodes[i].hostname == target_node_id) dst = &nodes[i];
	}
	if(!src) return "Error: Source node '" + source_node_id + "' not found.";
	if(!dst) return "Error: Target node '" + target_node_id + "' not found.";

	String src_path, dst_path;
	if(src->is_local || src->hostname == "192.168.1.106")
		src_path = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name) + "/";
	else
		src_path = Format("%s@%s:%s/%s/", src->ssh_user, src->hostname, src->remote_path, repo_name);

	if(dst->is_local || dst->hostname == "192.168.1.106")
		dst_path = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name) + "/";
	else
		dst_path = Format("%s@%s:%s/%s/", dst->ssh_user, dst->hostname, dst->remote_path, repo_name);

	if(!HostExec::SafeRepoName(repo_name)) return "Error: invalid repository name.";
	for(const DistributedNode* n : { src, dst }) {
		if(n->is_local || n->hostname == "192.168.1.106") continue;
		if(!HostExec::SafeUser(n->ssh_user) || !HostExec::SafeHost(n->hostname) || !HostExec::SafeRemotePath(n->remote_path))
			return "Error: invalid node address.";
	}
	Vector<String> rsync_args;
	rsync_args.Add("-avz");
	rsync_args.Add("--exclude=.git");
	rsync_args.Add("-e");
	rsync_args.Add("ssh -o BatchMode=yes -o ConnectTimeout=3");
	rsync_args.Add(src_path);
	rsync_args.Add(dst_path);
	String out = HostExec::Run("rsync", rsync_args, true);
	return "Mesh Sync Runner (" + src->name + " -> " + dst->name + " / " + repo_name + "):\n" + out;
}

Vector<FleetGitNodeStatus> HostPlatformService::GetFleetGitStatus(const String& repo_name)
{
	Vector<FleetGitNodeStatus> list;
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);
	String local_head_hash;

	// First pass: local workstation
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].is_local || nodes[i].hostname == "192.168.1.106" || nodes[i].hostname == "127.0.0.1") {
			FleetGitNodeStatus& st = list.Add();
			st.node_id = nodes[i].id;
			st.name = nodes[i].name;
			st.hostname = nodes[i].hostname;
			st.status = "online";
			st.latency_ms = 0;

			String repo_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name);
			bool repo_ok = HostExec::SafeRepoName(repo_name);
			if(repo_ok && DirectoryExists(AppendFileName(repo_dir, ".git"))) {
				st.branch = TrimBoth(HostExec::Run("git", {"-C", repo_dir, "branch", "--show-current"}, false));
				if(st.branch.IsEmpty()) st.branch = "master";

				String log_str = TrimBoth(HostExec::Run("git", {"-C", repo_dir, "log", "-1", "--format=%h|%s|%cr|%H"}, false));
				Vector<String> lp = Split(log_str, '|');
				if(lp.GetCount() >= 4) {
					st.commit_hash = lp[0];
					st.commit_msg = lp[1];
					st.commit_time = lp[2];
					local_head_hash = lp[3];
				}
				String status_out = HostExec::Run("git", {"-C", repo_dir, "status", "-s"}, false);
				Vector<String> dirty_lines = Split(status_out, '\n');
				st.dirty_count = 0;
				for(int d = 0; d < dirty_lines.GetCount(); d++) {
					if(!TrimBoth(dirty_lines[d]).IsEmpty()) st.dirty_count++;
				}
				st.is_dirty = (st.dirty_count > 0);
				st.sync_state = "synced";
			} else {
				st.branch = "not found";
				st.commit_hash = "-";
				st.commit_msg = "Repo directory not found";
				st.sync_state = "unreachable";
			}
			break;
		}
	}

	// Second pass: remote nodes
	for(int i = 0; i < nodes.GetCount(); i++) {
		if(nodes[i].is_local || nodes[i].hostname == "192.168.1.106" || nodes[i].hostname == "127.0.0.1")
			continue;

		const DistributedNode& n = nodes[i];
		FleetGitNodeStatus& st = list.Add();
		st.node_id = n.id;
		st.name = n.name;
		st.hostname = n.hostname;

		int64 t0 = msecs();
		int port = n.ssh_port > 0 ? n.ssh_port : 22;
		String user = n.ssh_user.IsEmpty() ? "sblo" : n.ssh_user;
		String r_dir = (n.remote_path.IsEmpty() ? "~/Dev" : n.remote_path) + "/" + repo_name;
		String out;
		if(HostExec::SafeUser(user) && HostExec::SafeHost(n.hostname) && HostExec::SafeRepoName(repo_name) && HostExec::SafeRemotePath(r_dir)) {
			String ssh_script = "echo PING && git -C " + r_dir + " branch --show-current 2>/dev/null && "
			                    "git -C " + r_dir + " log -1 --format='%h|%s|%cr|%H' 2>/dev/null && "
			                    "git -C " + r_dir + " status -s 2>/dev/null | wc -l";
			Vector<String> ssh_args = HostExec::SshArgs(port, user, n.hostname, 2);
			ssh_args.Add(ssh_script);
			out = HostExec::Run("ssh", ssh_args, false);
		}
		st.latency_ms = (int)max((int64)1, msecs() - t0);

		Vector<String> lines = Split(out, '\n');
		if(lines.GetCount() >= 2 && lines[0] == "PING") {
			st.status = "online";
			st.branch = TrimBoth(lines[1]);
			if(st.branch.IsEmpty()) st.branch = "master";

			String remote_full_hash;
			if(lines.GetCount() >= 3) {
				Vector<String> lp = Split(lines[2], '|');
				if(lp.GetCount() >= 4) {
					st.commit_hash = lp[0];
					st.commit_msg = lp[1];
					st.commit_time = lp[2];
					remote_full_hash = lp[3];
				}
			}
			if(lines.GetCount() >= 4) {
				st.dirty_count = ScanInt(TrimBoth(lines[3]));
				st.is_dirty = (st.dirty_count > 0);
			}

			if(!remote_full_hash.IsEmpty() && !local_head_hash.IsEmpty()) {
				if(remote_full_hash == local_head_hash) {
					st.sync_state = "synced";
				} else {
					String repo_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name);
					String test_behind = HostExec::SafeHash(remote_full_hash) ? HostExec::Run("git", {"-C", repo_dir, "branch", "--contains", remote_full_hash}, false) : String();
					if(!test_behind.IsEmpty()) {
						st.sync_state = "behind";
					} else {
						st.sync_state = "diverged";
					}
				}
			} else {
				st.sync_state = "unreachable";
			}
		} else {
			st.status = "unreachable";
			st.branch = "-";
			st.commit_hash = "-";
			st.commit_msg = "Node unreachable or repository missing";
			st.sync_state = "unreachable";
		}
	}

	return list;
}

String HostPlatformService::FleetSync(const String& source_node_id, const String& target_node_id, const String& repo_name, const String& mode)
{
	String log;
	log << "=== Mesh Fleet Git Synchronization ===\n";
	log << "Timestamp: " << Format(GetSysTime()) << "\n";
	log << "Repository: " << repo_name << "\n";
	log << "Action Mode: " << mode << "\n\n";

	Vector<DistributedNode> nodes = GetConfiguredNodes(false);

	Vector<String> targets;
	if(target_node_id == "all" || target_node_id.IsEmpty()) {
		for(int i = 0; i < nodes.GetCount(); i++) {
			if(!nodes[i].is_local && nodes[i].hostname != "192.168.1.106")
				targets.Add(nodes[i].id);
		}
	} else {
		targets.Add(target_node_id);
	}

	if(!HostExec::SafeRepoName(repo_name)) {
		log << "Error: invalid repository name.\n";
		return log;
	}
	for(int t = 0; t < targets.GetCount(); t++) {
		const String& tid = targets[t];
		const DistributedNode* node = nullptr;
		for(int i = 0; i < nodes.GetCount(); i++) {
			if(nodes[i].id == tid) { node = &nodes[i]; break; }
		}
		if(!node) continue;

		log << "----------------------------------------\n";
		log << "Target: " << node->name << " (" << node->hostname << ")\n";

		int port = node->ssh_port > 0 ? node->ssh_port : 22;
		String user = node->ssh_user.IsEmpty() ? "sblo" : node->ssh_user;
		String r_dir = (node->remote_path.IsEmpty() ? "~/Dev" : node->remote_path) + "/" + repo_name;
		if(!HostExec::SafeUser(user) || !HostExec::SafeHost(node->hostname) || !HostExec::SafeRepoName(repo_name) || !HostExec::SafeRemotePath(r_dir)) {
			log << "[skipped]: invalid node address or repository name.\n";
			continue;
		}

		if(mode == "git_pull" || mode == "sync_all") {
			String out = RunRemoteGit(port, user, node->hostname, r_dir, "pull", 4);
			log << "[SSH git pull origin]:\n" << out << "\n";
		}
		else if(mode == "git_fetch") {
			String out = RunRemoteGit(port, user, node->hostname, r_dir, "fetch origin", 4);
			log << "[SSH git fetch origin]:\n" << out << "\n";
		}
		else if(mode == "rsync") {
			String local_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name) + "/";
			String dst_path = Format("%s@%s:%s/", user, node->hostname, r_dir);
			Vector<String> rsync_args;
			rsync_args.Add("-avz");
			rsync_args.Add("--exclude=.git");
			rsync_args.Add("-e");
			rsync_args.Add("ssh -o BatchMode=yes -o ConnectTimeout=4 -p " + AsString(port));
			rsync_args.Add(local_dir);
			rsync_args.Add(dst_path);
			String out = HostExec::Run("rsync", rsync_args, true);
			log << "[rsync working tree mirror]:\n" << out << "\n";
		}
		else if(mode == "git_push_from_local") {
			String local_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name);
			String remote_url = Format("ssh://%s@%s:%d%s/%s", user, node->hostname, port, node->remote_path.StartsWith("~") ? "" : "", r_dir);
			String out = HostExec::Run("git", {"-C", local_dir, "push", remote_url, "HEAD:master"}, true);
			log << "[git push to remote]:\n" << out << "\n";
		}
	}

	log << "\nFleet synchronization completed.\n";
	return log;
}

FleetBuildResult HostPlatformService::ExecRemoteBuild(const String& node_id, const String& package_name, const String& method, const String& flags)
{
	FleetBuildResult res;
	res.node_id = node_id;
	res.package_name = package_name.IsEmpty() ? "reference/ManagerSkylark" : package_name;
	res.method = method.IsEmpty() ? "CLANG" : method;
	res.flags = flags;

	int64 t0 = msecs();
	DistributedNode node = GetNode(node_id);

	if(node.is_local || node.hostname == "192.168.1.106" || node.hostname == "127.0.0.1") {
		String upp_dir = AppendFileName(GetHomeDirFile(""), "Dev/ai-upp");
		Vector<String> flag_tokens;
		bool args_ok = HostExec::SafePackageName(res.package_name) && HostExec::SafeBuildMethod(res.method) &&
		               HostExec::SafeBuildFlags(res.flags, flag_tokens);
		if(!args_ok) {
			res.output = "Error: invalid package, method or flags.";
		} else {
			Vector<String> build_args;
			build_args.Add(res.package_name);
			build_args.Add("-m");
			build_args.Add(res.method);
			build_args.Append(flag_tokens);
			res.command = "cd \"" + upp_dir + "\" && ./bin/build " + res.package_name + " -m " + res.method + " " + Join(flag_tokens, " ") + " 2>&1";
			res.output = HostExec::Run(AppendFileName(upp_dir, "bin/build"), build_args, true, upp_dir);
		}
		res.exit_code = 0;
		if(res.output.Find("Unable to locate") >= 0 || res.output.Find("error:") >= 0 || res.output.Find("Error:") >= 0)
			res.exit_code = 1;
		res.success = (res.exit_code == 0);
	} else {
		int port = node.ssh_port > 0 ? node.ssh_port : 22;
		String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
		String r_dir = (node.remote_path.IsEmpty() ? "~/Dev" : node.remote_path) + "/ai-upp";

		Vector<String> flag_tokens;
		bool args_ok = HostExec::SafePackageName(res.package_name) && HostExec::SafeBuildMethod(res.method) &&
		               HostExec::SafeBuildFlags(res.flags, flag_tokens) && HostExec::SafeUser(user) &&
		               HostExec::SafeHost(node.hostname) && HostExec::SafeRemotePath(r_dir);
		if(!args_ok) {
			res.output = "Error: invalid node address, package, method or flags.";
		} else {
			String remote_cmd = "cd " + r_dir + " && ./bin/build " + res.package_name + " -m " + res.method + " " + Join(flag_tokens, " ") + " 2>&1";
			res.command = remote_cmd;
			Vector<String> ssh_args = HostExec::SshArgs(port, user, node.hostname, 5);
			ssh_args.Add(remote_cmd);
			res.output = HostExec::Run("ssh", ssh_args, false);
		}
		res.exit_code = 0;
		if(res.output.Find("Unable to locate") >= 0 || res.output.Find("error:") >= 0 || res.output.Find("Error:") >= 0 || res.output.Find("Connection timed out") >= 0)
			res.exit_code = 1;
		res.success = (res.exit_code == 0);
	}

	res.elapsed_ms = (int)max((int64)1, msecs() - t0);
	return res;
}

END_UPP_NAMESPACE
