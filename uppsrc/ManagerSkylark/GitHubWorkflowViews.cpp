#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "GitHubTemplates.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

static String QtfToReadableHtml(const String& qtf)
{
	String out;
	String cur;
	bool in_tag = false;
	for(int i = 0; i < qtf.GetLength(); i++) {
		char c = qtf[i];
		if(c == '[') {
			in_tag = true;
			continue;
		}
		if(c == ']') {
			in_tag = false;
			continue;
		}
		if(!in_tag) {
			if(c == '&') {
				if(!cur.IsEmpty()) {
					out << "<p style=\"margin-bottom:0.75rem; line-height:1.6;\">" << GitHubPlatformService::EscapeHtml(cur) << "</p>\n";
					cur.Clear();
				}
			}
			else if(c == '\n' || c == '\r') {
				if(!cur.IsEmpty() && cur.Mid(cur.GetLength()-1) != " ") cur.Cat(' ');
			}
			else {
				cur.Cat(c);
			}
		}
	}
	if(!cur.IsEmpty()) {
		out << "<p style=\"margin-bottom:0.75rem; line-height:1.6;\">" << GitHubPlatformService::EscapeHtml(cur) << "</p>\n";
	}
	return out;
}

static String ExtractTppText(const String& file_path, String& title)
{
	String content = LoadFile(file_path);
	if(content.IsEmpty()) return "";

	int tpos = content.Find("TITLE(");
	if(tpos >= 0) {
		int q1 = content.Find('\"', tpos);
		if(q1 >= 0) {
			int q2 = content.Find('\"', q1 + 1);
			if(q2 >= 0) {
				title = content.Mid(q1 + 1, q2 - q1 - 1);
			}
		}
	}
	if(title.IsEmpty()) title = GetFileTitle(file_path);

	int comp_idx = content.Find("COMPRESSED");
	if(comp_idx >= 0) {
		String nums = content.Mid(comp_idx + 10);
		StringBuffer bytes;
		const char *p = nums;
		while(*p) {
			while(*p && !IsDigit(*p)) p++;
			if(!*p) break;
			int val = 0;
			while(*p && IsDigit(*p)) {
				val = val * 10 + (*p - '0');
				p++;
			}
			bytes.Cat((byte)val);
		}
		String qtf = ZDecompress(~bytes, bytes.GetCount());
		return qtf;
	}
	return content;
}

String GitHubPlatformService::RenderOperatorWarningBar(const String& owner, const String& repo)
{
	String h;
	h << "<div class=\"wf-card\" style=\"margin-bottom:1.25rem; border-left:4px solid #d29922; background:rgba(210,153,34,0.06);\">\n"
	  << "  <div style=\"padding:14px 18px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:16px;\">\n"
	  << "    <div style=\"flex:1; min-width:280px;\">\n"
	  << "      <div style=\"display:flex; align-items:center; gap:8px; font-weight:700; color:#f0883e; margin-bottom:4px;\">\n"
	  << "        " << VividIcon::Get("alert-triangle", 18) << " <span>Local build</span>\n"
	  << "      </div>\n"
	  << "      <div style=\"font-size:0.86rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.4;\">\n"
	  << "        Starts <code>bin/build reference/ManagerSkylark</code> from the local ai-upp checkout on this machine. It is not a GitHub Actions run. Build cache state is not read by this view.\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:10px; align-items:center; flex-wrap:wrap;\">\n"
	  << "      <button id=\"btn-start-local-build\" onclick=\"runLocalBuild()\" class=\"wf-btn wf-btn-primary\" style=\"padding:6px 14px; font-size:0.82rem; font-weight:600;\">\n"
	  << "        " << VividIcon::Get("play", 13) << " Start local build\n"
	  << "      </button>\n"
	  << "      <a href=\"/repo/" << owner << "/" << repo << "/actions/new\" id=\"btn-new-workflow\" class=\"wf-btn\" style=\"padding:6px 14px; font-size:0.82rem; background:#238636; color:#ffffff; font-weight:600; text-decoration:none;\">\n"
	  << "        " << VividIcon::Get("plus", 14) << " New workflow\n"
	  << "      </a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function runLocalBuild() {\n"
	  << "  let btn = document.getElementById('btn-start-local-build');\n"
	  << "  if(btn) {\n"
	  << "    btn.disabled = true;\n"
	  << "    btn.innerHTML = '<span>&#9203; Starting local build...</span>';\n"
	  << "  }\n"
	  << "  fetch('/api/gh/actions/trigger', {\n"
	  << "    method: 'POST',\n"
	  << "    headers: { 'Content-Type': 'application/json' },\n"
	  << "    body: JSON.stringify({\n"
	  << "      owner: '" << owner << "',\n"
	  << "      repo: '" << repo << "',\n"

	  << "    })\n"
	  << "  })\n"
	  << "  .then(r => r.json())\n"
	  << "  .then(d => {\n"
	  << "    setTimeout(() => { window.location.reload(); }, 600);\n"
	  << "  })\n"
	  << "  .catch(e => {\n"
	  << "    alert('Local build error: ' + e);\n"
	  << "    if(btn) btn.disabled = false;\n"
	  << "  });\n"
	  << "}\n"
	  << "</script>\n";
	return h;
}

