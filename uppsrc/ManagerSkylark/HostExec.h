#ifndef _ManagerSkylark_HostExec_h_
#define _ManagerSkylark_HostExec_h_

#include <Core/Core.h>

NAMESPACE_UPP

// Argv-only process execution for the host and mesh features. No code path
// here starts a local shell. Every user-supplied value is checked by one of the
// validators below before it reaches an argv vector or a remote command string.
// Remote commands still run through the remote login shell, because ssh always
// hands its command to that shell; those strings are built only from tokens
// that passed these checks.
namespace HostExec {

bool SafeUser(const String& user);            // [A-Za-z0-9._-], 1..64 chars, no leading '-'
bool SafeHost(const String& host);            // [A-Za-z0-9.-], 1..253 chars, no leading '-' or '.'
bool SafeRemotePath(const String& path);      // [A-Za-z0-9._/~-], 1..512 chars, no "..", no leading '-'
bool SafeRepoName(const String& name);        // [A-Za-z0-9._-], 1..128 chars, no leading '.', no ".."
bool SafePackageName(const String& name);     // [A-Za-z0-9._/-], 1..256 chars, no "..", no leading '-'
bool SafeBuildMethod(const String& method);   // [A-Za-z0-9_], 1..32 chars
bool SafeHash(const String& hash);            // hex digits, 7..40 chars
// Splits a space-separated git argument string into argv tokens. The first token
// must be a read-only or fetch/pull subcommand, and no token may select a
// program or config (-c, --exec, --upload-pack, ...).
bool SafeGitArgs(const String& args, Vector<String>& tokens);
// Splits build flags into tokens; each must be a dash followed by letters (-r, -v, -c).
bool SafeBuildFlags(const String& flags, Vector<String>& tokens);

// ssh options and target for a batch-mode connection, without the remote command.
Vector<String> SshArgs(int port, const String& user, const String& host, int connect_timeout);

// Runs program with args (no shell). merge_stderr gives the 2>&1 behaviour; otherwise
// stderr is discarded (2>/dev/null). Returns stdout, or "" if the program did not start.
String Run(const char *program, const Vector<String>& args, bool merge_stderr, const char *cwd = NULL);

}

END_UPP_NAMESPACE

#endif
