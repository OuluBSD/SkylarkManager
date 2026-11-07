#include "WebAuth.h"

NAMESPACE_UPP

namespace {

String Esc(const String& text)
{
	String out;
	out.Reserve(text.GetCount() + 16);
	for(int i = 0; i < text.GetCount(); i++) {
		int c = text[i];
		switch(c) {
		case '&':  out << "&amp;"; break;
		case '<':  out << "&lt;"; break;
		case '>':  out << "&gt;"; break;
		case '"':  out << "&quot;"; break;
		case '\'': out << "&#39;"; break;
		default:   out.Cat(c);
		}
	}
	return out;
}

String Initials(const String& display_name)
{
	String s = TrimBoth(display_name);
	if(s.IsEmpty()) return "?";
	Vector<String> parts = Split(s, ' ');
	String out;
	for(int i = 0; i < parts.GetCount() && out.GetCount() < 2; i++) {
		String p = TrimBoth(parts[i]);
		if(!p.IsEmpty())
			out.Cat(ToUpper(p[0]));
	}
	return out.IsEmpty() ? String("?") : out;
}

}

String WebAuthViews::GetStyles()
{
	return
		"  .wa-input { width:100%; box-sizing:border-box; background:var(--wf-bg-alt,#1c202f); "
		"border:1px solid var(--wf-border-color,rgba(255,255,255,0.12)); border-radius:6px; "
		"padding:8px 10px; color:var(--wf-fg-normal,#d3dae3); font-size:0.9rem; }\n"
		"  .wa-input:focus { outline:none; border-color:var(--wf-accent-focus,#00a1c5); }\n"
		"  .wa-login-card { max-width:420px; margin:3rem auto; }\n"
		"  .wa-user-menu { display:flex; align-items:center; gap:10px; }\n"
		"  .wa-user-name { font-size:0.85rem; color:var(--wf-fg-normal,#d3dae3); }\n"
		"  .wa-sidebar-account { position:relative; }\n"
		"  .wa-sidebar-account-trigger { display:flex; align-items:center; gap:10px; width:100%; "
		"background:transparent; border:none; border-radius:8px; padding:8px; cursor:pointer; "
		"color:var(--wf-fg-normal,#d3dae3); text-align:left; }\n"
		"  .wa-sidebar-account-trigger:hover { background:var(--wf-surface-hover,rgba(0,161,197,0.12)); }\n"
		"  .wa-avatar { flex-shrink:0; width:28px; height:28px; border-radius:50%; background:var(--wf-accent-focus,#00a1c5); "
		"color:#fff; font-size:0.75rem; font-weight:700; display:flex; align-items:center; justify-content:center; }\n"
		"  .wa-sidebar-account-name { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; font-size:0.85rem; font-weight:600; }\n"
		"  .wa-account-popup { display:none; position:absolute; bottom:100%; left:0; right:0; margin-bottom:6px; "
		"background:var(--wf-surface,#1e283b); border:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); "
		"border-radius:8px; box-shadow:var(--wf-shadow,0 4px 12px rgba(0,0,0,0.3)); overflow:hidden; z-index:200; }\n"
		"  .wa-account-popup.wa-open { display:block; }\n"
		"  .wa-account-popup-item { display:block; width:100%; padding:9px 12px; font-size:0.85rem; "
		"color:var(--wf-fg-normal,#d3dae3); text-decoration:none; text-align:left; background:transparent; border:none; cursor:pointer; font-family:inherit; }\n"
		"  .wa-account-popup-item:hover { background:var(--wf-surface-hover,rgba(0,161,197,0.12)); }\n"
		"  .wa-facts { display:flex; flex-direction:column; gap:10px; margin:0; }\n"
		"  .wa-fact-row { display:flex; justify-content:space-between; gap:16px; padding-bottom:8px; "
		"border-bottom:1px solid var(--wf-border-color,rgba(255,255,255,0.06)); font-size:0.88rem; }\n"
		"  .wa-fact-row dt { color:var(--wf-fg-inactive,#8b919d); }\n"
		"  .wa-fact-row dd { margin:0; color:var(--wf-fg-normal,#d3dae3); font-weight:600; }\n";
}

String WebAuthViews::RenderLoginForm(Http& http, const String& redirect_url, const String& error_msg,
                                      const String& brand_label, const String& brand_note)
{
	String h;
	h << "<div class=\"wa-login-card wf-card\">\n"
	  << "  <div class=\"wf-card-header\"><span>" << Esc(brand_label) << "</span></div>\n"
	  << "  <div class=\"wf-card-body\">\n";
	if(!brand_note.IsEmpty())
		h << "    <p style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); margin-bottom:1rem;\">" << Esc(brand_note) << "</p>\n";
	if(!error_msg.IsEmpty())
		h << "    <div class=\"wf-alert wf-alert-danger\" style=\"margin-bottom:1rem;\">" << Esc(error_msg) << "</div>\n";
	h << "    <form method=\"POST\" action=\"/login/submit\" accept-charset=\"utf-8\">\n"
	  << "      <input type=\"hidden\" name=\"__post_identity__\" value=\"" << WebAuthService::MintCsrfIdentity(http) << "\">\n"
	  << "      <input type=\"hidden\" name=\"redirect\" value=\"" << Esc(redirect_url) << "\">\n"
	  << "      <div style=\"margin-bottom:1rem;\">\n"
	  << "        <label style=\"display:block; font-size:0.82rem; font-weight:600; margin-bottom:4px;\">Username</label>\n"
	  << "        <input type=\"text\" name=\"username\" required autofocus autocomplete=\"username\" class=\"wa-input\">\n"
	  << "      </div>\n"
	  << "      <div style=\"margin-bottom:1.25rem;\">\n"
	  << "        <label style=\"display:block; font-size:0.82rem; font-weight:600; margin-bottom:4px;\">Password</label>\n"
	  << "        <input type=\"password\" name=\"password\" required autocomplete=\"current-password\" class=\"wa-input\">\n"
	  << "      </div>\n"
	  << "      <button type=\"submit\" class=\"wf-btn wf-btn-primary\" style=\"width:100%; padding:9px;\">Sign In</button>\n"
	  << "    </form>\n"
	  << "  </div>\n"
	  << "</div>\n";
	return h;
}

