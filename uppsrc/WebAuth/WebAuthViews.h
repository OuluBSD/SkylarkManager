#ifndef _WebAuth_WebAuthViews_h_
#define _WebAuth_WebAuthViews_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include "WebAuthModel.h"
#include "WebAuthService.h"

NAMESPACE_UPP

// HTML fragments rendered here use `wf-*` CSS class names by convention only
// (WebFramework visual compatibility without a compile dependency on it --
// WebAuth never includes a WebFramework header) plus a small `wa-*` set of
// its own, styled by GetStyles().
struct WebAuthViews {
	static String GetStyles();

	static String RenderLoginForm(Http& http, const String& redirect_url, const String& error_msg,
	                               const String& brand_label = "Sign in", const String& brand_note = "");

	// Compact widget for a top-bar account-actions slot. The logout form is
	// CSRF-protected via WebAuthService::MintCsrfIdentity, matching whatever
	// POST-identity convention the embedding app's dispatcher checks.
	static String RenderUserMenu(Http& http, const WebAuthIdentity& id, const String& logout_url = "/logout");

	// Pinned footer for a left-nav sidebar: avatar/initials + display name,
	// opening a popup with Settings / Sign out.
	static String RenderSidebarAccountFooter(Http& http, const WebAuthIdentity& id,
	                                          const String& settings_url = "/account/settings",
	                                          const String& logout_url = "/logout");

	struct SettingsField : Moveable<SettingsField> {
		String label, value;
		SettingsField() {}
		SettingsField(const String& l, const String& v) : label(l), value(v) {}
	};

	// Read-mostly identity-facts settings page, modeled on the reference
	// site's Profile page; password change is the one self-service action
	// WebAuth renders and wires itself. `identity_facts` is caller-supplied
	// so each app passes its own relevant fields.
	static String RenderSettingsPage(const WebAuthIdentity& id,
	                                  const Vector<SettingsField>& identity_facts,
	                                  const String& password_change_action_url,
	                                  Http& http,
	                                  const String& error_msg = "", const String& notice_msg = "");
};

END_UPP_NAMESPACE

#endif
