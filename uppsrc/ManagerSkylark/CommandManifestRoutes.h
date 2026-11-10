#ifndef _ManagerSkylark_CommandManifestRoutes_h_
#define _ManagerSkylark_CommandManifestRoutes_h_

NAMESPACE_UPP

// Registers the broker operations of the composed sites (PrivateHub, Science,
// Company) in the command manifest. Called once the sites are composed.
void RegisterComposedBrokerCommands();

// Registers the GitHub JSON API routes of this host. Called at startup.
void RegisterGitHubCommands();

END_UPP_NAMESPACE

#endif
