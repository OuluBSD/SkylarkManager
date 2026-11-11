#ifndef _Channels_AgentIpcClient_h_
#define _Channels_AgentIpcClient_h_

#include <Core/Core.h>

NAMESPACE_UPP

// One message as the argv-only agentipc client reports it.
struct AgentIpcMessage : Moveable<AgentIpcMessage> {
	String ts;
	String user;
	String body;

	AgentIpcMessage() {}
	AgentIpcMessage(const AgentIpcMessage& o)
		: ts(o.ts), user(o.user), body(o.body) {}
	AgentIpcMessage& operator=(const AgentIpcMessage& o) {
		if(this != &o) {
			ts = o.ts;
			user = o.user;
			body = o.body;
		}
		return *this;
	}
};

struct AgentIpcReply {
	bool                    ok = false;
	String                  error;
	String                  output;    // the child's stdout (bounded)
	int                     exit_code = -1;
	Vector<AgentIpcMessage> messages;
	Vector<String>          names;
};

// Configuration of the read-only bridge to the AgentIPC node. The identity
// password is held in memory only and reaches the child process as an
// environment variable (never argv, never logs).
struct AgentIpcConfig {
	Vector<String> client_argv; // command prefix; client_argv[0] is the executable
	String direct = "127.0.0.1:7440";
	String server = "skylark";
	String user   = "channels";
	String password;
	String password_env_name = "AGENTIPC_CHANNELS_PW";
	String records_path;        // the node's mesh/records.jsonl (plaintext metadata only)
	int    timeout_ms = 10000;

	AgentIpcConfig() {}
	AgentIpcConfig(const AgentIpcConfig& o)
		: client_argv(clone(o.client_argv)), direct(o.direct), server(o.server), user(o.user)
		, password(o.password), password_env_name(o.password_env_name)
		, records_path(o.records_path), timeout_ms(o.timeout_ms) {}
	AgentIpcConfig& operator=(const AgentIpcConfig& o) {
		if(this != &o) {
			client_argv = clone(o.client_argv);
			direct = o.direct;
			server = o.server;
			user = o.user;
			password = o.password;
			password_env_name = o.password_env_name;
			records_path = o.records_path;
			timeout_ms = o.timeout_ms;
		}
		return *this;
	}
};

// The default command prefix: the agentipc venv python and client script.
Vector<String> AgentIpcDefaultClientArgv();

// Channel names are argv entries; they are validated before any call.
bool AgentIpcValidChannel(const String& chan);

// Reads one channel's recent messages through the argv-only client and keeps
// the last `limit` of them.
AgentIpcReply AgentIpcReadChannel(const AgentIpcConfig& cfg, const String& chan, int limit);

// Lists a channel's members.
AgentIpcReply AgentIpcNames(const AgentIpcConfig& cfg, const String& chan);

// The channel overview, from the node record file's plaintext metadata only
// (never the bodies, which are ciphertext on an encrypted server). Reads at
// most tail_bytes from the end of the file.
struct AgentIpcChannelInfo : Moveable<AgentIpcChannelInfo> {
	String server;
	String chan;
	String last_ts;
	String last_user;

	AgentIpcChannelInfo() {}
	AgentIpcChannelInfo(const AgentIpcChannelInfo& o)
		: server(o.server), chan(o.chan), last_ts(o.last_ts), last_user(o.last_user) {}
	AgentIpcChannelInfo& operator=(const AgentIpcChannelInfo& o) {
		if(this != &o) {
			server = o.server;
			chan = o.chan;
			last_ts = o.last_ts;
			last_user = o.last_user;
		}
		return *this;
	}
};

bool AgentIpcOverview(const AgentIpcConfig& cfg, int tail_bytes,
                      Vector<AgentIpcChannelInfo>& out, String& error);

END_UPP_NAMESPACE

#endif
