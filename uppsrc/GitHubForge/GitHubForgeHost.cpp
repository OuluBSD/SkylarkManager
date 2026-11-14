#include "GitHubForgeHost.h"

NAMESPACE_UPP

GitHubForgeHost& GetGitHubForgeHost()
{
	static GitHubForgeHost host;
	return host;
}

END_UPP_NAMESPACE
