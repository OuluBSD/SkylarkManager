#include "ManagerSkylark.h"
#include "SystemAuth.h"
#include "AssessmentViews.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include "HostPlatform.h"

NAMESPACE_UPP

String ManagerSkylarkApp::RenderBadge(const String& status)
{
	String s = ToLower(status);
	String bg = "var(--wf-surface-hover, #2d3748)", fg = "var(--wf-fg-normal, #d3dae3)";
	if(s == "done" || s == "released" || s == "resolved") {
		bg = "var(--wf-accent-positive, #00aa7f)"; fg = "#ffffff";
	}
	else if(s == "active" || s == "in-progress") {
		bg = "var(--wf-accent-focus, #00a1c5)"; fg = "#ffffff";
	}
	else if(s == "open" || s == "planned" || s == "accepted" || s == "verified") {
		bg = "var(--wf-accent-hover, #0088a6)"; fg = "#ffffff";
	}
	else if(s == "blocked" || s == "rejected") {
		bg = "var(--wf-accent-negative, #c83771)"; fg = "#ffffff";
	}
	else if(s == "proposed" || s == "draft") {
		bg = "var(--wf-accent-neutral, #6d55aa)"; fg = "#ffffff";
	}

	return Format("<span class=\"wf-badge\" style=\"display:inline-block; padding:3px 8px; border-radius:12px; font-size:12px; font-weight:600; color:%s; background-color:%s;\">%s</span>",
	              fg, bg, status);
}

String ManagerSkylarkApp::RenderProgressBar(double percent)
{
	if(percent < 0) percent = 0;
	if(percent > 100) percent = 100;
	return Format("<div style=\"width:100%%; background-color:var(--wf-bg-alt, #1c202f); border-radius:6px; height:12px; overflow:hidden; border:1px solid var(--wf-border-color, rgba(0,161,197,0.2));\">"
	              "<div style=\"width:%.1f%%; background-color:var(--wf-accent-focus, #00a1c5); height:100%%; border-radius:5px;\"></div>"
	              "</div><span style=\"font-size:11px; color:var(--wf-fg-inactive, #8b919d);\">%.0f%%</span>",
	              percent, percent);
}

