#ifndef _reference_ManagerSkylark_GitHubRepoPage_h_
#define _reference_ManagerSkylark_GitHubRepoPage_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include "GitHubPlatform.h"

NAMESPACE_UPP

// Parsed route state and the rendered fragments for one GitHub repo page.
// The orchestrator (GitHubRepoView.cpp) fills it, tab renderers append to
// content and sidebar.
struct GitHubRepoPage {
	String        owner;
	String        repo;
	LocalRepoInfo info;
	ValueMap      scraped;
	String        active_tab;
	String        mode;
	String        branch;
	String        file_or_folder;
	String        agent_run_id;
	int           issue_num = 0;
	int           pr_num = 0;
	String        sub_nav;
	String        content;
	String        sidebar;
};

void RenderRepoCodeTab(GitHubRepoPage& page);
void RenderRepoCommitsTab(GitHubRepoPage& page);
void RenderRepoIssueDetail(GitHubRepoPage& page);
void RenderRepoIssuesList(GitHubRepoPage& page);
void RenderRepoPullDetail(GitHubRepoPage& page);
void RenderRepoPullsList(GitHubRepoPage& page);
void RenderRepoActionsTab(Http& http, GitHubRepoPage& page);
void RenderRepoProjectsTab(GitHubRepoPage& page);
void RenderRepoSecurityTab(GitHubRepoPage& page);
void RenderRepoSettingsTab(GitHubRepoPage& page);
void RenderRepoAgentsTab(Http& http, GitHubRepoPage& page);
void RenderRepoFallbackTab(GitHubRepoPage& page);
void RenderRepoClientScript(GitHubRepoPage& page);

END_UPP_NAMESPACE

#endif
