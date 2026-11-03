#include "HostPlatform.h"
#include <sys/stat.h>
#include <sys/statvfs.h>

NAMESPACE_UPP

struct DirEntry : Moveable<DirEntry> {
	String name;
	int64  length = 0;
	Time   last_write_time;
	bool   is_folder = false;
};

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

	String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=3 -p %d %s@%s "
	                    "\"for d in %s/*; do if [ -d \\\"\\$d/.git\\\" ]; then "
	                    "b=\\$(git -C \\\"\\$d\\\" branch --show-current 2>/dev/null || echo master); "
	                    "h=\\$(git -C \\\"\\$d\\\" log -1 --format='%%h' 2>/dev/null || echo '-'); "
	                    "m=\\$(git -C \\\"\\$d\\\" log -1 --format='%%s' 2>/dev/null || echo '-'); "
	                    "u=\\$(find \\\"\\$d\\\" -maxdepth 2 -name '*.upp' 2>/dev/null | wc -l); "
	                    "echo \\\"\\$(basename \\\"\\$d\\\")|\\$b|\\$h|\\$m|\\$u\\\"; "
	                    "fi; done\"",
	                    port, user, node.hostname, remote_dev);

	String out = Sys("sh", {"-c", cmd});
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

	String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=2 -p %d %s@%s "
	                    "\"echo PING_OK && uname -s -r -m && uptime && (ls -1 %s 2>/dev/null | wc -l)\"",
	                    port, user, node.hostname, remote_dev);

	String out = Sys("sh", {"-c", cmd});
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
		String repo_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name);
		if(!DirectoryExists(repo_dir))
			return "Error: Repository '" + repo_name + "' does not exist locally at " + repo_dir;
		String cmd = "git -C \"" + repo_dir + "\" " + git_args + " 2>&1";
		return Sys("sh", {"-c", cmd});
	}

	String user = target->ssh_user.IsEmpty() ? "sblo" : target->ssh_user;
	int port = target->ssh_port > 0 ? target->ssh_port : 22;
	String remote_repo = target->remote_path + "/" + repo_name;

	String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=3 -p %d %s@%s \"git -C %s %s 2>&1\"",
	                    port, user, target->hostname, remote_repo, git_args);
	return Sys("sh", {"-c", cmd});
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

	String cmd = "rsync -avz --exclude='.git' -e 'ssh -o BatchMode=yes -o ConnectTimeout=3' " + src_path + " " + dst_path + " 2>&1";
	String out = Sys("sh", {"-c", cmd});
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
			if(DirectoryExists(AppendFileName(repo_dir, ".git"))) {
				st.branch = TrimBoth(Sys("sh", {"-c", "git -C \"" + repo_dir + "\" branch --show-current 2>/dev/null"}));
				if(st.branch.IsEmpty()) st.branch = "master";

				String log_str = TrimBoth(Sys("sh", {"-c", "git -C \"" + repo_dir + "\" log -1 --format='%h|%s|%cr|%H' 2>/dev/null"}));
				Vector<String> lp = Split(log_str, '|');
				if(lp.GetCount() >= 4) {
					st.commit_hash = lp[0];
					st.commit_msg = lp[1];
					st.commit_time = lp[2];
					local_head_hash = lp[3];
				}
				String status_out = Sys("sh", {"-c", "git -C \"" + repo_dir + "\" status -s 2>/dev/null"});
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

		String ssh_cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=2 -p %d %s@%s "
		                        "\"echo PING && git -C %s branch --show-current 2>/dev/null && "
		                        "git -C %s log -1 --format='%%h|%%s|%%cr|%%H' 2>/dev/null && "
		                        "git -C %s status -s 2>/dev/null | wc -l\"",
		                        port, user, n.hostname, r_dir, r_dir, r_dir);

		String out = Sys("sh", {"-c", ssh_cmd});
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
					String test_behind = Sys("sh", {"-c", "git -C \"" + repo_dir + "\" branch --contains " + remote_full_hash + " 2>/dev/null"});
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

		if(mode == "git_pull" || mode == "sync_all") {
			String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=4 -p %d %s@%s \"git -C %s pull 2>&1\"",
			                    port, user, node->hostname, r_dir);
			String out = Sys("sh", {"-c", cmd});
			log << "[SSH git pull origin]:\n" << out << "\n";
		}
		else if(mode == "git_fetch") {
			String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=4 -p %d %s@%s \"git -C %s fetch origin 2>&1\"",
			                    port, user, node->hostname, r_dir);
			String out = Sys("sh", {"-c", cmd});
			log << "[SSH git fetch origin]:\n" << out << "\n";
		}
		else if(mode == "rsync") {
			String local_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name) + "/";
			String dst_path = Format("%s@%s:%s/", user, node->hostname, r_dir);
			String cmd = "rsync -avz --exclude='.git' -e 'ssh -o BatchMode=yes -o ConnectTimeout=4 -p " + AsString(port) + "' "
			             + local_dir + " " + dst_path + " 2>&1";
			String out = Sys("sh", {"-c", cmd});
			log << "[rsync working tree mirror]:\n" << out << "\n";
		}
		else if(mode == "git_push_from_local") {
			String local_dir = AppendFileName(AppendFileName(GetHomeDirFile(""), "Dev"), repo_name);
			String remote_url = Format("ssh://%s@%s:%d%s/%s", user, node->hostname, port, node->remote_path.StartsWith("~") ? "" : "", r_dir);
			String cmd = Format("git -C \"%s\" push %s HEAD:master 2>&1", local_dir, remote_url);
			String out = Sys("sh", {"-c", cmd});
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
		String cmd = Format("cd \"%s\" && ./bin/build %s -m %s %s 2>&1", upp_dir, res.package_name, res.method, res.flags);
		res.command = cmd;
		res.output = Sys("sh", {"-c", cmd});
		res.exit_code = 0;
		if(res.output.Find("Unable to locate") >= 0 || res.output.Find("error:") >= 0 || res.output.Find("Error:") >= 0)
			res.exit_code = 1;
		res.success = (res.exit_code == 0);
	} else {
		int port = node.ssh_port > 0 ? node.ssh_port : 22;
		String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
		String r_dir = (node.remote_path.IsEmpty() ? "~/Dev" : node.remote_path) + "/ai-upp";

		String remote_cmd = Format("cd %s && ./bin/build %s -m %s %s 2>&1", r_dir, res.package_name, res.method, res.flags);
		String ssh_cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=5 -p %d %s@%s \"%s\"",
		                        port, user, node.hostname, remote_cmd);
		res.command = remote_cmd;
		res.output = Sys("sh", {"-c", ssh_cmd});
		res.exit_code = 0;
		if(res.output.Find("Unable to locate") >= 0 || res.output.Find("error:") >= 0 || res.output.Find("Error:") >= 0 || res.output.Find("Connection timed out") >= 0)
			res.exit_code = 1;
		res.success = (res.exit_code == 0);
	}

	res.elapsed_ms = (int)max((int64)1, msecs() - t0);
	return res;
}