String WebAuthViews::RenderUserMenu(Http& http, const WebAuthIdentity& id, const String& logout_url)
{
	String h;
	h << "<div class=\"wa-user-menu\">\n"
	  << "  <span class=\"wa-user-name\">" << Esc(id.display_name) << "</span>\n"
	  << "  <form method=\"POST\" action=\"" << Esc(logout_url) << "\" style=\"display:inline;\">\n"
	  << "    <input type=\"hidden\" name=\"__post_identity__\" value=\"" << WebAuthService::MintCsrfIdentity(http) << "\">\n"
	  << "    <button type=\"submit\" class=\"wf-btn wf-btn-secondary\">Sign out</button>\n"
	  << "  </form>\n"
	  << "</div>\n";
	return h;
}

String WebAuthViews::RenderSidebarAccountFooter(Http& http, const WebAuthIdentity& id, const String& settings_url, const String& logout_url)
{
	String menu_id = "wa-account-menu";
	String h;
	h << "<div class=\"wa-sidebar-account\">\n"
	  << "  <button type=\"button\" class=\"wa-sidebar-account-trigger\" aria-haspopup=\"true\" "
	  << "onclick=\"document.getElementById('" << menu_id << "').classList.toggle('wa-open')\" "
	  << "aria-label=\"Account: " << Esc(id.display_name) << "\">\n"
	  << "    <span class=\"wa-avatar\">" << Esc(Initials(id.display_name)) << "</span>\n"
	  << "    <span class=\"wa-sidebar-account-name wf-sidebar-label\">" << Esc(id.display_name) << "</span>\n"
	  << "  </button>\n"
	  << "  <div class=\"wa-account-popup\" id=\"" << menu_id << "\">\n"
	  << "    <a href=\"" << Esc(settings_url) << "\" class=\"wa-account-popup-item\">Settings</a>\n"
	  << "    <form method=\"POST\" action=\"" << Esc(logout_url) << "\">\n"
	  << "      <input type=\"hidden\" name=\"__post_identity__\" value=\"" << WebAuthService::MintCsrfIdentity(http) << "\">\n"
	  << "      <button type=\"submit\" class=\"wa-account-popup-item\">Sign out</button>\n"
	  << "    </form>\n"
	  << "  </div>\n"
	  << "</div>\n";
	return h;
}

String WebAuthViews::RenderSettingsPage(const WebAuthIdentity& id, const Vector<SettingsField>& identity_facts,
                                         const String& password_change_action_url, Http& http,
                                         const String& error_msg, const String& notice_msg)
{
	String h;
	h << "<h1>Settings</h1>\n";
	if(!notice_msg.IsEmpty())
		h << "<div class=\"wf-alert wf-alert-success\">" << Esc(notice_msg) << "</div>\n";
	if(!error_msg.IsEmpty())
		h << "<div class=\"wf-alert wf-alert-danger\">" << Esc(error_msg) << "</div>\n";

	h << "<div class=\"wf-card\">\n"
	  << "  <div class=\"wf-card-header\"><span>Account</span></div>\n"
	  << "  <div class=\"wf-card-body\">\n"
	  << "    <dl class=\"wa-facts\">\n";
	for(int i = 0; i < identity_facts.GetCount(); i++) {
		h << "      <div class=\"wa-fact-row\"><dt>" << Esc(identity_facts[i].label) << "</dt>"
		  << "<dd>" << Esc(identity_facts[i].value) << "</dd></div>\n";
	}
	h << "    </dl>\n"
	  << "  </div>\n"
	  << "</div>\n";

	h << "<div class=\"wf-card\">\n"
	  << "  <div class=\"wf-card-header\"><span>Security</span></div>\n"
	  << "  <div class=\"wf-card-body\">\n"
	  << "    <form method=\"POST\" action=\"" << Esc(password_change_action_url) << "\" accept-charset=\"utf-8\">\n"
	  << "      <input type=\"hidden\" name=\"__post_identity__\" value=\"" << WebAuthService::MintCsrfIdentity(http) << "\">\n"
	  << "      <div style=\"margin-bottom:1rem;\">\n"
	  << "        <label style=\"display:block; font-size:0.82rem; font-weight:600; margin-bottom:4px;\">Current password</label>\n"
	  << "        <input type=\"password\" name=\"current_password\" required autocomplete=\"current-password\" class=\"wa-input\">\n"
	  << "      </div>\n"
	  << "      <div style=\"margin-bottom:1rem;\">\n"
	  << "        <label style=\"display:block; font-size:0.82rem; font-weight:600; margin-bottom:4px;\">New password</label>\n"
	  << "        <input type=\"password\" name=\"new_password\" required autocomplete=\"new-password\" class=\"wa-input\">\n"
	  << "      </div>\n"
	  << "      <button type=\"submit\" class=\"wf-btn wf-btn-primary\">Change password</button>\n"
	  << "    </form>\n"
	  << "  </div>\n"
	  << "</div>\n";
	return h;
}

END_UPP_NAMESPACE
