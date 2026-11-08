#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
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
	  << "        " << VividIcon::Get("alert-triangle", 18) << " <span>Operator Notice: C++ Test Matrix Coverage &amp; Cache State</span>\n"
	  << "      </div>\n"
	  << "      <div style=\"font-size:0.86rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.4;\">\n"
	  << "        <strong>8 packages up-to-date &amp; cached</strong> (0s recompilation, source &amp; dependency hashes matched).<br>\n"
	  << "        <span style=\"color:#f0883e;\">&#9888; <strong>2 package configurations untested</strong></span> for USE flag combination <code>flagUSEMALLOC</code>.<br>\n"
	  << "        Full matrix rebuild estimated at <strong>4m 15s</strong> &middot; Delta rebuild: <strong>18s</strong>.\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:10px; align-items:center; flex-wrap:wrap;\">\n"
	  << "      <button id=\"btn-run-delta-build\" onclick=\"runWorkflowDispatch('upp-build', 'delta')\" class=\"wf-btn wf-btn-primary\" style=\"padding:6px 14px; font-size:0.82rem; font-weight:600;\">\n"
	  << "        " << VividIcon::Get("play", 13) << " Run Delta Build (18s)\n"
	  << "      </button>\n"
	  << "      <button id=\"btn-run-full-matrix\" onclick=\"runWorkflowDispatch('upp-build', 'matrix')\" class=\"wf-btn wf-btn-secondary\" style=\"padding:6px 14px; font-size:0.82rem;\">\n"
	  << "        Run Full Matrix (4m 15s)\n"
	  << "      </button>\n"
	  << "      <a href=\"/repo/" << owner << "/" << repo << "/actions/new\" id=\"btn-new-workflow\" class=\"wf-btn\" style=\"padding:6px 14px; font-size:0.82rem; background:#238636; color:#ffffff; font-weight:600; text-decoration:none;\">\n"
	  << "        " << VividIcon::Get("plus", 14) << " New workflow\n"
	  << "      </a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function runWorkflowDispatch(wf, type) {\n"
	  << "  let btn = (type === 'delta' ? document.getElementById('btn-run-delta-build') : document.getElementById('btn-run-full-matrix'));\n"
	  << "  if(btn) {\n"
	  << "    btn.disabled = true;\n"
	  << "    btn.innerHTML = '<span>&#9203; Dispatching SysProcess...</span>';\n"
	  << "  }\n"
	  << "  fetch('/api/gh/actions/trigger', {\n"
	  << "    method: 'POST',\n"
	  << "    headers: { 'Content-Type': 'application/json' },\n"
	  << "    body: JSON.stringify({\n"
	  << "      owner: '" << owner << "',\n"
	  << "      repo: '" << repo << "',\n"
	  << "      workflow: (type === 'delta' ? 'U++ Build System (Delta 18s)' : 'U++ Full Matrix Rebuild (4m 15s)'),\n"
	  << "      type: 'workflow_dispatch'\n"
	  << "    })\n"
	  << "  })\n"
	  << "  .then(r => r.json())\n"
	  << "  .then(d => {\n"
	  << "    setTimeout(() => { window.location.reload(); }, 600);\n"
	  << "  })\n"
	  << "  .catch(e => {\n"
	  << "    alert('Workflow dispatch error: ' + e);\n"
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

	String h;
	h << "<div style=\"display:flex; flex-direction:column; gap:1.25rem;\">\n"
	  << "  <!-- Top Header & Search Bar -->\n"
	  << "  <div class=\"wf-card\">\n"
	  << "    <div style=\"padding:16px 20px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:16px;\">\n"
	  << "      <div>\n"
	  << "        <h2 style=\"margin:0 0 4px 0; font-size:1.35rem; font-weight:700;\">Choose a workflow</h2>\n"
	  << "        <p style=\"margin:0; font-size:0.86rem; color:var(--wf-fg-inactive,#8b919d);\">Choose a starter workflow template to configure continuous integration, deployment, automation, or documentation for this repository.</p>\n"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
	  << "        <input type=\"text\" id=\"workflow-search\" placeholder=\"Search workflows...\" value=\"" << EscapeHtml(search_q) << "\" oninput=\"filterWorkflows(this.value)\" class=\"wf-input\" style=\"padding:6px 12px; font-size:0.85rem; border-radius:6px; background:var(--wf-bg-view,#151d2b); border:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); color:var(--wf-fg-normal,#d3dae3); width:260px;\">\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "\n"
	  << "  <!-- Category Nav Pills -->\n"
	  << "  <div style=\"display:flex; gap:8px; flex-wrap:wrap; align-items:center;\">\n"
	  << "    <a href=\"/repo/" << owner << "/" << repo << "/actions/new\" class=\"wf-btn" << (cat == "all" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">All</a>\n"
	  << "    <a href=\"/repo/" << owner << "/" << repo << "/actions/new?category=continuous-integration\" class=\"wf-btn" << (cat == "continuous-integration" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Continuous integration</a>\n"
	  << "    <a href=\"/repo/" << owner << "/" << repo << "/actions/new?category=deployment\" class=\"wf-btn" << (cat == "deployment" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Deployment</a>\n"
	  << "    <a href=\"/repo/" << owner << "/" << repo << "/actions/new?category=automation\" class=\"wf-btn" << (cat == "automation" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Automation</a>\n"
	  << "    <a href=\"/repo/" << owner << "/" << repo << "/actions/new?category=pages\" class=\"wf-btn" << (cat == "pages" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Pages</a>\n"
	  << "  </div>\n"
	  << "\n";

	// Suggested for this repository section
	if(cat == "all" || cat == "continuous-integration" || cat == "pages") {
		h << "  <div id=\"section-suggested\">\n"
		  << "    <div style=\"display:flex; align-items:center; gap:8px; font-weight:700; font-size:1.05rem; margin-bottom:10px;\">\n"
		  << "      <span>Suggested for this repository</span>\n"
		  << "      <span class=\"wf-badge wf-badge-cyan\">Detected C++ &amp; U++ Codebase</span>\n"
		  << "    </div>\n"
		  << "    <div style=\"display:grid; grid-template-columns:repeat(auto-fill, minmax(360px, 1fr)); gap:16px;\">\n";

		for(const WorkflowTemplate& t : templates) {
			if(!t.suggested) continue;
			if(cat != "all" && t.category != cat) continue;
			h << "      <div class=\"wf-card wf-workflow-card\" style=\"padding:18px; display:flex; flex-direction:column; justify-content:space-between; border:1px solid rgba(0,161,197,0.4); background:rgba(0,161,197,0.03);\">\n"
			  << "        <div>\n"
			  << "          <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:10px;\">\n"
			  << "            <div style=\"display:flex; align-items:center; gap:10px;\">\n"
			  << "              <div style=\"padding:8px; border-radius:6px; background:rgba(0,161,197,0.15); color:var(--wf-accent-focus,#00a1c5);\">\n"
			  << "                " << VividIcon::Get(t.icon, 22) << "\n"
			  << "              </div>\n"
			  << "              <div>\n"
			  << "                <div style=\"font-weight:700; font-size:1.05rem;\">" << EscapeHtml(t.title) << "</div>\n"
			  << "                <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(t.author) << "</div>\n"
			  << "              </div>\n"
			  << "            </div>\n"
			  << "            <span class=\"wf-badge wf-badge-emerald\">Suggested</span>\n"
			  << "          </div>\n"
			  << "          <p style=\"font-size:0.86rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.4; margin-bottom:14px;\">" << EscapeHtml(t.description) << "</p>\n"
			  << "        </div>\n"
			  << "        <div style=\"display:flex; justify-content:space-between; align-items:center; padding-top:12px; border-top:1px solid var(--wf-border-color,rgba(0,161,197,0.15));\">\n"
			  << "          <span style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << t.default_filename << "</code></span>\n"
			  << "          <div style=\"display:flex; gap:8px;\">\n";
			if(t.id == "pages-upp-tpp") {
				h << "            <a href=\"/repo/" << owner << "/" << repo << "/actions/new?template=pages-upp-tpp&preview=1\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; padding:4px 10px;\">Preview Site</a>\n";
			}
			h << "            <a href=\"/repo/" << owner << "/" << repo << "/actions/new?template=" << t.id << "\" class=\"wf-btn wf-btn-primary\" style=\"font-size:0.8rem; padding:4px 12px; font-weight:600;\">Configure</a>\n"
			  << "          </div>\n"
			  << "        </div>\n"
			  << "      </div>\n";
		}
		h << "    </div>\n"
		  << "  </div>\n\n";
	}

	// Other Categories Grid
	h << "  <div>\n"
	  << "    <div style=\"font-weight:700; font-size:1.05rem; margin-bottom:12px;\">\n"
	  << "      <span>" << (cat == "all" ? "All Starter Workflows" : (cat == "continuous-integration" ? "Continuous Integration" : (cat == "deployment" ? "Deployment" : (cat == "automation" ? "Automation" : "Pages")))) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:grid; grid-template-columns:repeat(auto-fill, minmax(320px, 1fr)); gap:16px;\">\n";

	for(const WorkflowTemplate& t : templates) {
		if(t.suggested && cat == "all") continue; // Already shown in suggested
		if(cat != "all" && t.category != cat) continue;

		h << "      <div class=\"wf-card wf-workflow-card\" style=\"padding:16px; display:flex; flex-direction:column; justify-content:space-between;\">\n"
		  << "        <div>\n"
		  << "          <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:10px;\">\n"
		  << "            <div style=\"display:flex; align-items:center; gap:10px;\">\n"
		  << "              <div style=\"padding:6px; border-radius:6px; background:var(--wf-bg-alt,#1c202f); color:var(--wf-accent-focus,#00a1c5);\">\n"
		  << "                " << VividIcon::Get(t.icon, 20) << "\n"
		  << "              </div>\n"
		  << "              <div>\n"
		  << "                <div style=\"font-weight:700; font-size:0.96rem;\">" << EscapeHtml(t.title) << "</div>\n"
		  << "                <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(t.author) << "</div>\n"
		  << "              </div>\n"
		  << "            </div>\n"
		  << "            <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.7rem;\">" << t.category << "</span>\n"
		  << "          </div>\n"
		  << "          <p style=\"font-size:0.84rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.4; margin-bottom:12px;\">" << EscapeHtml(t.description) << "</p>\n"
		  << "        </div>\n"
		  << "        <div style=\"display:flex; justify-content:space-between; align-items:center; padding-top:10px; border-top:1px solid var(--wf-border-color,rgba(0,161,197,0.15));\">\n"
		  << "          <span style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << t.default_filename << "</code></span>\n"
		  << "          <a href=\"/repo/" << owner << "/" << repo << "/actions/new?template=" << t.id << "\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; padding:4px 12px; font-weight:600;\">Configure</a>\n"
		  << "        </div>\n"
		  << "      </div>\n";
	}

	h << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function filterWorkflows(q) {\n"
	  << "  q = (q || '').toLowerCase();\n"
	  << "  document.querySelectorAll('.wf-workflow-card').forEach(c => {\n"
	  << "    let t = c.innerText.toLowerCase();\n"
	  << "    c.style.display = (!q || t.includes(q)) ? 'flex' : 'none';\n"
	  << "  });\n"
	  << "}\n"
	  << "</script>\n";

	return h;
}

String GitHubPlatformService::RenderWorkflowEditor(const String& owner, const String& repo, const String& template_id, const String& filename)
{
	WorkflowTemplate tpl = GetWorkflowTemplate(template_id);
	Vector<MarketplaceAction> market_actions = GetMarketplaceActions();

	String fn = filename;
	if(fn.IsEmpty()) fn = tpl.default_filename;
	if(fn.IsEmpty()) fn = "workflow.yml";

	String h;
	h << "<div style=\"display:flex; flex-direction:column; gap:14px;\">\n"
	  << "  <!-- Toast Notification -->\n"
	  << "  <div id=\"workflow-toast\" style=\"display:none; padding:10px 16px; background:#238636; color:#ffffff; font-weight:600; border-radius:6px; font-size:0.88rem;\"></div>\n"
	  << "\n"
	  << "  <!-- Top Bar: Breadcrumb + Action Controls -->\n"
	  << "  <div class=\"wf-card\" style=\"padding:12px 18px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div style=\"display:flex; align-items:center; gap:8px; font-size:0.9rem;\">\n"
	  << "      <a href=\"/repo/" << owner << "/" << repo << "/actions/new\" style=\"color:var(--wf-accent-link,#00acd3); text-decoration:none; font-weight:600;\">Actions</a>\n"
	  << "      <span style=\"color:var(--wf-fg-inactive,#8b919d);\">/</span>\n"
	  << "      <span style=\"color:var(--wf-fg-inactive,#8b919d);\">.github / workflows /</span>\n"
	  << "      <input type=\"text\" id=\"workflow-filename\" value=\"" << EscapeHtml(fn) << "\" class=\"wf-input\" style=\"padding:4px 8px; font-size:0.85rem; font-family:monospace; width:220px; background:var(--wf-bg-view,#151d2b); border:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); color:#ffffff; border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:10px; align-items:center;\">\n"
	  << "      <a href=\"/repo/" << owner << "/" << repo << "/actions/new\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem; padding:5px 12px;\">Cancel</a>\n"
	  << "      <button id=\"btn-commit-workflow\" onclick=\"saveWorkflowFile()\" class=\"wf-btn\" style=\"background:#238636; color:#ffffff; font-weight:600; font-size:0.82rem; padding:6px 16px; border:none; border-radius:6px; cursor:pointer;\">"
	  << VividIcon::Get("check", 14) << " Commit changes...</button>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "\n"
	  << "  <!-- Dual Layout: Code Editor on Left, Marketplace / Docs on Right -->\n"
	  << "  <div style=\"display:flex; gap:16px; align-items:flex-start;\">\n"
	  << "    <!-- Left: YAML Workflow Code Editor -->\n"
	  << "    <div class=\"wf-card\" style=\"flex:1; min-width:0; padding:12px; display:flex; flex-direction:column;\">\n"
	  << "      <div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d);\">\n"
	  << "        <span>Workflow Definition (YAML) &middot; <code>" << EscapeHtml(tpl.title) << "</code></span>\n"
	  << "        <span style=\"font-size:0.75rem;\">UTF-8 &middot; Indent: 2 spaces</span>\n"
	  << "      </div>\n"
	  << "      <textarea id=\"workflow-editor-yaml\" spellcheck=\"false\" style=\"width:100%; height:580px; font-family:Consolas, 'Courier New', monospace; font-size:0.88rem; line-height:1.5; background:#0d1117; color:#e6edf3; border:1px solid #30363d; border-radius:6px; padding:14px; box-sizing:border-box; resize:vertical; outline:none;\">"
	  << EscapeHtml(tpl.default_yaml) << "</textarea>\n"
	  << "    </div>\n"
	  << "\n"
	  << "    <!-- Right: Tabbed Panel (Marketplace & Documentation) -->\n"
	  << "    <div class=\"wf-card\" style=\"width:380px; flex-shrink:0; padding:12px; display:flex; flex-direction:column; max-height:640px; overflow-y:auto;\">\n"
	  << "      <!-- Drawer Tabs -->\n"
	  << "      <div style=\"display:flex; border-bottom:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); margin-bottom:12px;\">\n"
	  << "        <button id=\"tab-btn-marketplace\" onclick=\"switchDrawerTab('marketplace')\" style=\"flex:1; background:none; border:none; padding:8px; font-weight:700; font-size:0.85rem; color:#ffffff; border-bottom:2px solid #00a1c5; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;\">"
	  << VividIcon::Get("folder", 14) << " Marketplace</button>\n"
	  << "        <button id=\"tab-btn-docs\" onclick=\"switchDrawerTab('docs')\" style=\"flex:1; background:none; border:none; padding:8px; font-weight:600; font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); border-bottom:none; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;\">"
	  << VividIcon::Get("file", 14) << " Documentation</button>\n"
	  << "      </div>\n"
	  << "\n"
	  << "      <!-- View 1: Marketplace Tab Content -->\n"
	  << "      <div id=\"view-marketplace\" style=\"display:block;\">\n"
	  << "        <input type=\"text\" id=\"marketplace-search\" placeholder=\"Search actions...\" oninput=\"filterMarketplace(this.value)\" class=\"wf-input\" style=\"width:100%; padding:6px 10px; font-size:0.82rem; margin-bottom:12px; box-sizing:border-box; background:var(--wf-bg-alt,#1c202f); border:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); color:#ffffff; border-radius:4px;\">\n"
	  << "        <div style=\"font-size:0.78rem; font-weight:700; color:var(--wf-fg-inactive,#8b919d); margin-bottom:8px; text-transform:uppercase;\">Featured Actions</div>\n"
	  << "        <div style=\"display:flex; flex-direction:column; gap:8px;\">\n";

	for(const MarketplaceAction& a : market_actions) {
		h << "          <div class=\"wf-marketplace-item\" style=\"padding:10px; border-radius:6px; background:var(--wf-bg-alt,#1c202f); border:1px solid var(--wf-border-color,rgba(0,161,197,0.15));\">\n"
		  << "            <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
		  << "              <div>\n"
		  << "                <div style=\"font-weight:700; font-size:0.88rem; color:#ffffff;\">" << EscapeHtml(a.name) << "</div>\n"
		  << "                <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">by " << EscapeHtml(a.author) << " &middot; " << a.version << " &middot; &#9733; " << a.stars << "</div>\n"
		  << "              </div>\n"
		  << "              <button onclick=\"insertActionSnippet(" << AsCString(a.yaml_snippet) << ")\" class=\"wf-btn wf-btn-secondary\" style=\"padding:3px 8px; font-size:0.75rem;\">Insert</button>\n"
		  << "            </div>\n"
		  << "            <div style=\"font-size:0.8rem; color:var(--wf-fg-normal,#d3dae3); margin-top:4px; line-height:1.3;\">" << EscapeHtml(a.description) << "</div>\n"
		  << "          </div>\n";
	}

	h << "        </div>\n"
	  << "      </div>\n"
	  << "\n"
	  << "      <!-- View 2: Documentation Tab Content -->\n"
	  << "      <div id=\"view-documentation\" style=\"display:none; font-size:0.84rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.5;\">\n"
	  << "        <h4 style=\"margin:0 0 6px 0; color:#00acd3; font-size:0.95rem;\">Workflow Syntax Reference</h4>\n"
	  << "        <p style=\"margin-bottom:8px;\">Workflows use YAML syntax with the following root keys:</p>\n"
	  << "        <ul style=\"padding-left:18px; margin:0 0 12px 0;\">\n"
	  << "          <li><code>name</code>: Identifier shown in Actions runs list</li>\n"
	  << "          <li><code>on</code>: Triggers (<code>push</code>, <code>pull_request</code>, <code>workflow_dispatch</code>)</li>\n"
	  << "          <li><code>jobs</code>: Set of parallel/sequential execution jobs</li>\n"
	  << "          <li><code>steps</code>: List of tasks invoking actions or shell scripts</li>\n"
	  << "        </ul>\n"
	  << "        <h4 style=\"margin:10px 0 6px 0; color:#00acd3; font-size:0.95rem;\">U++ Build Flag Guide</h4>\n"
	  << "        <ul style=\"padding-left:18px; margin:0;\">\n"
	  << "          <li><code>method</code>: <code>CLANG</code>, <code>GCC</code>, <code>MINGW</code></li>\n"
	  << "          <li><code>MT</code>: Multi-threaded memory allocator and runtime</li>\n"
	  << "          <li><code>BLITZ</code>: Ultra-fast unity build compilation</li>\n"
	  << "          <li><code>DEBUG / DEBUG_FULL</code>: Debug symbols with runtime checks</li>\n"
	  << "          <li><code>flagSOMEFLAG</code>: Custom preprocessor definition mapping</li>\n"
	  << "        </ul>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function switchDrawerTab(tab) {\n"
	  << "  let mTab = document.getElementById('tab-btn-marketplace');\n"
	  << "  let dTab = document.getElementById('tab-btn-docs');\n"
	  << "  let mView = document.getElementById('view-marketplace');\n"
	  << "  let dView = document.getElementById('view-documentation');\n"
	  << "  if(tab === 'marketplace') {\n"
	  << "    mTab.style.borderBottom = '2px solid #00a1c5';\n"
	  << "    mTab.style.color = '#ffffff';\n"
	  << "    dTab.style.borderBottom = 'none';\n"
	  << "    dTab.style.color = 'var(--wf-fg-inactive,#8b919d)';\n"
	  << "    mView.style.display = 'block';\n"
	  << "    dView.style.display = 'none';\n"
	  << "  } else {\n"
	  << "    dTab.style.borderBottom = '2px solid #00a1c5';\n"
	  << "    dTab.style.color = '#ffffff';\n"
	  << "    mTab.style.borderBottom = 'none';\n"
	  << "    mTab.style.color = 'var(--wf-fg-inactive,#8b919d)';\n"
	  << "    mView.style.display = 'none';\n"
	  << "    dView.style.display = 'block';\n"
	  << "  }\n"
	  << "}\n"
	  << "function insertActionSnippet(snippet) {\n"
	  << "  let ed = document.getElementById('workflow-editor-yaml');\n"
	  << "  if(!ed) return;\n"
	  << "  let pos = ed.selectionStart || ed.value.length;\n"
	  << "  let text = ed.value;\n"
	  << "  ed.value = text.substring(0, pos) + '\\n' + snippet + text.substring(pos);\n"
	  << "  ed.focus();\n"
	  << "  let toast = document.getElementById('workflow-toast');\n"
	  << "  if(toast) {\n"
	  << "    toast.style.display = 'block';\n"
	  << "    toast.innerText = 'Action snippet inserted into workflow editor!';\n"
	  << "    setTimeout(() => { toast.style.display = 'none'; }, 2200);\n"
	  << "  }\n"
	  << "}\n"
	  << "function filterMarketplace(q) {\n"
	  << "  q = (q || '').toLowerCase();\n"
	  << "  document.querySelectorAll('.wf-marketplace-item').forEach(el => {\n"
	  << "    let t = el.innerText.toLowerCase();\n"
	  << "    el.style.display = (!q || t.includes(q)) ? 'block' : 'none';\n"
	  << "  });\n"
	  << "}\n"
	  << "function saveWorkflowFile() {\n"
	  << "  let btn = document.getElementById('btn-commit-workflow');\n"
	  << "  let fn = document.getElementById('workflow-filename').value.trim();\n"
	  << "  let yaml = document.getElementById('workflow-editor-yaml').value;\n"
	  << "  if(!fn) { alert('Filename cannot be empty'); return; }\n"
	  << "  btn.innerText = 'Committing...';\n"
	  << "  btn.disabled = true;\n"
	  << "  fetch('/api/gh/workflows/save', {\n"
	  << "    method: 'POST',\n"
	  << "    headers: { 'Content-Type': 'application/json' },\n"
	  << "    body: JSON.stringify({\n"
	  << "      owner: '" << owner << "',\n"
	  << "      repo: '" << repo << "',\n"
	  << "      filename: fn,\n"
	  << "      content: yaml\n"
	  << "    })\n"
	  << "  })\n"
	  << "  .then(r => r.json())\n"
	  << "  .then(data => {\n"
	  << "    if(data.status === 'ok') {\n"
	  << "      btn.innerText = 'Committed!';\n"
	  << "      btn.style.background = '#0969da';\n"
	  << "      let toast = document.getElementById('workflow-toast');\n"
	  << "      if(toast) {\n"
	  << "        toast.style.display = 'block';\n"
	  << "        toast.innerText = 'Workflow successfully committed to ' + data.path + '!';\n"
	  << "      }\n"
	  << "      setTimeout(() => {\n"
	  << "        window.location.href = '/repo/" << owner << "/" << repo << "?tab=actions';\n"
	  << "      }, 1200);\n"
	  << "    } else {\n"
	  << "      alert('Error saving workflow: ' + (data.message || 'unknown error'));\n"
	  << "      btn.innerText = 'Commit changes...';\n"
	  << "      btn.disabled = false;\n"
	  << "    }\n"
	  << "  })\n"
	  << "  .catch(err => {\n"
	  << "    alert('Failed to save workflow: ' + err);\n"
	  << "    btn.innerText = 'Commit changes...';\n"
	  << "    btn.disabled = false;\n"
	  << "  });\n"
	  << "}\n"
	  << "</script>\n";

	return h;
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

	String h;
	h << "<div style=\"background:#ffffff; color:#24292f; border-radius:8px; overflow:hidden; font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;\">\n"
	  << "  <!-- Top U++ Banner -->\n"
	  << "  <div style=\"background:#003366; color:#ffffff; padding:12px 20px; display:flex; justify-content:space-between; align-items:center;\">\n"
	  << "    <div style=\"display:flex; align-items:center; gap:12px;\">\n"
	  << "      <span style=\"font-size:1.3rem; font-weight:800; letter-spacing:0.5px;\">Ultimate++</span>\n"
	  << "      <span style=\"font-size:0.95rem; color:#8ab4f8;\">Documentation &middot; " << tpp_rel_dir << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:10px;\">\n"
	  << "      <a href=\"/repo/SeppoPakonen/ai-upp-priv/actions/new?template=pages-upp-tpp\" class=\"wf-btn\" style=\"background:#ffffff; color:#003366; font-weight:600; padding:4px 12px; font-size:0.8rem; text-decoration:none;\">"
	  << VividIcon::Get("arrow-left", 14) << " Back to Workflow Editor</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <!-- Two-Column Layout matching ultimatepp.org -->\n"
	  << "  <div style=\"display:flex; min-height:600px;\">\n"
	  << "    <!-- Sidebar Navigation -->\n"
	  << "    <div style=\"width:280px; background:#f6f8fa; border-right:1px solid #d0d7de; padding:14px; flex-shrink:0;\">\n"
	  << "      <div style=\"font-weight:700; font-size:0.88rem; color:#57606a; margin-bottom:10px; text-transform:uppercase; letter-spacing:0.5px;\">Topics (" << files.GetCount() << ")</div>\n"
	  << "      <ul style=\"list-style:none; padding:0; margin:0; display:flex; flex-direction:column; gap:4px;\">\n";

	for(int i = 0; i < files.GetCount(); i++) {
		String ftitle = titles[i];
		bool active = (i == 0);
		h << "        <li><a href=\"#topic-" << i << "\" onclick=\"showTppDoc(" << i << ")\" id=\"tpp-nav-" << i << "\" style=\"display:block; padding:5px 8px; border-radius:4px; font-size:0.86rem; color:" << (active ? "#0969da; font-weight:600; background:#ddf4ff;" : "#24292f;") << " text-decoration:none;\">"
		  << EscapeHtml(ftitle) << "</a></li>\n";
	}

	h << "      </ul>\n"
	  << "    </div>\n"
	  << "    <!-- Main Content Reader -->\n"
	  << "    <div id=\"tpp-doc-content\" style=\"flex:1; padding:24px 32px; background:#ffffff; overflow-y:auto; max-height:800px;\">\n";

	for(int i = 0; i < files.GetCount(); i++) {
		String tit = titles[i];
		String qtf = ExtractTppText(files[i], tit);
		String body_html = QtfToReadableHtml(qtf);
		h << "      <div class=\"tpp-topic-pane\" id=\"tpp-pane-" << i << "\" style=\"display:" << (i == 0 ? "block" : "none") << ";\">\n"
		  << "        <div style=\"font-size:0.82rem; color:#57606a; margin-bottom:6px;\">" << tpp_rel_dir << " / " << GetFileName(files[i]) << "</div>\n"
		  << "        <h1 style=\"font-size:1.6rem; font-weight:700; border-bottom:1px solid #d0d7de; padding-bottom:8px; margin-bottom:16px; color:#1f2328;\">" << EscapeHtml(tit) << "</h1>\n"
		  << "        <div class=\"tpp-rendered-body\" style=\"font-size:0.95rem; color:#1f2328; line-height:1.6;\">\n"
		  << body_html << "\n"
		  << "        </div>\n"
		  << "      </div>\n";
	}

	h << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function showTppDoc(idx) {\n"
	  << "  document.querySelectorAll('.tpp-topic-pane').forEach((p, i) => { p.style.display = (i === idx ? 'block' : 'none'); });\n"
	  << "  document.querySelectorAll('[id^=\"tpp-nav-\"]').forEach((a, i) => {\n"
	  << "    a.style.color = (i === idx ? '#0969da' : '#24292f');\n"
	  << "    a.style.fontWeight = (i === idx ? '600' : 'normal');\n"
	  << "    a.style.background = (i === idx ? '#ddf4ff' : 'transparent');\n"
	  << "  });\n"
	  << "}\n"
	  << "</script>\n";

	return h;
}

END_UPP_NAMESPACE