String HostPlatformService::RenderSubNav(const String& active_tab, const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	Vector<DistributedNode> all_nodes = GetConfiguredNodes(false);

	String h;

	// Machine Switcher Top Bar
	h << "<div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:10px; margin-bottom:1rem; padding:8px 14px; background:var(--wf-bg-view,#0d1117); border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
	  << "  <div style=\"display:flex; align-items:center; flex-wrap:wrap; gap:8px; font-size:0.85rem;\">\n"
	  << "    <span style=\"color:var(--wf-fg-inactive,#8b919d); font-weight:700;\">Active Host:</span>\n";

	for(int i = 0; i < all_nodes.GetCount(); i++) {
		const DistributedNode& n = all_nodes[i];
		bool is_cur = (n.id == node.id);
		String target_url;
		if(active_tab == "nodes" || active_tab == "settings")
			target_url = "/host/" + active_tab;
		else if(n.is_local)
			target_url = "/host/" + active_tab;
		else
			target_url = "/node/" + n.id + "/" + active_tab;

		h << "    <a href=\"" << target_url << "\" class=\"btn\" style=\""
		  << (is_cur ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700; border-color:var(--wf-accent-focus,#00a1c5);"
		             : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
		  << " padding:4px 10px; font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(n.is_local ? "monitor" : "server", 14) << " " << n.name
		  << (n.is_local ? " <small style='opacity:0.75;'>(Local)</small>" : "")
		  << "</a>\n";
	}

	h << "  </div>\n"
	  << "  <div style=\"display:flex; gap:6px;\">\n"
	  << "    <a href=\"/host/nodes\" class=\"btn\" style=\"padding:4px 10px; font-size:0.78rem; background:#1e293b; color:#38bdf8;\">"
	  << VividIcon::Get("cpu", 14) << " Mesh Topology</a>\n"
	  << "    <a href=\"/host/settings\" class=\"btn\" style=\"padding:4px 10px; font-size:0.78rem; background:#1e293b; color:#fff;\">"
	  << VividIcon::Get("settings", 14) << " Settings</a>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Second row: Standard SubNav Tabs for the active machine
	struct Tab { const char* id; const char* title; const char* icon; };
	static Tab tabs[] = {
		{ "overview", "Overview",           "server" },
		{ "files",    "Home Files",         "folder" },
		{ "repos",    "Repositories",       "git-branch" },
		{ "fleet",    "Fleet Sync & Build", "refresh-cw" },
		{ "storage",  "Disk Storage",       "database" },
	};

	h << "<div style=\"display:flex; gap:8px; border-bottom:1px solid var(--wf-border-color, rgba(0,161,197,0.25)); padding-bottom:8px; margin-bottom:1.5rem; overflow-x:auto;\">\n";
	for(int i = 0; i < 5; i++) {
		bool is_act = (active_tab == tabs[i].id);
		String url = node.is_local ? ("/host/" + String(tabs[i].id)) : ("/node/" + node.id + "/" + String(tabs[i].id));
		h << "  <a href=\"" << url << "\" class=\"btn\" style=\""
		  << (is_act ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700;"
		             : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
		  << " padding:6px 14px; font-size:0.85rem; display:inline-flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(tabs[i].icon, 15) << " " << tabs[i].title << "</a>\n";
	}

	h << "  <a href=\"/host/nodes\" class=\"btn\" style=\""
	  << (active_tab == "nodes" ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700;"
	                            : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
	  << " padding:6px 14px; font-size:0.85rem; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("cpu", 15) << " Compute Mesh</a>\n";

	h << "  <a href=\"/host/settings\" class=\"btn\" style=\""
	  << (active_tab == "settings" ? "background:var(--wf-accent-focus,#00a1c5); color:#ffffff; font-weight:700;"
	                               : "background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#d3dae3);")
	  << " padding:6px 14px; font-size:0.85rem; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("settings", 15) << " Mesh Settings</a>\n";

	h << "</div>\n";
	return h;
}

String HostPlatformService::RenderOverview(Http& http, const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	if(node.os_info.IsEmpty() || node.status != "online")
		ProbeNode(node);

	UserSession session = SystemAuthService::GetCurrentSession(http);
	Vector<HostRepoInfo> repos = GetRepositoriesForNode(node.id);

	String b;
	b << RenderSubNav("overview", node.id);

	// Status header banner
	b << "<div class=\"card\" style=\"border-left:4px solid " << (node.is_local ? "var(--wf-accent-focus,#00a1c5)" : "#38bdf8") << ";\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"font-size:1.4rem; font-weight:800; color:var(--wf-fg-normal,#fff); display:flex; align-items:center; gap:8px;\">\n"
	  << "        " << VividIcon::Get(node.is_local ? "monitor" : "server", 24) << " Node: <span>" << node.name << "</span> (" << node.hostname << ")\n"
	  << "        <span class=\"badge-v2\" style=\"background:var(--wf-color-positive,#00aa7f);\">" << ToUpper(node.status) << "</span>\n"
	  << "        <span class=\"badge-v2\" style=\"background:#1e283b; color:#38bdf8;\">" << node.latency_ms << " ms</span>\n"
	  << "      </div>\n"
	  << "      <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin-top:4px;\">"
	  << (node.is_local ? "Primary self-hosted workstation with local filesystem, development workspace, and mesh coordination."
	                    : "Remote compute node connected via low-latency SSH with full equal parity file, repository, and storage management.")
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px;\">\n"
	  << "      <a href=\"" << (node.is_local ? "/host/files" : ("/node/" + node.id + "/files")) << "\" class=\"btn btn-primary\">" << VividIcon::Get("folder", 14) << " Explore Files</a>\n"
	  << "      <a href=\"" << (node.is_local ? "/host/repos" : ("/node/" + node.id + "/repos")) << "\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#fff);\">" << VividIcon::Get("git-branch", 14) << " Browse Repos</a>\n"
	  << "      <a href=\"" << (node.is_local ? "/host/storage" : ("/node/" + node.id + "/storage")) << "\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:var(--wf-fg-normal,#fff);\">" << VividIcon::Get("database", 14) << " Disk Storage</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// 4 Stat Cards
	b << "<div class=\"grid-4\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">Development Repos</span>\n"
	  << "    <span class=\"stat-val\">" << repos.GetCount() << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">scanned in " << node.remote_path << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">Protocol &amp; Access</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#38bdf8;\">" << ToUpper(node.protocol) << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">User: " << node.ssh_user << ":" << node.ssh_port << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">System Uptime</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#34d399;\">" << (node.uptime.IsEmpty() ? "Up" : node.uptime) << "</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Kernel: " << (node.os_info.IsEmpty() ? "Linux" : node.os_info) << "</span>\n"
	  << "  </div>\n"
	  << "  <div class=\"stat-card\">\n"
	  << "    <span class=\"stat-lbl\">SSH Latency &amp; Load</span>\n"
	  << "    <span class=\"stat-val\" style=\"font-size:1.35rem; color:#a78bfa;\">" << node.latency_ms << " ms</span>\n"
	  << "    <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Load: " << (node.load_avg.IsEmpty() ? "0.00" : node.load_avg) << "</span>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Details Grid
	b << "<div class=\"grid-2\">\n";

	// System Specs Table
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>System Specifications (" << node.name << ")</span></div>\n"
	  << "    <table><tbody>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d); width:35%;\">Node Identifier</td><td><strong>" << node.id << "</strong></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Hostname / IP</td><td><code>" << node.hostname << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Operating System</td><td>" << (node.os_info.IsEmpty() ? "Linux x86_64" : node.os_info) << "</td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">System Uptime</td><td>" << node.uptime << "</td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Load Average</td><td><code>" << node.load_avg << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">Remote Path</td><td><code>" << node.remote_path << "</code></td></tr>\n"
	  << "      <tr><td style=\"color:var(--wf-fg-inactive,#8b919d);\">User Account</td><td><code>" << node.ssh_user << "</code> (Port " << node.ssh_port << ")</td></tr>\n"
	  << "    </tbody></table>\n"
	  << "  </div>\n";

	// Repositories Preview
	b << "  <div class=\"card\">\n"
	  << "    <div class=\"card-header\"><span>Repositories on " << node.name << "</span><a href=\"" << (node.is_local ? "/host/repos" : ("/node/" + node.id + "/repos")) << "\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">All " << repos.GetCount() << "</a></div>\n"
	  << "    <table><thead><tr><th>Repository</th><th>Branch</th><th>Commit</th></tr></thead><tbody>\n";

	int limit = min(repos.GetCount(), 6);
	for(int i = 0; i < limit; i++) {
		const HostRepoInfo& r = repos[i];
		String file_link = node.is_local ? ("/host/files/Dev/" + r.name) : ("/node/" + node.id + "/files/Dev/" + r.name);
		b << "<tr>"
		  << "<td><strong><a href=\"" << file_link << "\">" << r.name << "</a></strong></td>"
		  << "<td><code style=\"color:var(--wf-accent-focus,#00a1c5);\">" << (r.branch.IsEmpty() ? "-" : r.branch) << "</code></td>"
		  << "<td><code style=\"color:var(--wf-fg-inactive,#8b919d);\">" << (r.last_commit_hash.IsEmpty() ? "-" : r.last_commit_hash) << "</code></td>"
		  << "</tr>\n";
	}
	b << "    </tbody></table>\n  </div>\n";

	b << "</div>\n";
	return b;
}

String HostPlatformService::RenderFiles(Http& http, const String& node_id, const String& rel_path)
{
	if(!SystemAuthService::RequireAuth(http))
		return String();

	DistributedNode node = GetNode(node_id);

	String clean_rel = rel_path;
	while(clean_rel.StartsWith("/")) clean_rel = clean_rel.Mid(1);
	while(clean_rel.EndsWith("/")) clean_rel = clean_rel.Left(clean_rel.GetLength() - 1);

	// Prevent path traversal
	Vector<String> parts = Split(clean_rel, '/');
	Vector<String> safe_parts;
	for(int i = 0; i < parts.GetCount(); i++) {
		String p = parts[i];
		if(p == "." || p.IsEmpty()) continue;
		if(p == "..") {
			if(!safe_parts.IsEmpty()) safe_parts.Drop();
			continue;
		}
		safe_parts.Add(p);
	}

	String safe_rel = Join(safe_parts, "/");
	String node_base_url = node.is_local ? "/host/files" : ("/node/" + node.id + "/files");

	String b;
	b << RenderSubNav("files", node.id);

	// Breadcrumb header
	b << "<div class=\"card\" style=\"margin-bottom:1rem; padding:0.85rem 1.25rem;\">\n"
	  << "  <div style=\"display:flex; align-items:center; gap:8px; font-size:0.95rem; font-weight:600; flex-wrap:wrap;\">\n"
	  << "    <a href=\"" << node_base_url << "\" style=\"display:inline-flex; align-items:center; gap:4px;\">"
	  << VividIcon::Get("home", 16) << " ~ (" << node.name << ")</a>\n";

	String accum;
	for(int i = 0; i < safe_parts.GetCount(); i++) {
		if(!accum.IsEmpty()) accum << "/";
		accum << safe_parts[i];
		b << "    <span style=\"color:var(--wf-fg-inactive,#8b919d);\">/</span>\n";
		if(i == safe_parts.GetCount() - 1)
			b << "    <span style=\"color:var(--wf-accent-focus,#00a1c5);\">" << safe_parts[i] << "</span>\n";
		else
			b << "    <a href=\"" << node_base_url << "/" << accum << "\">" << safe_parts[i] << "</a>\n";
	}
	b << "  </div>\n"
	  << "</div>\n";

	if(node.is_local || node.hostname == "192.168.1.106") {
		// Local filesystem handling
		String home_dir = GetHomeDirFile("");
		String full_path = safe_rel.IsEmpty() ? home_dir : AppendFileName(home_dir, safe_rel);

		if(DirectoryExists(full_path)) {
			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\"><span>Contents: <code>~/" << safe_rel << "</code></span></div>\n"
			  << "  <table><thead><tr><th>Name</th><th>Size</th><th>Permissions</th><th>Last Modified</th></tr></thead><tbody>\n";

			if(!safe_parts.IsEmpty()) {
				String parent_rel;
				for(int i = 0; i < safe_parts.GetCount() - 1; i++) {
					if(i > 0) parent_rel << "/";
					parent_rel << safe_parts[i];
				}
				b << "<tr><td colspan=\"4\"><a href=\"" << node_base_url << "/" << parent_rel << "\" style=\"display:inline-flex; align-items:center; gap:6px; font-weight:700;\">"
				  << VividIcon::Get("chevron-left", 14) << " .. (Parent Directory)</a></td></tr>\n";
			}

			Vector<DirEntry> folders, files;
			FindFile ff(AppendFileName(full_path, "*"));
			while(ff) {
				if(ff.GetName() != "." && ff.GetName() != "..") {
					DirEntry e;
					e.name = ff.GetName();
					e.length = ff.GetLength();
					e.last_write_time = ff.GetLastWriteTime();
					e.is_folder = ff.IsFolder();
					if(e.is_folder) folders.Add(e);
					else files.Add(e);
				}
				ff.Next();
			}
			Sort(folders, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
			Sort(files, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });

			for(int i = 0; i < folders.GetCount(); i++) {
				const DirEntry& f = folders[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px; font-weight:600;\">"
				  << VividIcon::Get("folder", 16) << " " << f.name << "</a></td>"
				  << "<td style=\"color:var(--wf-fg-inactive,#8b919d);\">[dir]</td>"
				  << "<td><code>drwxr-xr-x</code></td><td>" << Format(f.last_write_time) << "</td></tr>\n";
			}
			for(int i = 0; i < files.GetCount(); i++) {
				const DirEntry& f = files[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				int64 sz = f.length;
				String sz_str = sz > 1048576 ? Format("%.1f MB", sz / 1048576.0) : (sz > 1024 ? Format("%.1f KB", sz / 1024.0) : AsString(sz) + " B");
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px;\">"
				  << VividIcon::Get("file", 16) << " " << f.name << "</a></td>"
				  << "<td>" << sz_str << "</td><td><code>-rw-r--r--</code></td><td>" << Format(f.last_write_time) << "</td></tr>\n";
			}
			b << "  </tbody></table>\n</div>\n";
		} else if(FileExists(full_path)) {
			int64 sz = GetFileLength(full_path);
			String sz_str = sz > 1048576 ? Format("%.1f MB", sz / 1048576.0) : (sz > 1024 ? Format("%.1f KB", sz / 1024.0) : AsString(sz) + " B");
			String content = LoadFile(full_path);
			bool is_markdown = full_path.EndsWith(".md") || full_path.EndsWith(".markdown") || full_path.EndsWith(".MD");

			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\">\n"
			  << "    <span>" << VividIcon::Get("file-text", 18) << " <strong>" << GetFileName(full_path) << "</strong> (" << sz_str << ")</span>\n"
			  << "    <div style=\"display:flex; gap:8px; align-items:center;\">\n";

			if(is_markdown) {
				b << "      <div class=\"wf-view-toggle\">\n"
				  << "        <button type=\"button\" id=\"btn-view-preview\" class=\"active\" onclick=\"toggleFileView('preview')\">Preview</button>\n"
				  << "        <button type=\"button\" id=\"btn-view-code\" onclick=\"toggleFileView('code')\">Code</button>\n"
				  << "      </div>\n";
			}

			b << "      <a href=\"" << node_base_url << "/" << (safe_parts.GetCount() > 1 ? safe_rel.Left(safe_rel.ReverseFind('/')) : "") << "\" class=\"btn\" style=\"background:var(--wf-surface,#1e283b); color:#fff; font-size:0.75rem;\">Back</a>\n"
			  << "    </div>\n"
			  << "  </div>\n";

			if(is_markdown) {
				b << "  <div id=\"wf-blob-preview-box\" class=\"markdown-render-box\" style=\"padding:1.5rem; background:var(--wf-bg-view,#151d2b); border-radius:6px;\">\n"
				  << "    <textarea id=\"wf-blob-raw-md\" class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(content) << "</textarea>\n"
				  << "    <div id=\"wf-blob-preview\" class=\"markdown-body\"></div>\n"
				  << "  </div>\n";
			}

			Vector<String> lines = Split(content, '\n', false);
			b << "  <div id=\"wf-blob-code-box\" style=\"" << (is_markdown ? "display:none; " : "") << "background:var(--wf-bg-view,#151d2b); padding:1rem; border-radius:6px; overflow-x:auto; font-family:monospace; font-size:0.85rem; line-height:1.5;\">\n"
			  << "    <table style=\"border-collapse:collapse; width:100%;\">\n";
			for(int i = 0; i < lines.GetCount(); i++) {
				b << "<tr><td style=\"width:45px; text-align:right; padding-right:12px; color:var(--wf-fg-inactive,#8b919d); user-select:none; border:none;\">" << (i + 1) << "</td>"
				  << "<td style=\"white-space:pre-wrap; border:none; color:var(--wf-fg-normal,#d3dae3);\">" << DeHtml(lines[i]) << "</td></tr>\n";
			}
			b << "    </table>\n  </div>\n</div>\n";
		} else {
			b << "<div class=\"card\"><p style=\"color:#ef4444;\">Path not found: " << DeHtml(safe_rel) << "</p></div>\n";
		}
	} else {
		// Remote SSH File & Directory Handling
		String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
		int port = node.ssh_port > 0 ? node.ssh_port : 22;
		String target_path = safe_rel.IsEmpty() ? "~" : ("~/" + safe_rel);

		String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=3 -p %d %s@%s "
		                    "\"if [ -d %s ]; then echo IS_DIR; find %s/ -mindepth 1 -maxdepth 1 -printf '%%y|%%s|%%T@|%%f\\n'; "
		                    "elif [ -f %s ]; then echo IS_FILE; cat %s; else echo NOT_FOUND; fi\"",
		                    port, user, node.hostname, target_path, target_path, target_path, target_path);

		String out = Sys("sh", {"-c", cmd});
		Vector<String> lines = Split(out, '\n', false);

		if(lines.GetCount() > 0 && lines[0] == "IS_DIR") {
			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\"><span>Contents on " << node.name << ": <code>~/" << safe_rel << "</code></span></div>\n"
			  << "  <table><thead><tr><th>Name</th><th>Size</th><th>Permissions</th><th>Type</th></tr></thead><tbody>\n";

			if(!safe_parts.IsEmpty()) {
				String parent_rel;
				for(int i = 0; i < safe_parts.GetCount() - 1; i++) {
					if(i > 0) parent_rel << "/";
					parent_rel << safe_parts[i];
				}
				b << "<tr><td colspan=\"4\"><a href=\"" << node_base_url << "/" << parent_rel << "\" style=\"display:inline-flex; align-items:center; gap:6px; font-weight:700;\">"
				  << VividIcon::Get("chevron-left", 14) << " .. (Parent Directory)</a></td></tr>\n";
			}

			Vector<DirEntry> folders, files;
			for(int i = 1; i < lines.GetCount(); i++) {
				Vector<String> p = Split(lines[i], '|');
				if(p.GetCount() >= 4 && p[3] != "." && p[3] != "..") {
					DirEntry e;
					e.name = p[3];
					e.length = ScanInt64(p[1]);
					e.is_folder = (p[0] == "d" || (p[0] == "l" && p[3].Find(".") < 0));
					if(e.is_folder) folders.Add(e);
					else files.Add(e);
				}
			}
			Sort(folders, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
			Sort(files, [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });

			for(int i = 0; i < folders.GetCount(); i++) {
				const DirEntry& f = folders[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px; font-weight:600;\">"
				  << VividIcon::Get("folder", 16) << " " << f.name << "</a></td>"
				  << "<td style=\"color:var(--wf-fg-inactive,#8b919d);\">[dir]</td>"
				  << "<td><code>drwxr-xr-x</code></td><td>Folder</td></tr>\n";
			}
			for(int i = 0; i < files.GetCount(); i++) {
				const DirEntry& f = files[i];
				String child_rel = safe_rel.IsEmpty() ? f.name : (safe_rel + "/" + f.name);
				int64 sz = f.length;
				String sz_str = sz > 1048576 ? Format("%.1f MB", sz / 1048576.0) : (sz > 1024 ? Format("%.1f KB", sz / 1024.0) : AsString(sz) + " B");
				b << "<tr><td><a href=\"" << node_base_url << "/" << child_rel << "\" style=\"display:inline-flex; align-items:center; gap:8px;\">"
				  << VividIcon::Get("file", 16) << " " << f.name << "</a></td>"
				  << "<td>" << sz_str << "</td><td><code>-rw-r--r--</code></td><td>File</td></tr>\n";
			}
			b << "  </tbody></table>\n</div>\n";
		} else if(lines.GetCount() > 0 && lines[0] == "IS_FILE") {
			bool is_markdown = safe_rel.EndsWith(".md") || safe_rel.EndsWith(".markdown") || safe_rel.EndsWith(".MD");
			String remote_file_content;
			for(int i = 1; i < lines.GetCount(); i++) {
				if(i > 1) remote_file_content << "\n";
				remote_file_content << lines[i];
			}

			b << "<div class=\"card\">\n"
			  << "  <div class=\"card-header\">\n"
			  << "    <span>" << VividIcon::Get("file-text", 18) << " <strong>" << GetFileName(safe_rel) << "</strong> on " << node.name << "</span>\n"
			  << "    <div style=\"display:flex; gap:8px; align-items:center;\">\n";

			if(is_markdown) {
				b << "      <div class=\"wf-view-toggle\">\n"
				  << "        <button type=\"button\" id=\"btn-view-preview\" class=\"active\" onclick=\"toggleFileView('preview')\">Preview</button>\n"
				  << "        <button type=\"button\" id=\"btn-view-code\" onclick=\"toggleFileView('code')\">Code</button>\n"
				  << "      </div>\n";
			}

			b << "      <a href=\"" << node_base_url << "/" << (safe_parts.GetCount() > 1 ? safe_rel.Left(safe_rel.ReverseFind('/')) : "") << "\" class=\"btn\" style=\"background:var(--wf-surface,#1e283b); color:#fff; font-size:0.75rem;\">Back</a>\n"
			  << "    </div>\n"
			  << "  </div>\n";

			if(is_markdown) {
				b << "  <div id=\"wf-blob-preview-box\" class=\"markdown-render-box\" style=\"padding:1.5rem; background:var(--wf-bg-view,#151d2b); border-radius:6px;\">\n"
				  << "    <textarea id=\"wf-blob-raw-md\" class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(remote_file_content) << "</textarea>\n"
				  << "    <div id=\"wf-blob-preview\" class=\"markdown-body\"></div>\n"
				  << "  </div>\n";
			}

			b << "  <div id=\"wf-blob-code-box\" style=\"" << (is_markdown ? "display:none; " : "") << "background:var(--wf-bg-view,#151d2b); padding:1rem; border-radius:6px; overflow-x:auto; font-family:monospace; font-size:0.85rem; line-height:1.5;\">\n"
			  << "    <table style=\"border-collapse:collapse; width:100%;\">\n";
			for(int i = 1; i < lines.GetCount(); i++) {
				b << "<tr><td style=\"width:45px; text-align:right; padding-right:12px; color:var(--wf-fg-inactive,#8b919d); user-select:none; border:none;\">" << i << "</td>"
				  << "<td style=\"white-space:pre-wrap; border:none; color:var(--wf-fg-normal,#d3dae3);\">" << DeHtml(lines[i]) << "</td></tr>\n";
			}
			b << "    </table>\n  </div>\n</div>\n";
		} else {
			b << "<div class=\"card\"><p style=\"color:#ef4444;\">Path not found on remote node: " << DeHtml(safe_rel) << "</p></div>\n";
		}
	}

	return b;
}

String HostPlatformService::RenderRepos(Http& http, const String& node_id)
{
	DistributedNode node = GetNode(node_id);
	Vector<HostRepoInfo> repos = GetRepositoriesForNode(node.id);

	String b;
	b << RenderSubNav("repos", node.id);

	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>Repositories on " << node.name << " (" << node.hostname << ")</span>\n"
	  << "    <span class=\"badge-v2\">" << repos.GetCount() << " detected in " << node.remote_path << "</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem; margin-bottom:1rem;\">"
	  << "    Git repositories residing on " << node.name << ". Equal parity repository browser: jump into files, check branches, inspect commits, or execute remote SSH git."
	  << "  </p>\n"
	  << "  <table><thead><tr><th>Repository Name</th><th>Git Branch</th><th>Commit Hash &amp; Message</th><th>Packages</th><th>Actions</th></tr></thead><tbody>\n";

	for(int i = 0; i < repos.GetCount(); i++) {
		const HostRepoInfo& r = repos[i];
		String files_url = node.is_local ? ("/host/files/Dev/" + r.name) : ("/node/" + node.id + "/files/Dev/" + r.name);
		b << "<tr id=\"repo-" << r.name << "\">"
		  << "<td><strong><a href=\"" << files_url << "\">" << r.name << "</a></strong></td>"
		  << "<td>"
		  << (r.has_git ? ("<span style=\"display:inline-flex; align-items:center; gap:4px; font-weight:600; color:var(--wf-accent-focus,#00a1c5);\">" + VividIcon::Get("git-branch", 14) + " " + (r.branch.IsEmpty() ? "master" : r.branch) + "</span>") : "<span style=\"color:#64748b;\">no git</span>")
		  << "</td>"
		  << "<td><code style=\"color:var(--wf-fg-inactive,#8b919d);\">" << (r.last_commit_hash.IsEmpty() ? "-" : r.last_commit_hash) << "</code> "
		  << "<small style=\"color:var(--wf-fg-normal,#d3dae3);\">" << DeHtml(r.last_commit_msg) << "</small></td>"
		  << "<td>"
		  << (r.upp_packages.IsEmpty() ? "<span style=\"color:#64748b;\">-</span>" : ("<span class=\"badge-v2\" style=\"background:#6d55aa;\">" + AsString(r.upp_packages.GetCount()) + " upp</span>"))
		  << "</td>"
		  << "<td>"
		  << "  <div style=\"display:inline-flex; gap:6px;\">"
		  << "    <a href=\"" << files_url << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Files</a>"
		  << "    <a href=\"/repo/" << (r.owner.IsEmpty() ? "SeppoPakonen" : r.owner) << "/" << r.name << "\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:#24292f; color:#ffffff;\" title=\"GitHub\">GitHub</a>"
		  << "  </div>"
		  << "</td>"
		  << "</tr>\n";
	}

	b << "  </tbody></table>\n</div>\n";
	return b;
}

String HostPlatformService::RenderNodes(Http& http)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);

	String b;
	b << RenderSubNav("nodes", "local-workstation");

	b << "<div class=\"card\" style=\"border-left:4px solid var(--wf-accent-focus,#00a1c5); margin-bottom:1.5rem;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <h2 style=\"margin:0; font-size:1.35rem; display:flex; align-items:center; gap:8px;\">"
	  << VividIcon::Get("cpu", 22) << " Self-Hosted Compute Mesh Topology</h2>\n"
	  << "      <p style=\"margin:4px 0 0 0; color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem;\">"
	  << "        High-performance SSH &amp; rsync mesh linking workstation clusters for rapid multi-machine git coordination and sync."
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px;\">\n"
	  << "      <a href=\"/host/settings\" class=\"btn btn-primary\">" << VividIcon::Get("settings", 14) << " Add / Manage Nodes</a>\n"
	  << "      <button onclick=\"probeAllNodes()\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:#fff;\">" << VividIcon::Get("refresh-cw", 14) << " Probe All Nodes</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Mesh Nodes Grid Cards
	b << "<div style=\"display:grid; grid-template-columns:repeat(auto-fill, minmax(340px, 1fr)); gap:1rem; margin-bottom:1.5rem;\">\n";
	for(int i = 0; i < nodes.GetCount(); i++) {
		const DistributedNode& n = nodes[i];
		bool online = (n.status == "online");
		String status_color = online ? "var(--wf-color-positive,#00aa7f)" : "#ef4444";
		String node_overview_url = n.is_local ? "/host/overview" : ("/node/" + n.id + "/overview");

		b << "<div class=\"card\" id=\"node-card-" << n.id << "\" style=\"border-top:3px solid " << (online ? "var(--wf-accent-focus,#00a1c5)" : "#ef4444") << "; margin-bottom:0;\">\n"
		  << "  <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:0.75rem;\">\n"
		  << "    <div>\n"
		  << "      <div style=\"font-weight:700; font-size:1.05rem; display:flex; align-items:center; gap:6px;\">"
		  << VividIcon::Get(n.is_local ? "monitor" : "server", 18) << " <a href=\"" << node_overview_url << "\" style=\"text-decoration:none;\">" << n.name << "</a></div>\n"
		  << "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << n.hostname << "</code> (" << n.id << ")</div>\n"
		  << "    </div>\n"
		  << "    <div style=\"display:flex; flex-direction:column; align-items:flex-end; gap:4px;\">\n"
		  << "      <span class=\"badge-v2\" style=\"background:" << status_color << "; font-size:0.75rem;\">" << ToUpper(n.status) << "</span>\n"
		  << "      <span class=\"badge-v2\" style=\"background:#1e283b; color:#38bdf8; font-size:0.7rem; border:1px solid rgba(56,189,248,0.2);\">" << n.latency_ms << " ms</span>\n"
		  << "    </div>\n"
		  << "  </div>\n"
		  << "  <div style=\"font-size:0.85rem; line-height:1.6; color:var(--wf-fg-normal,#d3dae3); margin-bottom:0.75rem;\">\n"
		  << "    <div><strong>Protocol:</strong> <span class=\"badge-v2\" style=\"background:#24292f; color:#a78bfa; font-size:0.7rem;\">" << ToUpper(n.protocol) << "</span> "
		  << "      (" << n.ssh_user << ":" << n.ssh_port << ")</div>\n"
		  << "    <div><strong>Path:</strong> <code>" << n.remote_path << "</code></div>\n"
		  << "    <div><strong>Kernel:</strong> " << (n.os_info.IsEmpty() ? "Linux x86_64" : n.os_info) << "</div>\n"
		  << "    <div><strong>Uptime:</strong> " << (n.uptime.IsEmpty() ? "Up" : n.uptime) << "</div>\n"
		  << "    <div><strong>Load Avg:</strong> " << (n.load_avg.IsEmpty() ? "0.00" : n.load_avg) << "</div>\n"
		  << "    <div><strong>Repositories:</strong> " << n.repo_count << " scanned</div>\n"
		  << "  </div>\n"
		  << "  <div style=\"display:flex; gap:6px; border-top:1px solid var(--wf-border-color, rgba(255,255,255,0.08)); padding-top:0.6rem;\">\n"
		  << "    <a href=\"" << node_overview_url << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open Node</a>\n"
		  << "    <button onclick=\"probeSingleNode('" << n.id << "')\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Probe</button>\n"
		  << "    <button onclick=\"selectRemoteGitNode('" << n.id << "')\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Run Git</button>\n"
		  << "  </div>\n"
		  << "</div>\n";
	}
	b << "</div>\n";

	// Fast Remote Git Box
	b << "<div class=\"card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("git-branch", 18) << " Fast Remote Git Command Runner (Low-Latency SSH)</span>\n"
	  << "    <span class=\"badge-v2\" style=\"background:#38bdf8; color:#0f172a;\">~50ms Direct SSH</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1rem;\">"
	  << "    Executes git operations directly on remote mesh node RAM/CPU over persistent SSH keys. Sub-second response without filesystem mount overhead."
	  << "  </p>\n"
	  << "  <div style=\"display:grid; grid-template-columns: 1fr 1fr 2fr auto; gap:10px; align-items:end; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Mesh Node</label>\n"
	  << "      <select id=\"git-node-select\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";

	for(int i = 0; i < nodes.GetCount(); i++) {
		b << "        <option value=\"" << nodes[i].id << "\">" << nodes[i].name << " (" << nodes[i].hostname << ")</option>\n";
	}

	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Repository</label>\n"
	  << "      <input type=\"text\" id=\"git-repo-input\" value=\"ai-upp\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Git Command / Arguments</label>\n"
	  << "      <div style=\"display:flex; gap:6px;\">\n"
	  << "        <input type=\"text\" id=\"git-args-input\" value=\"status -s\" class=\"form-control\" style=\"flex:1; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "        <button onclick=\"setGitCmd('status -s')\" class=\"btn\" style=\"padding:4px 8px; font-size:0.75rem; background:#24292f; color:#fff;\">status</button>\n"
	  << "        <button onclick=\"setGitCmd('log -5 --oneline')\" class=\"btn\" style=\"padding:4px 8px; font-size:0.75rem; background:#24292f; color:#fff;\">log</button>\n"
	  << "        <button onclick=\"setGitCmd('branch -a')\" class=\"btn\" style=\"padding:4px 8px; font-size:0.75rem; background:#24292f; color:#fff;\">branch</button>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-exec-git\" onclick=\"runRemoteGit()\" class=\"btn btn-primary\" style=\"padding:8px 16px; font-weight:700;\">Execute SSH Git</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <pre id=\"git-terminal-output\" style=\"background:#0b0f19; color:#38bdf8; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:80px; max-height:360px; overflow-y:auto; border:1px solid #1e293b;\">Ready for remote execution. Select node and press 'Execute SSH Git'.</pre>\n"
	  << "</div>\n";

	// Mesh Synchronization Section
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("refresh-cw", 18) << " Peer-to-Peer Mesh Sync Runner</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1rem;\">"
	  << "    Sync source repository workspace between two nodes across the mesh over SSH rsync."
	  << "  </p>\n"
	  << "  <div style=\"display:grid; grid-template-columns: 1fr 1fr 1fr auto; gap:10px; align-items:end; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Source Node</label>\n"
	  << "      <select id=\"sync-src-select\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";
	for(int i = 0; i < nodes.GetCount(); i++)
		b << "        <option value=\"" << nodes[i].id << "\"" << (nodes[i].is_local ? " selected" : "") << ">" << nodes[i].name << " (" << nodes[i].hostname << ")</option>\n";
	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Node</label>\n"
	  << "      <select id=\"sync-dst-select\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";
	for(int i = 0; i < nodes.GetCount(); i++)
		b << "        <option value=\"" << nodes[i].id << "\"" << (!nodes[i].is_local && i == 1 ? " selected" : "") << ">" << nodes[i].name << " (" << nodes[i].hostname << ")</option>\n";
	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Repository</label>\n"
	  << "      <input type=\"text\" id=\"sync-repo-input\" value=\"ai-upp\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button onclick=\"runMeshSync()\" class=\"btn btn-primary\" style=\"padding:8px 16px; font-weight:700;\">Trigger Sync</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <pre id=\"sync-terminal-output\" style=\"background:#0b0f19; color:#34d399; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:60px; max-height:300px; overflow-y:auto; border:1px solid #1e293b;\">No active sync running.</pre>\n"
	  << "</div>\n";

	// JavaScript controllers
	b << "<script>\n"
	  << "function setGitCmd(cmd) { document.getElementById('git-args-input').value = cmd; }\n"
	  << "function selectRemoteGitNode(id) { document.getElementById('git-node-select').value = id; document.getElementById('git-args-input').focus(); }\n"
	  << "async function runRemoteGit() {\n"
	  << "  const out = document.getElementById('git-terminal-output');\n"
	  << "  const btn = document.getElementById('btn-exec-git');\n"
	  << "  const node = document.getElementById('git-node-select').value;\n"
	  << "  const repo = document.getElementById('git-repo-input').value;\n"
	  << "  const args = document.getElementById('git-args-input').value;\n"
	  << "  out.textContent = 'Running: git -C ' + repo + ' ' + args + ' on ' + node + '...';\n"
	  << "  btn.disabled = true;\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/remotegit', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: {'Content-Type': 'application/json'},\n"
	  << "      body: JSON.stringify({ node_id: node, repo: repo, args: args })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    out.textContent = data.output || '(No output returned)';\n"
	  << "  } catch(e) {\n"
	  << "    out.textContent = 'Error: ' + e;\n"
	  << "  } finally { btn.disabled = false; }\n"
	  << "}\n"
	  << "async function probeSingleNode(id) {\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/nodes/probe/' + encodeURIComponent(id));\n"
	  << "    const data = await resp.json();\n"
	  << "    alert('Node ' + id + ' probe: status=' + data.status + ', latency=' + data.latency_ms + 'ms, repos=' + data.repo_count);\n"
	  << "    location.reload();\n"
	  << "  } catch(e) { alert('Probe error: ' + e); }\n"
	  << "}\n"
	  << "async function probeAllNodes() {\n"
	  << "  try {\n"
	  << "    await fetch('/api/host/nodes?probe=1');\n"
	  << "    location.reload();\n"
	  << "  } catch(e) { alert('Probe error: ' + e); }\n"
	  << "}\n"
	  << "async function runMeshSync() {\n"
	  << "  const out = document.getElementById('sync-terminal-output');\n"
	  << "  const src = document.getElementById('sync-src-select').value;\n"
	  << "  const dst = document.getElementById('sync-dst-select').value;\n"
	  << "  const repo = document.getElementById('sync-repo-input').value;\n"
	  << "  out.textContent = 'Triggering sync: ' + src + ' -> ' + dst + ' (' + repo + ')...';\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/sync', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: {'Content-Type': 'application/json'},\n"
	  << "      body: JSON.stringify({ source: src, target: dst, repo: repo })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    out.textContent = data.output || 'Sync finished with no output.';\n"
	  << "  } catch(e) { out.textContent = 'Sync error: ' + e; }\n"
	  << "}\n"
	  << "</script>\n";

	return b;
}

String HostPlatformService::RenderFleet(Http& http, const String& node_id)
{
	DistributedNode active_node = GetNode(node_id);
	Vector<FleetGitNodeStatus> fleet = GetFleetGitStatus("ai-upp");
	Vector<DistributedNode> all_nodes = GetConfiguredNodes(false);

	String b;
	b << RenderSubNav("fleet", active_node.id);

	// Fleet Operations Header Card
	b << "<div class=\"card\" style=\"border-left:4px solid var(--wf-accent-focus,#00a1c5); margin-bottom:1.5rem;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div>\n"
	  << "      <h2 style=\"margin:0; font-size:1.35rem; display:flex; align-items:center; gap:8px;\">"
	  << VividIcon::Get("refresh-cw", 22) << " Cluster Fleet Git Synchronization &amp; Remote Compilation</h2>\n"
	  << "      <p style=\"margin:4px 0 0 0; color:var(--wf-fg-inactive,#8b919d); font-size:0.9rem;\">"
	  << "        High-speed multi-node Git synchronization and remote <code>./bin/build</code> dispatcher across workstation and cluster fleet over low-latency SSH."
	  << "      </p>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:8px;\">\n"
	  << "      <button id=\"btn-fleet-sync-all\" onclick=\"runFleetSync('git_pull', 'all')\" class=\"btn btn-primary\" style=\"display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("refresh-cw", 14) << " 1-Click Sync All Fleet</button>\n"
	  << "      <button id=\"btn-fleet-fetch-all\" onclick=\"runFleetSync('git_fetch', 'all')\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:#fff; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("download", 14) << " SSH Fetch All</button>\n"
	  << "      <button id=\"btn-fleet-refresh-status\" onclick=\"refreshFleetStatus()\" class=\"btn\" style=\"background:var(--wf-bg-alt,#1c202f); color:#38bdf8; display:inline-flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("activity", 14) << " Refresh Status</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Card 1: Multi-Node Git Status Matrix
	b << "<div class=\"card\" id=\"fleet-git-matrix\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("git-branch", 18) << " Fleet Git Synchronization Matrix (ai-upp)</span>\n"
	  << "    <span class=\"badge-v2\" style=\"background:#38bdf8; color:#0f172a;\">3 Fleet Nodes Online</span>\n"
	  << "  </div>\n"
	  << "  <table>\n"
	  << "    <thead>\n"
	  << "      <tr>\n"
	  << "        <th>Node / Hostname</th>\n"
	  << "        <th>Active Branch</th>\n"
	  << "        <th>Latest Commit</th>\n"
	  << "        <th>Working Tree</th>\n"
	  << "        <th>Fleet Sync State</th>\n"
	  << "        <th style=\"text-align:right;\">Node Actions</th>\n"
	  << "      </tr>\n"
	  << "    </thead>\n"
	  << "    <tbody>\n";

	for(int i = 0; i < fleet.GetCount(); i++) {
		const FleetGitNodeStatus& s = fleet[i];
		String badge_color = "#10b981"; // green
		String badge_text = "SYNCED";

		if(s.sync_state == "behind") {
			badge_color = "#f59e0b"; // amber
			badge_text = "BEHIND";
		} else if(s.sync_state == "ahead") {
			badge_color = "#38bdf8"; // blue
			badge_text = "AHEAD";
		} else if(s.sync_state == "diverged") {
			badge_color = "#ec4899"; // pink
			badge_text = "DIVERGED";
		} else if(s.sync_state == "unreachable") {
			badge_color = "#ef4444"; // red
			badge_text = "OFFLINE";
		}

		b << "      <tr id=\"fleet-node-row-" << s.node_id << "\">\n"
		  << "        <td>\n"
		  << "          <div style=\"font-weight:700; color:var(--wf-fg-normal,#fff);\">" << s.name << "</div>\n"
		  << "          <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << s.hostname << "</code> &bull; " << s.latency_ms << " ms</div>\n"
		  << "        </td>\n"
		  << "        <td><code style=\"color:var(--wf-accent-focus,#00a1c5);\">" << s.branch << "</code></td>\n"
		  << "        <td>\n"
		  << "          <div><code style=\"color:#38bdf8;\">" << s.commit_hash << "</code> <span style=\"font-size:0.85rem;\">" << s.commit_msg << "</span></div>\n"
		  << "          <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">" << s.commit_time << "</div>\n"
		  << "        </td>\n"
		  << "        <td>\n";

		if(s.is_dirty) {
			b << "          <span class=\"badge-v2\" style=\"background:#f59e0b; color:#000;\">" << s.dirty_count << " modified</span>\n";
		} else {
			b << "          <span class=\"badge-v2\" style=\"background:#10b981;\">Clean</span>\n";
		}

		b << "        </td>\n"
		  << "        <td><span class=\"badge-v2\" style=\"background:" << badge_color << "; font-weight:700;\">" << badge_text << "</span></td>\n"
		  << "        <td style=\"text-align:right;\">\n";

		if(s.node_id == "local-workstation") {
			b << "          <span style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8b919d);\">Primary Master</span>\n";
		} else {
			b << "          <button id=\"btn-fleet-sync-" << s.node_id << "\" onclick=\"runFleetSync('git_pull', '" << s.node_id << "')\" class=\"btn btn-primary\" style=\"font-size:0.75rem; padding:3px 8px;\">Pull</button>\n"
			  << "          <button id=\"btn-fleet-fetch-" << s.node_id << "\" onclick=\"runFleetSync('git_fetch', '" << s.node_id << "')\" class=\"btn\" style=\"font-size:0.75rem; padding:3px 8px; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Fetch</button>\n"
			  << "          <button id=\"btn-fleet-rsync-" << s.node_id << "\" onclick=\"runFleetSync('rsync', '" << s.node_id << "')\" class=\"btn\" style=\"font-size:0.75rem; padding:3px 8px; background:var(--wf-bg-alt,#1c202f); color:#38bdf8;\">Rsync</button>\n";
		}

		b << "        </td>\n"
		  << "      </tr>\n";
	}

	b << "    </tbody>\n"
	  << "  </table>\n"
	  << "  <div style=\"margin-top:1rem;\">\n"
	  << "    <div style=\"font-size:0.85rem; font-weight:700; color:var(--wf-fg-normal,#fff); margin-bottom:6px; display:flex; align-items:center; gap:6px;\">"
	  << VividIcon::Get("terminal", 14) << " Fleet Sync Stream Console</div>\n"
	  << "    <pre id=\"fleet-sync-terminal\" style=\"background:#0b0f19; color:#34d399; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:80px; max-height:260px; overflow-y:auto; border:1px solid #1e293b;\">Ready for fleet operations. Click '1-Click Sync All Fleet' or individual node actions above.</pre>\n"
	  << "  </div>\n"
	  << "</div>\n";

	// Card 2: Remote Compilation & Build Dispatcher
	b << "<div class=\"card\" id=\"fleet-build-card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("cpu", 18) << " Remote Build &amp; Test Dispatcher</span>\n"
	  << "    <span class=\"badge-v2\" style=\"background:#10b981;\">Direct SSH umk / build</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1rem;\">"
	  << "    Dispatch compilation commands (<code>./bin/build &lt;package&gt;</code>) to any remote cluster machine over persistent SSH. View real-time terminal output and build results."
	  << "  </p>\n"
	  << "  <div style=\"display:grid; grid-template-columns: 1.5fr 2fr 1fr 1fr auto auto; gap:10px; align-items:end; margin-bottom:1rem;\">\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Cluster Node</label>\n"
	  << "      <select id=\"fleet-build-node\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n";

	for(int i = 0; i < all_nodes.GetCount(); i++) {
		b << "        <option value=\"" << all_nodes[i].id << "\"" << (all_nodes[i].id == active_node.id ? " selected" : "") << ">"
		  << all_nodes[i].name << " (" << all_nodes[i].hostname << ")</option>\n";
	}

	b << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Target Package</label>\n"
	  << "      <input type=\"text\" id=\"fleet-build-pkg\" value=\"reference/ManagerSkylark\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Compiler Method</label>\n"
	  << "      <select id=\"fleet-build-method\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "        <option value=\"CLANG\" selected>CLANG</option>\n"
	  << "        <option value=\"GCC\">GCC</option>\n"
	  << "        <option value=\"MSVS\">MSVS</option>\n"
	  << "      </select>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Build Flags</label>\n"
	  << "      <input type=\"text\" id=\"fleet-build-flags\" value=\"-r\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-fleet-build-run\" onclick=\"dispatchFleetBuild(false)\" class=\"btn btn-primary\" style=\"padding:8px 14px; font-weight:700;\">Dispatch Build</button>\n"
	  << "    </div>\n"
	  << "    <div>\n"
	  << "      <button id=\"btn-fleet-build-clean\" onclick=\"dispatchFleetBuild(true)\" class=\"btn\" style=\"padding:8px 14px; font-weight:700; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Clean &amp; Build</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <div id=\"fleet-build-status-bar\" style=\"display:none; margin-bottom:0.75rem; font-size:0.85rem; font-weight:600; padding:6px 12px; border-radius:4px;\"></div>\n"
	  << "  <pre id=\"fleet-build-terminal\" style=\"background:#0b0f19; color:#38bdf8; padding:1rem; border-radius:6px; font-family:monospace; font-size:0.85rem; min-height:100px; max-height:400px; overflow-y:auto; border:1px solid #1e293b;\">Build dispatcher ready. Select target cluster node and click 'Dispatch Build'.</pre>\n"
	  << "</div>\n";

	// Injected JavaScript
	b << "<script>\n"
	  << "async function runFleetSync(mode, target_node) {\n"
	  << "  const term = document.getElementById('fleet-sync-terminal');\n"
	  << "  term.textContent = 'Executing fleet sync (mode: ' + mode + ', target: ' + target_node + ')...';\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/fleet/sync', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: { 'Content-Type': 'application/json' },\n"
	  << "      body: JSON.stringify({ mode: mode, target_node: target_node, repo: 'ai-upp' })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    term.textContent = data.output || 'Fleet sync completed with no output.';\n"
	  << "  } catch(e) { term.textContent = 'Fleet sync failed: ' + e; }\n"
	  << "}\n"
	  << "async function dispatchFleetBuild(is_clean) {\n"
	  << "  const term = document.getElementById('fleet-build-terminal');\n"
	  << "  const sbar = document.getElementById('fleet-build-status-bar');\n"
	  << "  const btnRun = document.getElementById('btn-fleet-build-run');\n"
	  << "  const node = document.getElementById('fleet-build-node').value;\n"
	  << "  const pkg = document.getElementById('fleet-build-pkg').value;\n"
	  << "  const method = document.getElementById('fleet-build-method').value;\n"
	  << "  let flags = document.getElementById('fleet-build-flags').value;\n"
	  << "  if (is_clean && !flags.includes('-c')) flags = '-c ' + flags;\n"
	  << "  term.textContent = 'Dispatching build to node [' + node + ']: ./bin/build ' + pkg + ' -m ' + method + ' ' + flags + '...';\n"
	  << "  sbar.style.display = 'block';\n"
	  << "  sbar.style.background = '#1e293b';\n"
	  << "  sbar.style.color = '#38bdf8';\n"
	  << "  sbar.textContent = 'Building on ' + node + '...';\n"
	  << "  btnRun.disabled = true;\n"
	  << "  try {\n"
	  << "    const resp = await fetch('/api/host/fleet/build', {\n"
	  << "      method: 'POST',\n"
	  << "      headers: { 'Content-Type': 'application/json' },\n"
	  << "      body: JSON.stringify({ node_id: node, package: pkg, method: method, flags: flags })\n"
	  << "    });\n"
	  << "    const data = await resp.json();\n"
	  << "    term.textContent = data.output || '(No build output returned)';\n"
	  << "    if (data.success) {\n"
	  << "      sbar.style.background = '#064e3b';\n"
	  << "      sbar.style.color = '#34d399';\n"
	  << "      sbar.textContent = 'Build SUCCESS on ' + node + ' in ' + data.elapsed_ms + 'ms (exit code 0)';\n"
	  << "    } else {\n"
	  << "      sbar.style.background = '#7f1d1d';\n"
	  << "      sbar.style.color = '#fca5a5';\n"
	  << "      sbar.textContent = 'Build FAILED on ' + node + ' in ' + data.elapsed_ms + 'ms (exit code ' + data.exit_code + ')';\n"
	  << "    }\n"
	  << "  } catch(e) {\n"
	  << "    term.textContent = 'Build dispatch error: ' + e;\n"
	  << "    sbar.style.background = '#7f1d1d';\n"
	  << "    sbar.style.color = '#fca5a5';\n"
	  << "    sbar.textContent = 'Network or dispatch error: ' + e;\n"
	  << "  } finally { btnRun.disabled = false; }\n"
	  << "}\n"
	  << "function refreshFleetStatus() {\n"
	  << "  location.reload();\n"
	  << "}\n"
	  << "</script>\n";

	return b;
}

String HostPlatformService::RenderSettings(Http& http)
{
	Vector<DistributedNode> nodes = GetConfiguredNodes(false);

	String b;
	b << RenderSubNav("settings", "local-workstation");

	// Add Node Form
	b << "<div class=\"card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("plus-circle", 18) << " Add Computer or Server to Mesh</span>\n"
	  << "  </div>\n"
	  << "  <p style=\"color:var(--wf-fg-inactive,#8b919d); font-size:0.85rem; margin-bottom:1.25rem;\">"
	  << "    Enter connection credentials for any network host accessible via SSH keys. The node will be probed and added to the cluster mesh."
	  << "  </p>\n"
	  << "  <form method=\"POST\" action=\"/host/settings/add\">\n"
	  << "    <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(220px, 1fr)); gap:12px; margin-bottom:1.25rem;\">\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Node Identifier (Slug)</label>\n"
	  << "        <input type=\"text\" name=\"id\" placeholder=\"e.g. worker-node-2\" required class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Display Name</label>\n"
	  << "        <input type=\"text\" name=\"name\" placeholder=\"e.g. Office Ryzen Workstation\" required class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Hostname / IP Address</label>\n"
	  << "        <input type=\"text\" name=\"hostname\" placeholder=\"e.g. 192.168.1.108\" required class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">SSH Username</label>\n"
	  << "        <input type=\"text\" name=\"ssh_user\" value=\"sblo\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">SSH Port</label>\n"
	  << "        <input type=\"number\" name=\"ssh_port\" value=\"22\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Remote Dev Workspace Path</label>\n"
	  << "        <input type=\"text\" name=\"remote_path\" value=\"~/Dev\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "      </div>\n"
	  << "      <div>\n"
	  << "        <label style=\"display:block; font-size:0.8rem; font-weight:600; margin-bottom:4px;\">Protocol</label>\n"
	  << "        <select name=\"protocol\" class=\"form-control\" style=\"width:100%; padding:6px 10px; background:var(--wf-bg-alt,#1c202f); color:#fff; border:1px solid var(--wf-border-color,#334155); border-radius:4px;\">\n"
	  << "          <option value=\"ssh\">SSH (Fast Remote Execution)</option>\n"
	  << "          <option value=\"rsync\">Rsync (Storage Mirroring)</option>\n"
	  << "          <option value=\"local\">Local Workstation</option>\n"
	  << "        </select>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <button type=\"submit\" class=\"btn btn-primary\" style=\"padding:8px 20px; font-weight:700;\">"
	  << VividIcon::Get("check", 16) << " Probe &amp; Register Node</button>\n"
	  << "  </form>\n"
	  << "</div>\n";

	// Node Management Table
	b << "<div class=\"card\" style=\"margin-bottom:1.5rem;\">\n"
	  << "  <div class=\"card-header\">\n"
	  << "    <span>" << VividIcon::Get("list", 18) << " Registered Mesh Cluster Nodes (" << nodes.GetCount() << ")</span>\n"
	  << "  </div>\n"
	  << "  <table><thead><tr><th>Node Name &amp; ID</th><th>Host / IP</th><th>SSH Creds</th><th>Remote Workspace</th><th>Status</th><th>Latency</th><th>Actions</th></tr></thead><tbody>\n";

	for(int i = 0; i < nodes.GetCount(); i++) {
		const DistributedNode& n = nodes[i];
		bool online = (n.status == "online");
		String node_url = n.is_local ? "/host/overview" : ("/node/" + n.id + "/overview");
		b << "<tr>"
		  << "<td><strong><a href=\"" << node_url << "\">" << n.name << "</a></strong><br><small style=\"color:#64748b;\">" << n.id << "</small></td>"
		  << "<td><code>" << n.hostname << "</code></td>"
		  << "<td><code>" << n.ssh_user << ":" << n.ssh_port << "</code></td>"
		  << "<td><code>" << n.remote_path << "</code></td>"
		  << "<td><span class=\"badge-v2\" style=\"background:" << (online ? "var(--wf-color-positive,#00aa7f)" : "#ef4444") << ";\">" << ToUpper(n.status) << "</span></td>"
		  << "<td>" << n.latency_ms << " ms</td>"
		  << "<td>\n"
		  << "  <div style=\"display:inline-flex; gap:6px;\">\n"
		  << "    <a href=\"" << node_url << "\" class=\"btn btn-primary\" style=\"padding:3px 8px; font-size:0.75rem;\">Open</a>\n"
		  << "    <a href=\"/api/host/nodes/probe/" << n.id << "\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:var(--wf-bg-alt,#1c202f); color:#fff;\">Probe</a>\n";

		if(!n.is_local) {
			b << "    <form method=\"POST\" action=\"/host/settings/remove\" style=\"margin:0; display:inline;\">\n"
			  << "      <input type=\"hidden\" name=\"id\" value=\"" << n.id << "\">\n"
			  << "      <button type=\"submit\" class=\"btn\" style=\"padding:3px 8px; font-size:0.75rem; background:#7f1d1d; color:#fca5a5;\" onclick=\"return confirm('Remove node " << n.name << "?');\">Delete</button>\n"
			  << "    </form>\n";
		}
		b << "  </div>\n"
		  << "</td>"
		  << "</tr>\n";
	}

	b << "  </tbody></table>\n</div>\n";

	// Restore Defaults Banner
	b << "<div class=\"card\" style=\"border-left:4px solid #a78bfa;\">\n"
	  << "  <div style=\"display:flex; justify-content:space-between; align-items:center;\">\n"
	  << "    <div>\n"
	  << "      <div style=\"font-weight:700;\">Reset Default Mesh Fleet</div>\n"
	  << "      <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); margin:4px 0 0 0;\">Restore default verified triple nodes: 192.168.1.106 (Workstation), 192.168.1.105 (MediaCenter), 192.168.1.169 (Gen7).</p>\n"
	  << "    </div>\n"
	  << "    <form method=\"POST\" action=\"/host/settings/reset\" style=\"margin:0;\">\n"
	  << "      <button type=\"submit\" class=\"btn\" style=\"background:#2e1065; color:#c4b5fd; font-weight:600; padding:6px 14px;\">Restore Verified Fleet</button>\n"
	  << "    </form>\n"
	  << "  </div>\n"
	  << "</div>\n";

	return b;
}

String HostPlatformService::RenderStorage(Http& http, const String& node_id)
{
	DistributedNode node = GetNode(node_id);

	String b;
	b << RenderSubNav("storage", node.id);

	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Storage Mounts &amp; Disk Usage (" << node.name << ")</span></div>\n"
	  << "  <table><thead><tr><th>Mount Point</th><th>Total Size</th><th>Used</th><th>Free Space</th><th>Usage Bar</th></tr></thead><tbody>\n";

	if(node.is_local || node.hostname == "192.168.1.106") {
		const char* check_mounts[] = { "/", "/home", "/yhteinen", "/tmp" };
		for(int i = 0; i < 4; i++) {
			struct statvfs sv;
			if(statvfs(check_mounts[i], &sv) == 0 && sv.f_blocks > 0) {
				double total_gb = (sv.f_blocks * (double)sv.f_frsize) / (1024.0 * 1024 * 1024);
				double free_gb = (sv.f_bavail * (double)sv.f_frsize) / (1024.0 * 1024 * 1024);
				double used_gb = total_gb - free_gb;
				double pct = (total_gb > 0) ? (used_gb / total_gb * 100.0) : 0.0;

				b << "<tr>"
				  << "<td><code>" << check_mounts[i] << "</code></td>"
				  << "<td>" << Format("%.1f GB", total_gb) << "</td>"
				  << "<td>" << Format("%.1f GB", used_gb) << "</td>"
				  << "<td><strong style=\"color:#34d399;\">" << Format("%.1f GB", free_gb) << "</strong></td>"
				  << "<td style=\"width:220px;\">" << ManagerSkylarkApp::RenderProgressBar(pct) << "</td>"
				  << "</tr>\n";
			}
		}
	} else {
		// Remote SSH df -k -P
		String user = node.ssh_user.IsEmpty() ? "sblo" : node.ssh_user;
		int port = node.ssh_port > 0 ? node.ssh_port : 22;
		String cmd = Format("ssh -o BatchMode=yes -o ConnectTimeout=3 -p %d %s@%s \"df -k -P\"",
		                    port, user, node.hostname);
		String out = Sys("sh", {"-c", cmd});
		Vector<String> lines = Split(out, '\n');

		for(int i = 1; i < lines.GetCount(); i++) {
			String line = lines[i];
			Vector<String> parts = Split(line, [](int c)->int { return c == ' ' || c == '\t'; });
			if(parts.GetCount() >= 6) {
				String fs = parts[0];
				double total_kb = ScanDouble(parts[1]);
				double used_kb = ScanDouble(parts[2]);
				double free_kb = ScanDouble(parts[3]);
				String mount = parts[5];

				if(total_kb > 100000) { // filter tiny devfs
					double total_gb = total_kb / (1024.0 * 1024.0);
					double used_gb = used_kb / (1024.0 * 1024.0);
					double free_gb = free_kb / (1024.0 * 1024.0);
					double pct = (total_gb > 0) ? (used_gb / total_gb * 100.0) : 0.0;

					b << "<tr>"
					  << "<td><code>" << mount << "</code> <small style=\"color:#64748b;\">(" << fs << ")</small></td>"
					  << "<td>" << Format("%.1f GB", total_gb) << "</td>"
					  << "<td>" << Format("%.1f GB", used_gb) << "</td>"
					  << "<td><strong style=\"color:#34d399;\">" << Format("%.1f GB", free_gb) << "</strong></td>"
					  << "<td style=\"width:220px;\">" << ManagerSkylarkApp::RenderProgressBar(pct) << "</td>"
					  << "</tr>\n";
				}
			}
		}
	}

	b << "  </tbody></table>\n</div>\n";
	return b;
}

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