String GitHubPlatformService::RenderActionsCatalog(const String& owner, const String& repo, const String& category, const String& search_q)
{
	Vector<WorkflowTemplate> templates = GetWorkflowTemplates(owner, repo);
	String cat = category;
	if(cat.IsEmpty()) cat = "all";

	bool show_suggested = cat == "all" || cat == "continuous-integration" || cat == "pages";
	String section_title = cat == "all" ? "All Starter Workflows" : cat == "continuous-integration" ? "Continuous Integration" : cat == "deployment" ? "Deployment" : cat == "automation" ? "Automation" : "Pages";

	auto make_row = [](const WorkflowTemplate& t, int icon_size) {
		ValueMap row;
		row.Add("id", t.id);
		row.Add("title", t.title);
		row.Add("author", t.author);
		row.Add("description", t.description);
		row.Add("category", t.category);
		row.Add("default_filename", t.default_filename);
		row.Add("icon", VividIcon::Get(t.icon, icon_size));
		return row;
	};

	ValueArray suggested;
	ValueArray others;
	for(const WorkflowTemplate& t : templates) {
		if(show_suggested && t.suggested && (cat == "all" || t.category == cat)) {
			ValueMap row = make_row(t, 22);
			row.Add("has_preview", t.id == "pages-upp-tpp");
			suggested.Add(row);
		}
		if(!(t.suggested && cat == "all") && (cat == "all" || t.category == cat))
			others.Add(make_row(t, 20));
	}

	Renderer vars;
	vars("owner", owner)("repo", repo)("search_q", search_q)
		("btn_all", String(cat == "all" ? " wf-btn-primary" : " wf-btn-secondary"))
		("btn_ci", String(cat == "continuous-integration" ? " wf-btn-primary" : " wf-btn-secondary"))
		("btn_deployment", String(cat == "deployment" ? " wf-btn-primary" : " wf-btn-secondary"))
		("btn_automation", String(cat == "automation" ? " wf-btn-primary" : " wf-btn-secondary"))
		("btn_pages", String(cat == "pages" ? " wf-btn-primary" : " wf-btn-secondary"))
		("show_suggested", show_suggested)
		("suggested", suggested)
		("section_title", section_title)
		("others", others);
	return RenderGitHubTemplate(vars, "github-actions-catalog");
}

String GitHubPlatformService::RenderWorkflowEditor(const String& owner, const String& repo, const String& template_id, const String& filename)
{
	WorkflowTemplate tpl = GetWorkflowTemplate(template_id);
	Vector<MarketplaceAction> market_actions = GetMarketplaceActions();

	String fn = filename;
	if(fn.IsEmpty()) fn = tpl.default_filename;
	if(fn.IsEmpty()) fn = "workflow.yml";

	ValueArray actions;
	for(const MarketplaceAction& a : market_actions) {
		ValueMap row;
		row.Add("name", a.name);
		row.Add("author", a.author);
		row.Add("version", a.version);
		row.Add("stars", a.stars);
		row.Add("snippet_js", AsCString(a.yaml_snippet));
		row.Add("description", a.description);
		actions.Add(row);
	}

	Renderer vars;
	vars("owner", owner)("repo", repo)("filename", fn)
		("tpl_title", tpl.title)
		("default_yaml", EscapeHtml(tpl.default_yaml))
		("icon_check", VividIcon::Get("check", 14))
		("icon_folder", VividIcon::Get("folder", 14))
		("icon_file", VividIcon::Get("file", 14))
		("actions", actions);
	return RenderGitHubTemplate(vars, "github-actions-editor");
}

String GitHubPlatformService::RenderTppDocSite(const String& tpp_rel_dir)
{
	String full_tpp_dir = AppendFileName(GetHomeDirFile("Dev/ai-upp"), tpp_rel_dir);
	Vector<String> files;
	Vector<String> titles;
	FindFile ff(AppendFileName(full_tpp_dir, "*.tpp"));
	while(ff) {
		if(ff.IsFile()) {
			files.Add(ff.GetPath());
			String tit;
			ExtractTppText(ff.GetPath(), tit);
			titles.Add(tit);
		}
		ff.Next();
	}

	ValueArray nav;
	for(int i = 0; i < files.GetCount(); i++) {
		bool active = (i == 0);
		ValueMap n;
		n.Add("index", i);
		n.Add("style", String(active ? "#0969da; font-weight:600; background:#ddf4ff;" : "#24292f;"));
		n.Add("title", titles[i]);
		nav.Add(n);
	}

	ValueArray panes;
	for(int i = 0; i < files.GetCount(); i++) {
		String tit = titles[i];
		String qtf = ExtractTppText(files[i], tit);
		ValueMap p;
		p.Add("index", i);
		p.Add("display", String(i == 0 ? "block" : "none"));
		p.Add("file", GetFileName(files[i]));
		p.Add("title", tit);
		p.Add("body", QtfToReadableHtml(qtf));
		panes.Add(p);
	}

	Renderer vars;
	vars("rel_dir", tpp_rel_dir)
		("topic_count", files.GetCount())
		("icon_arrow_left", VividIcon::Get("arrow-left", 14))
		("nav", nav)
		("panes", panes);
	return RenderGitHubTemplate(vars, "github-tpp-docsite");
}

END_UPP_NAMESPACE
