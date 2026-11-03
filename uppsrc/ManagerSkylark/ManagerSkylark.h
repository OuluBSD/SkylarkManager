#ifndef _reference_ManagerSkylark_ManagerSkylark_h_
#define _reference_ManagerSkylark_ManagerSkylark_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include <Manager/Manager.h>
#include <SweAgent/SweAgent.h>
#include <WebChat/WebChat.h>
#include <WebFramework/WebFramework.h>
#include "V2Model.h"
#include "GitHubPlatform.h"
#include "SourceForgePlatform.h"
#include "SystemAuth.h"

NAMESPACE_UPP

class ManagerSkylarkApp : public SkylarkApp {
public:
	ManagerSkylarkApp();

	bool Initialize(const Vector<String>& args);
	void InitApp();

	V2Store&      GetV2Store()      { return v2_store; }
	ManagerStore& GetLocalStore()   { static StaticMutex sMutex; Mutex::Lock __(sMutex); local_store.LoadAll(); return local_store; }

	static ManagerSkylarkApp& Get();

	void StopServer();

	static bool RunSelfTests(int port = 18080);

	// Shared HTML rendering components
	static String RenderHtmlPage(const String& title, const String& active_nav, const String& body_content, const Http* http = nullptr);
	static void   AddMarkdownSupport(PageBuilder& pb);
	static String RenderBadge(const String& status);
	static String RenderProgressBar(double percent);
	static NavigationModel BuildNavigation(const String& active_nav = "");
	static TreeModel       BuildMeshFleetTree(const String& active_nav = "", const String& active_node_id = "");
	static TreeModel       BuildGoalsTree(V2Store& store, const String& active_id = "");
	static TreeModel       BuildProjectsTree(ManagerStore& store, const String& active_slug = "");

private:
	static ManagerSkylarkApp* s_instance;

	V2Store      v2_store;
	ManagerStore local_store;
	bool         test_mode;
	int          custom_port;
};

END_UPP_NAMESPACE

#endif