String ManagerSkylarkApp::RenderHtmlPage(const String& title, const String& active_nav, const String& body_content, const Http* http)
{
	NavigationModel nav = BuildNavigation(active_nav);

	PageBuilder pb;
	pb.Title(title + " - Manager Skylark")
	  .Brand("Manager Skylark", "/", "cube")
	  .Navigation(nav)
	  .Template("megamenu")
	  .Theme("vivid-dark")
	  .Density(active_nav == "chat" ? DENSITY_NONE : DENSITY_COMFORTABLE)
	  .Content(body_content);

	// User auth menu in navbar
	if(http) {
		UserSession sess = SystemAuthService::GetCurrentSession(*http);
		pb.NavActions(SystemAuthService::RenderUserMenu(sess));
	} else {
		UserSession guest;
		pb.NavActions(SystemAuthService::RenderUserMenu(guest));
	}

	// Contextual Left Tree configuration
	if(active_nav != "home" && active_nav != "chat" && active_nav != "login") {
		TreeModel tree;
		String tree_title = "Navigation";
		String tree_id = "wf-tree";

		if(active_nav.StartsWith("node") || active_nav.StartsWith("host") || active_nav.StartsWith("mesh")) {
			String active_node_id = "";
			if(active_nav.StartsWith("node_")) {
				Vector<String> parts = Split(active_nav, '_');
				if(parts.GetCount() >= 2)
					active_node_id = parts[1];
			} else if(active_nav.StartsWith("host")) {
				active_node_id = "local-workstation";
			}
			tree = BuildMeshFleetTree(active_nav, active_node_id);
			tree_title = "Compute Fleet & Repositories";
			tree_id = "mesh-tree";
		}
		else if(active_nav == "projects" || active_nav.StartsWith("project")) {
			tree = BuildProjectsTree(Get().GetLocalStore(), "");
			tree_title = "Manager Task Queue";
			tree_id = "mgr-tree";
		}
		else if(active_nav == "goals" || active_nav.StartsWith("goal")) {
			tree = BuildGoalsTree(Get().GetV2Store(), "");
			tree_title = "Milestones & Goals";
			tree_id = "goals-tree";
		}
		else {
			tree = TreeGenerators::FromNavigation(nav);
			tree_title = "Workspace Navigation";
			tree_id = "nav-tree";
		}

		if(!tree.IsEmpty()) {
			pb.LeftTree(tree, tree_title, tree_id);
			pb.AutoOutline(true); // Automatically builds right-side TOC outline
		}
	}

	pb.Footer("Manager Skylark &bull; Built on Ultimate++ Skylark &amp; Core &bull; OpenRouter &amp; SweAgent Autonomous Engineering");

	// Add manager custom component styling integrated with Vivid Plasma palette variables
	String custom_css;
	custom_css << "<style>\n"
	           << "  main.wf-page-body { max-width: var(--wf-container-width, 1500px); margin: 0 auto; }\n"
	           << "  .card { background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: var(--wf-radius, 8px); padding: var(--wf-pad-card, 1.25rem); margin-bottom: var(--wf-gap-grid, 1.25rem); box-shadow: var(--wf-shadow); }\n"
	           << "  .card-header { font-size: 1.15rem; font-weight: 700; margin-bottom: 0.85rem; color: var(--wf-fg-normal, #d3dae3); border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); padding-bottom: 0.5rem; display: flex; justify-content: space-between; align-items: center; }\n"
	           << "  .card-header a { font-size: 0.85rem; }\n"
	           << "  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: var(--wf-gap-grid, 1.25rem); }\n"
	           << "  .grid-3 { display: grid; grid-template-columns: repeat(3, 1fr); gap: var(--wf-gap-grid, 1.25rem); }\n"
	           << "  .grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: var(--wf-gap-grid, 1.25rem); }\n"
	           << "  .stat-card { background: var(--wf-bg-view, #151d2b); border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); border-radius: var(--wf-radius, 8px); padding: var(--wf-pad-card, 1.25rem); display: flex; flex-direction: column; gap: 4px; box-shadow: var(--wf-shadow); }\n"
	           << "  .stat-val { font-size: 2rem; font-weight: 800; color: var(--wf-accent-focus, #00a1c5); }\n"
	           << "  .stat-lbl { font-size: 0.8rem; color: var(--wf-fg-inactive, #8b919d); text-transform: uppercase; letter-spacing: 0.05em; font-weight: 600; }\n"
	           << "  table { width: 100%; border-collapse: collapse; margin-top: 0.5rem; }\n"
	           << "  th { text-align: left; padding: var(--wf-cell-pad, 10px 12px); background-color: var(--wf-bg-alt, #1c202f); color: var(--wf-fg-inactive, #8b919d); font-weight: 600; font-size: 0.82rem; border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); text-transform: uppercase; letter-spacing: 0.04em; }\n"
	           << "  td { padding: var(--wf-cell-pad, 10px 12px); border-bottom: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); font-size: 0.9rem; color: var(--wf-fg-normal, #d3dae3); }\n"
	           << "  tr:hover td { background-color: var(--wf-surface-hover, rgba(0, 161, 197, 0.08)); }\n"
	           << "  .badge-v2 { font-size: 0.75rem; background-color: var(--wf-accent-focus, #00a1c5); color: #ffffff; padding: 2px 7px; border-radius: 4px; font-weight: 600; }\n"
	           << "  .btn { display: inline-block; padding: var(--wf-btn-pad, 6px 14px); border-radius: var(--wf-radius, 8px); font-weight: 600; text-decoration: none; border: none; cursor: pointer; font-size: 0.88rem; transition: all 0.15s; }\n"
	           << "  .btn-primary { background-color: var(--wf-accent-focus, #00a1c5); color: #ffffff; }\n"
	           << "  .btn-primary:hover { background-color: var(--wf-accent-hover, #0088a6); }\n"
	           << "  .code-box { background-color: var(--wf-bg-alt, #1c202f); color: var(--wf-accent-focus, #00a1c5); padding: 1rem; border-radius: var(--wf-radius, 8px); font-family: monospace; font-size: 0.85rem; overflow-x: auto; border: 1px solid var(--wf-border-color, rgba(0, 161, 197, 0.2)); }\n"
	           << "</style>\n";

	pb.ExtraHead(custom_css);
	AddMarkdownSupport(pb);

	return pb.Build();
}

void ManagerSkylarkApp::AddMarkdownSupport(PageBuilder& pb)
{
	String markdown_head;
	markdown_head << "<link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/atom-one-dark.min.css\">\n"
	              << "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js\"></script>\n"
	              << "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/jsdiff/5.2.0/diff.min.js\"></script>\n"
	              << "<script src=\"https://cdn.jsdelivr.net/npm/marked/lib/marked.umd.js\"></script>\n"
	              << "<style>\n"
	              << WebChatView::GetMarkdownStyles() << "\n"
	              << "</style>\n";

	pb.ExtraHead(markdown_head);
	pb.ExtraScripts(WebChatView::GetMarkdownScript());
}

END_UPP_NAMESPACE
