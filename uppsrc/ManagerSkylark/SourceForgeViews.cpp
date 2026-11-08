#include "ManagerSkylark.h"
#include "SourceForgePlatform.h"

NAMESPACE_UPP

// Explicit unavailable state: what failed, the provenance, and the adapter's diagnostic.
static String RenderUnavailable(const ForgeResult& r, const String& what)
{
	return "<div class=\"wf-card\" style=\"margin-bottom:1rem;\"><div class=\"wf-card-body\">"
	       "<span class=\"wf-badge wf-badge-neutral\">Unavailable</span> " + DeHtml(what) +
	       " could not be loaded from SourceForge: " + DeHtml(r.error) + "</div></div>\n";
}

// ---------------------------------------------------------------------------
// SKYLARK Handlers for SourceForge Parity
// ---------------------------------------------------------------------------

void RenderSourceForgeView(Http& http, const String& project, const String& subpath)
{
	String p = project.IsEmpty() ? "topside" : project;
	SourceForgeProjectInfo info = SourceForgePlatformService::GetProject(p);

	String path = TrimBoth(subpath);
	String tab = http["tab"];

	Vector<String> parts = Split(path, '/');
	if(parts.GetCount() > 0)
		tab = parts[0];

	if(tab.IsEmpty()) tab = "summary";

	int ticket_num = 0;
	if(tab == "tickets" && parts.GetCount() > 1 && IsDigit(parts[1][0])) {
		ticket_num = ScanInt(parts[1]);
	}

	String wiki_slug = "Home";
	if(tab == "wiki" && parts.GetCount() > 1) {
		wiki_slug = parts[1];
	}

	int topic_id = 0;
	String forum_name = "";
	if(tab == "discussion") {
		if(parts.GetCount() >= 3 && parts[1] == "topic" && IsDigit(parts[2][0])) {
			topic_id = ScanInt(parts[2]);
		}
		else if(parts.GetCount() >= 2) {
			forum_name = parts[1];
		}
	}

	String content;
	String sidebar = SourceForgePlatformService::RenderSidebar(info);

	// 1. Summary View
	if(tab == "summary") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Description</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p style=\"font-size:1.05rem; line-height:1.6;\">" << info.description << "</p>\n"
			<< "    <p>Topside combines high-performance C++ backend architectures with autonomous agent loops, comprehensive browser testing via Aria, and enterprise project tracking via Manager.</p>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Key Features</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <ul style=\"margin:0; padding-left:1.2rem; line-height:1.8;\">\n";
		for(int i = 0; i < info.features.GetCount(); i++) {
			content << "      <li>" << info.features[i] << "</li>\n";
		}
		content
			<< "    </ul>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>User Reviews</span><a href=\"/projects/" << info.name << "/reviews/\" class=\"wf-btn wf-btn-secondary\" style=\"padding:2px 8px; font-size:0.75rem;\">Write a Review</a></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"display:flex; gap:12px; align-items:flex-start;\">\n"
			<< "      <div style=\"width:36px; height:36px; border-radius:50%; background:#ee6524; color:#fff; display:flex; align-items:center; justify-content:center; font-weight:700;\">S</div>\n"
			<< "      <div>\n"
			<< "        <div style=\"font-weight:600;\">sppp <span style=\"color:#f59f00;\">&#9733;&#9733;&#9733;&#9733;&#9733;</span></div>\n"
			<< "        <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8c929a); margin-bottom:6px;\">Posted today</div>\n"
			<< "        <p style=\"margin:0; font-size:0.9rem;\">Topside provides unmatched autonomy for AI-assisted engineering with U++. Zero latency, native performance, and full platform parity.</p>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 2. Files & Download Packages View
	else if(tab == "files") {
		ForgeResult fr;
		Vector<SourceForgeReleaseFile> files = SourceForgePlatformService::GetFiles(info.name, &fr);
		if(!fr.ok)
			content << RenderUnavailable(fr, "Release files");
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Files &amp; Download Packages</span><span class=\"wf-badge wf-badge-neutral\">Stable Releases</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>Package File</th><th>Release</th><th>Date</th><th>Size</th><th>Downloads</th><th>SHA-256 Hash</th><th>Action</th></tr></thead>\n"
			<< "      <tbody>\n";
		for(int i = 0; i < files.GetCount(); i++) {
			const SourceForgeReleaseFile& f = files[i];
			content
				<< "        <tr>\n"
				<< "          <td>" << VividIcon::Get("package", 16) << " <strong>" << f.name << "</strong></td>\n"
				<< "          <td><span class=\"wf-badge wf-badge-emerald\">" << f.version << "</span></td>\n"
				<< "          <td>" << f.date << "</td>\n"
				<< "          <td>" << f.size_str << "</td>\n"
				<< "          <td><strong>" << f.downloads << "</strong></td>\n"
				<< "          <td><code style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8c929a);\" title=\"" << f.sha256 << "\">" << f.sha256.Left(12) << "...</code></td>\n"
				<< "          <td><a href=\"" << f.url << "\" class=\"wf-btn wf-btn-primary\" style=\"padding:3px 10px; font-size:0.75rem; background:#ee6524; border-color:#ee6524; display:inline-flex; align-items:center; gap:4px;\">" << VividIcon::Get("download", 13) << " Download</a></td>\n"
				<< "        </tr>\n";
		}
		content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
	}
	// 3. Tickets Tracker View (List or Conversation View)
	else if(tab == "tickets" && ticket_num > 0) {
		ForgeResult tr;
		SourceForgeTicket t = SourceForgePlatformService::GetTicket(info.name, ticket_num, &tr);
		if(!tr.ok)
			content << RenderUnavailable(tr, "Ticket #" + AsString(ticket_num))
			        << "<a href=\"/p/" << info.name << "/tickets/\" class=\"wf-btn wf-btn-secondary\">Back to Tickets</a>\n";
		else {
		bool is_open = (t.status == "open" || t.status == "in-progress");

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"/p/" << info.name << "/tickets/\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to Tickets</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< DeHtml(t.summary) << " <span style=\"color:var(--wf-fg-inactive,#7d8590); font-weight:400;\">#" << t.num << "</span></h1>\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px; font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); flex-wrap:wrap;\">\n"
			<< "    <span class=\"wf-badge\" style=\"background:" << (is_open ? "#238636" : "#8957e5") << "; color:#ffffff; font-weight:700; padding:4px 10px; border-radius:12px; font-size:0.82rem; display:inline-flex; align-items:center; gap:4px;\">"
			<< VividIcon::Get(is_open ? "alert-circle" : "check", 14) << " " << (is_open ? "Open" : "Closed") << "</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">Priority: " << CapitalizeWord(t.priority) << "</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">Milestone: " << t.milestone << "</span>\n"
			<< "    <span>Created by <strong>" << t.author << "</strong> on " << t.created_at << "</span>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Ticket Description Card
		content
			<< "<div class=\"wf-card\" style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div class=\"wf-card-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << t.author << "</strong> <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.82rem;\">opened this ticket on " << t.created_at << "</span></div>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">Author</span>\n"
			<< "  </div>\n"
			<< "  <div class=\"wf-card-body\" style=\"font-size:0.92rem; line-height:1.6;\">\n"
			<< (!t.description.IsEmpty() ? DeHtml(t.description) : "<em>No description provided.</em>") << "\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Comments Timeline
		for(int i = 0; i < t.comments.GetCount(); i++) {
			const ValueMap& c = t.comments[i];
			String c_author = c["author"];
			String c_text = c["text"];
			String c_created = c["created_at"];

			content
				<< "<div class=\"wf-card\" style=\"margin-bottom:1.25rem;\">\n"
				<< "  <div class=\"wf-card-header\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << c_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.82rem;\">commented on " << c_created << "</span></div>\n"
				<< "  </div>\n"
				<< "  <div class=\"wf-card-body\" style=\"font-size:0.92rem; line-height:1.6;\">\n"
				<< DeHtml(c_text) << "\n"
				<< "  </div>\n"
				<< "</div>\n";
		}

		// Comment & Triage Controls
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Add a Comment &amp; Update Status</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <textarea id=\"sf-comment-text\" placeholder=\"Write your comment or update...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:6px; padding:10px; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <div style=\"display:flex; gap:10px; align-items:center;\">\n"
			<< "      <button type=\"button\" id=\"btn-sf-submit-comment\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; padding:6px 14px; font-weight:600;\" onclick=\"SourceForgeUI.submitComment('" << info.name << "', " << t.num << ")\">Add Comment</button>\n"
			<< "      <button type=\"button\" id=\"btn-sf-toggle-ticket\" class=\"wf-btn wf-btn-secondary\" style=\"padding:6px 14px;\" onclick=\"SourceForgeUI.toggleTicket('" << info.name << "', " << t.num << ")\">"
			<< (is_open ? "Close Ticket" : "Reopen Ticket") << "</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
		}
	}
	else if(tab == "tickets") {
		ForgeResult tlr;
		Vector<SourceForgeTicket> tickets = SourceForgePlatformService::GetTickets(info.name, &tlr);

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">Tickets &amp; Bug Tracker</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">" << tickets.GetCount() << " Tickets</span>\n"
			<< "  </div>\n"
			<< "  <button type=\"button\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; display:inline-flex; align-items:center; gap:6px; font-weight:600;\" onclick=\"SourceForgeUI.toggleNewTicketCard()\">"
			<< VividIcon::Get("plus", 14) << " Create Ticket</button>\n"
			<< "</div>\n"
			<< "<p style=\"font-size:0.82rem; color:var(--wf-fg-inactive,#7d8590); margin:0 0 1rem 0;\">SourceForge writes need configured API credentials. Without them, create, comment and status actions report unavailable.</p>\n";
		if(!tlr.ok)
			content << RenderUnavailable(tlr, "Tickets");

		// New Ticket Creation Card (Collapsible)
		content
			<< "<div id=\"sf-new-ticket-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid #ee6524;\">\n"
			<< "  <div class=\"wf-card-header\"><span>Create New Ticket</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:12px;\">\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Summary</label>\n"
			<< "      <input type=\"text\" id=\"sf-ticket-summary\" placeholder=\"Brief summary of the issue or feature...\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "    </div>\n"
			<< "    <div style=\"display:grid; grid-template-columns:1fr 1fr; gap:12px;\">\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Priority</label>\n"
			<< "        <select id=\"sf-ticket-priority\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "          <option value=\"low\">Low</option>\n"
			<< "          <option value=\"normal\" selected>Normal</option>\n"
			<< "          <option value=\"high\">High</option>\n"
			<< "          <option value=\"urgent\">Urgent</option>\n"
			<< "        </select>\n"
			<< "      </div>\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Milestone</label>\n"
			<< "        <input type=\"text\" id=\"sf-ticket-milestone\" value=\"Phase 13\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Description</label>\n"
			<< "      <textarea id=\"sf-ticket-desc\" placeholder=\"Detailed description, reproduction steps, or requirements...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:8px 10px; font-family:inherit;\"></textarea>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; gap:10px;\">\n"
			<< "      <button type=\"button\" id=\"btn-sf-create-ticket\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\" onclick=\"SourceForgeUI.createTicket('" << info.name << "')\">Submit Ticket</button>\n"
			<< "      <button type=\"button\" class=\"wf-btn wf-btn-secondary\" onclick=\"SourceForgeUI.toggleNewTicketCard()\">Cancel</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Tickets List Table
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>#</th><th>Summary</th><th>Status</th><th>Priority</th><th>Milestone</th><th>Author</th><th>Comments</th></tr></thead>\n"
			<< "      <tbody>\n";
		for(int i = 0; i < tickets.GetCount(); i++) {
			const SourceForgeTicket& t = tickets[i];
			bool is_op = (t.status == "open" || t.status == "in-progress");
			content
				<< "        <tr>\n"
				<< "          <td>#" << t.num << "</td>\n"
				<< "          <td><a href=\"/p/" << info.name << "/tickets/" << t.num << "\" style=\"font-weight:600; color:#ee6524; text-decoration:none;\">" << DeHtml(t.summary) << "</a></td>\n"
				<< "          <td><span class=\"wf-badge\" style=\"background:" << (is_op ? "#238636" : "#8957e5") << "; color:#fff;\">" << CapitalizeWord(t.status) << "</span></td>\n"
				<< "          <td><span class=\"wf-badge wf-badge-" << (t.priority == "high" || t.priority == "urgent" ? "rose" : "amber") << "\">" << CapitalizeWord(t.priority) << "</span></td>\n"
				<< "          <td>" << t.milestone << "</td>\n"
				<< "          <td>" << t.author << "</td>\n"
				<< "          <td>" << t.comments.GetCount() << "</td>\n"
				<< "        </tr>\n";
		}
		content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
	}
	// 4. Wiki CMS View (View or Edit Mode)
	else if(tab == "wiki") {
		ForgeResult wr, wlr;
		SourceForgeWikiPage page = SourceForgePlatformService::GetWikiPage(info.name, wiki_slug, &wr);
		Vector<SourceForgeWikiPage> pages = SourceForgePlatformService::GetWikiPages(info.name, &wlr);
		bool edit_mode = (http["edit"] == "1");
		if(!wr.ok || !wlr.ok)
			content << RenderUnavailable(wr.ok ? wlr : wr, "Wiki");

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">Topside Wiki &middot; " << page.title << "</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">v" << page.version << "</span>\n"
			<< "  </div>\n"
			<< "  <div style=\"display:flex; gap:8px;\">\n";
		if(!edit_mode) {
			content << "    <a href=\"/p/" << info.name << "/wiki/" << page.slug << "/?edit=1\" id=\"btn-sf-edit-wiki\" class=\"wf-btn wf-btn-secondary\" style=\"display:inline-flex; align-items:center; gap:6px;\">" << VividIcon::Get("edit", 14) << " Edit Page</a>\n";
		} else {
			content << "    <a href=\"/p/" << info.name << "/wiki/" << page.slug << "/\" class=\"wf-btn wf-btn-secondary\">Cancel Editing</a>\n";
		}
		content << "  </div>\n</div>\n";

		// Sub-navigation bar of wiki pages
		content
			<< "<div style=\"display:flex; gap:8px; margin-bottom:1.25rem; flex-wrap:wrap;\">\n";
		for(int i = 0; i < pages.GetCount(); i++) {
			bool is_act_p = (ToLower(pages[i].slug) == ToLower(page.slug));
			content << "  <a href=\"/p/" << info.name << "/wiki/" << pages[i].slug << "/\" class=\"wf-btn " << (is_act_p ? "wf-btn-primary" : "wf-btn-secondary") << "\" style=\"font-size:0.8rem; " << (is_act_p ? "background:#ee6524; border-color:#ee6524;" : "") << "\">" << pages[i].title << "</a>\n";
		}
		content << "</div>\n";

		if(edit_mode) {
			content
				<< "<div class=\"wf-card\">\n"
				<< "  <div class=\"wf-card-header\"><span>Editing Wiki Page: " << page.title << "</span></div>\n"
				<< "  <div class=\"wf-card-body\">\n"
				<< "    <input type=\"text\" id=\"sf-wiki-title\" value=\"" << page.title << "\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px; margin-bottom:12px; font-weight:700;\">\n"
				<< "    <textarea id=\"sf-wiki-content\" style=\"width:100%; height:260px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:6px; padding:10px; font-family:monospace; line-height:1.5; margin-bottom:12px;\">" << DeHtml(page.content) << "</textarea>\n"
				<< "    <div style=\"display:flex; justify-content:space-between; align-items:center;\">\n"
				<< "      <button type=\"button\" id=\"btn-sf-save-wiki\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; padding:6px 14px; font-weight:600;\" onclick=\"SourceForgeUI.saveWikiPage('" << info.name << "', '" << page.slug << "')\">Save Changes</button>\n"
				<< "      <a href=\"/p/" << info.name << "/wiki/" << page.slug << "/\" class=\"wf-btn wf-btn-secondary\">Cancel</a>\n"
				<< "    </div>\n"
				<< "  </div>\n"
				<< "</div>\n";
		} else {
			content
				<< "<div class=\"wf-card markdown-render-box\">\n"
				<< "  <textarea class=\"raw-markdown\" style=\"display:none;\">" << DeHtml(page.content) << "</textarea>\n"
				<< "  <div class=\"wf-card-body markdown-body\" id=\"sf-wiki-render\" style=\"padding:1.5rem; line-height:1.7; font-size:0.95rem;\">" << DeHtml(page.content) << "</div>\n"
				<< "</div>\n";
		}
	}
	// 5. Discussion Forums View (Topic Thread or Forum List)
	else if(tab == "discussion" && topic_id > 0) {
		ForgeResult dr;
		SourceForgeDiscussionTopic topic = SourceForgePlatformService::GetDiscussionTopic(info.name, topic_id, &dr);
		if(!dr.ok)
			content << RenderUnavailable(dr, "Discussion topic");

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"/p/" << info.name << "/discussion/\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to Forums</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< DeHtml(topic.title) << "</h1>\n"
			<< "  <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590);\">\n"
			<< "    Posted in <strong>" << CapitalizeWord(topic.forum) << "</strong> by <strong>" << topic.author << "</strong> on " << topic.created_at << "\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Posts Timeline
		for(int i = 0; i < topic.posts.GetCount(); i++) {
			const ValueMap& p_post = topic.posts[i];
			String p_author = p_post["author"];
			String p_text = p_post["text"];
			String p_created = p_post["created_at"];

			content
				<< "<div class=\"wf-card\" style=\"margin-bottom:1.25rem;\">\n"
				<< "  <div class=\"wf-card-header\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << p_author << "</strong> <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.82rem;\">" << p_created << "</span></div>\n"
				<< "  </div>\n"
				<< "  <div class=\"wf-card-body\" style=\"font-size:0.92rem; line-height:1.6;\">\n"
				<< DeHtml(p_text) << "\n"
				<< "  </div>\n"
				<< "</div>\n";
		}

		// Post Reply Form
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Post a Reply</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <textarea id=\"sf-post-text\" placeholder=\"Write your reply to this discussion...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:6px; padding:10px; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <button type=\"button\" id=\"btn-sf-submit-post\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; padding:6px 14px; font-weight:600;\" onclick=\"SourceForgeUI.submitPost('" << info.name << "', " << topic.id << ")\">Post Reply</button>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(tab == "discussion") {
		ForgeResult dlr;
		Vector<SourceForgeDiscussionTopic> topics = SourceForgePlatformService::GetDiscussionTopics(info.name, forum_name, &dlr);
		if(!dlr.ok)
			content << RenderUnavailable(dlr, "Discussion forums");

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">Forums &amp; Discussion</span>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\">" << topics.GetCount() << " Topics</span>\n"
			<< "  </div>\n"
			<< "  <button type=\"button\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; display:inline-flex; align-items:center; gap:6px; font-weight:600;\" onclick=\"SourceForgeUI.toggleNewTopicCard()\">"
			<< VividIcon::Get("plus", 14) << " Create Topic</button>\n"
			<< "</div>\n";

		// New Topic Creation Card (Collapsible)
		content
			<< "<div id=\"sf-new-topic-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid #ee6524;\">\n"
			<< "  <div class=\"wf-card-header\"><span>Create New Topic</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:12px;\">\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Forum</label>\n"
			<< "      <select id=\"sf-topic-forum\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "        <option value=\"general\">General Discussion</option>\n"
			<< "        <option value=\"development\">Development &amp; Architecture</option>\n"
			<< "      </select>\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Topic Title</label>\n"
			<< "      <input type=\"text\" id=\"sf-topic-title\" placeholder=\"Subject or discussion title...\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.82rem; font-weight:600; display:block; margin-bottom:4px;\">Message</label>\n"
			<< "      <textarea id=\"sf-topic-first-post\" placeholder=\"Write your topic message...\" style=\"width:100%; height:90px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:8px 10px; font-family:inherit;\"></textarea>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; gap:10px;\">\n"
			<< "      <button type=\"button\" id=\"btn-sf-create-topic\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\" onclick=\"SourceForgeUI.createTopic('" << info.name << "')\">Create Topic</button>\n"
			<< "      <button type=\"button\" class=\"wf-btn wf-btn-secondary\" onclick=\"SourceForgeUI.toggleNewTopicCard()\">Cancel</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Topics List Table
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>Topic Title</th><th>Forum</th><th>Author</th><th>Posts</th><th>Date</th></tr></thead>\n"
			<< "      <tbody>\n";
		for(int i = 0; i < topics.GetCount(); i++) {
			const SourceForgeDiscussionTopic& top = topics[i];
			content
				<< "        <tr>\n"
				<< "          <td><a href=\"/p/" << info.name << "/discussion/topic/" << top.id << "\" style=\"font-weight:600; color:#ee6524; text-decoration:none;\">" << DeHtml(top.title) << "</a></td>\n"
				<< "          <td><span class=\"wf-badge wf-badge-neutral\">" << CapitalizeWord(top.forum) << "</span></td>\n"
				<< "          <td>" << top.author << "</td>\n"
				<< "          <td><strong>" << top.posts.GetCount() << "</strong></td>\n"
				<< "          <td>" << top.created_at << "</td>\n"
				<< "        </tr>\n";
		}
		content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
	}
	// 6. Reviews View
	else if(tab == "reviews") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Community Reviews &middot; 5.0 out of 5 stars</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:20px; margin-bottom:20px; padding-bottom:16px; border-bottom:1px solid var(--wf-border-color,#393e46);\">\n"
			<< "      <div style=\"font-size:2.8rem; font-weight:700; color:#ee6524;\">5.0</div>\n"
			<< "      <div>\n"
			<< "        <div style=\"color:#f59f00; font-size:1.2rem;\">&#9733;&#9733;&#9733;&#9733;&#9733;</div>\n"
			<< "        <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8c929a);\">100% of reviewers recommend this project</div>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "    <div style=\"margin-bottom:20px;\">\n"
			<< "      <h4 style=\"margin-top:0; margin-bottom:10px;\">Write a Review</h4>\n"
			<< "      <div style=\"display:flex; gap:6px; margin-bottom:10px; color:#f59f00; font-size:1.4rem; cursor:pointer;\">&#9733;&#9733;&#9733;&#9733;&#9733;</div>\n"
			<< "      <textarea placeholder=\"Share your experience with Topside...\" style=\"width:100%; height:80px; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:8px; font-family:inherit; margin-bottom:8px;\"></textarea>\n"
			<< "      <button class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\">Submit Review</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 7. Support View
	else if(tab == "support") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Topside Support &amp; Community Resources</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p>Get assistance with installation, tool integration, and autonomous agents.</p>\n"
			<< "    <div class=\"wf-grid wf-grid-2\" style=\"gap:16px; margin-top:16px;\">\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("message-square", 16) << " Community Forum</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Ask questions, share workflows, and discuss features with the maintainers.<br><br><a href=\"/p/" << info.name << "/discussion/\" class=\"wf-btn wf-btn-secondary\">Visit Discussion</a></div>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("bug", 16) << " Issue Tracker</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Submit bug reports, feature requests, and follow development milestones.<br><br><a href=\"/p/" << info.name << "/tickets/\" class=\"wf-btn wf-btn-secondary\">View Tickets</a></div>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("mail", 16) << " Mailing Lists</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Subscribe to <code>topside-discuss</code> for email updates and discussions.<br><br><a href=\"/p/" << info.name << "/mailman/\" class=\"wf-btn wf-btn-secondary\">Mailing Lists</a></div>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card\" style=\"margin-bottom:0;\">\n"
			<< "        <div class=\"wf-card-header\"><span>" << VividIcon::Get("robot", 16) << " SweAgent Pair</span></div>\n"
			<< "        <div class=\"wf-card-body\" style=\"font-size:0.85rem;\">Interact directly with the integrated autonomous agent for instant support.<br><br><a href=\"/p/" << info.name << "/agents/\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524;\">Launch SweAgent</a></div>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 8. Blog View
	else if(tab == "blog") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Topside Devlog &middot; Recent Posts</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <article style=\"margin-bottom:24px; padding-bottom:16px; border-bottom:1px solid var(--wf-border-color,#393e46);\">\n"
			<< "      <h3 style=\"margin-top:0; margin-bottom:6px;\"><a href=\"#\" style=\"color:#ee6524; text-decoration:none;\">Topside Architectural Restructuring &amp; Agentic Independence</a></h3>\n"
			<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8c929a); margin-bottom:12px;\">By <strong>sppp</strong> &middot; Today &middot; Tags: <em>Architecture, Release</em></div>\n"
			<< "      <p style=\"line-height:1.6;\">Topside will become the flagship autonomous software engineering environment. We are establishing modular parity across SourceForge and GitHub platforms to empower open-source teams.</p>\n"
			<< "    </article>\n"
			<< "    <article>\n"
			<< "      <h3 style=\"margin-top:0; margin-bottom:6px;\"><a href=\"#\" style=\"color:#ee6524; text-decoration:none;\">SourceForge Platform Parity Live with ManagerSkylark</a></h3>\n"
			<< "      <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#8c929a); margin-bottom:12px;\">By <strong>sppp</strong> &middot; Today &middot; Tags: <em>Skylark, Aria</em></div>\n"
			<< "      <p style=\"line-height:1.6;\">Complete SourceForge tool suite parity implemented: Code browser, Tickets tracker, Wiki, Discussion forums, Mailman mailing lists, Subprojects, and SweAgent pair programming integration.</p>\n"
			<< "    </article>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 9. Code Browser View
	else if(tab == "code") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:12px;\">\n"
			<< "      <span>" << VividIcon::Get("git-branch", 16) << " Branch: <strong>" << info.active_branch << "</strong></span>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\">Commit [eefabd]</span>\n"
			<< "    </div>\n"
			<< "    <a href=\"#\" class=\"wf-btn wf-btn-secondary\" style=\"padding:2px 8px; font-size:0.75rem;\">Clone: " << info.git_repo_url << "</a>\n"
			<< "  </div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <table class=\"wf-table\">\n"
			<< "      <thead><tr><th>File</th><th>Last Commit</th><th>Updated</th></tr></thead>\n"
			<< "      <tbody>\n"
			<< "        <tr>\n"
			<< "          <td>" << VividIcon::Get("file-text", 16) << " <a href=\"#\">README.md</a></td>\n"
			<< "          <td>Initial commit on " << info.active_branch << " branch</td>\n"
			<< "          <td>recent</td>\n"
			<< "        </tr>\n"
			<< "        <tr>\n"
			<< "          <td>" << VividIcon::Get("folder", 16) << " <a href=\"#\">uppsrc</a></td>\n"
			<< "          <td>Core framework packages and runtime</td>\n"
			<< "          <td>today</td>\n"
			<< "        </tr>\n"
			<< "        <tr>\n"
			<< "          <td>" << VividIcon::Get("folder", 16) << " <a href=\"#\">reference</a></td>\n"
			<< "          <td>ManagerSkylark and WebShowcase reference applications</td>\n"
			<< "          <td>today</td>\n"
			<< "        </tr>\n"
			<< "      </tbody>\n"
			<< "    </table>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\">" << VividIcon::Get("book", 16) << " README.md</div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <h2 style=\"margin-top:0;\">Topside</h2>\n"
			<< "    <p>Topside is an agentic software development and desktop platform powered by Ultimate++ and autonomous AI agents.</p>\n"
			<< "    <h3>Quick Start</h3>\n"
			<< "    <pre style=\"background:var(--wf-bg-view,#181a1d); padding:10px; border-radius:4px; font-family:monospace; border:1px solid var(--wf-border-color,#393e46);\">git clone " << info.git_repo_url << " topside\ncd topside\n./bin/build -m CLANG reference/ManagerSkylark</pre>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 10. Mailman Mailing Lists
	else if(tab == "mailman") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Topside Mailing Lists</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p>Subscribe to stay informed with developer discussions and release announcements.</p>\n"
			<< "    <div style=\"margin-top:16px;\">\n"
			<< "      <div style=\"padding:12px; background:var(--wf-bg-view,#181a1d); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; margin-bottom:12px; display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "        <div>\n"
			<< "          <div style=\"font-weight:700;\">topside-discuss</div>\n"
			<< "          <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8c929a);\">General discussions for developers and users</div>\n"
			<< "        </div>\n"
			<< "        <a href=\"#\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; font-size:0.75rem;\">Subscribe</a>\n"
			<< "      </div>\n"
			<< "      <div style=\"padding:12px; background:var(--wf-bg-view,#181a1d); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "        <div>\n"
			<< "          <div style=\"font-weight:700;\">topside-announce</div>\n"
			<< "          <div style=\"font-size:0.85rem; color:var(--wf-fg-inactive,#8c929a);\">Milestone and major release announcements only</div>\n"
			<< "        </div>\n"
			<< "        <a href=\"#\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.75rem;\">Subscribe</a>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 11. Subproject View
	else if(tab == "subproject") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Linked Subprojects &middot; subproject.topside.p</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <p>SourceForge subprojects allow hierarchical structuring of large systems into independent tool ecosystems.</p>\n"
			<< "    <div class=\"wf-card\" style=\"margin-top:16px; margin-bottom:0;\">\n"
			<< "      <div class=\"wf-card-header\">\n"
			<< "        <a href=\"/projects/subproject.topside.p/\" style=\"font-weight:700; color:#ee6524; text-decoration:none;\">\n"
			<< "          " << VividIcon::Get("layers", 16) << " SubProject (subproject.topside.p)\n"
			<< "        </a>\n"
			<< "        <span class=\"wf-badge wf-badge-neutral\">Child Project</span>\n"
			<< "      </div>\n"
			<< "      <div class=\"wf-card-body\" style=\"font-size:0.88rem;\">\n"
			<< "        <p>Brought to you by: <strong>sppp</strong>. Independent downloads, ticket trackers, and git branches configured.</p>\n"
			<< "        <a href=\"/projects/subproject.topside.p/\" class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; font-size:0.8rem;\">Open Subproject Page</a>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 12. Admin View
	else if(tab == "admin") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Project Administration &middot; Topside</span><span class=\"wf-badge wf-badge-emerald\">Admin Access: sppp</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <h4>Enabled Tools</h4>\n"
			<< "    <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(200px, 1fr)); gap:12px; margin-bottom:20px;\">\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Summary</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Files</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Reviews</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Support</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Blog</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Code (Git)</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Wiki</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Tickets</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Discussion</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> Mailing Lists</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> SubProjects</label>\n"
			<< "      <label style=\"display:flex; align-items:center; gap:8px;\"><input type=\"checkbox\" checked disabled> SweAgent Pair</label>\n"
			<< "    </div>\n"
			<< "    <h4>Project Metadata</h4>\n"
			<< "    <div style=\"display:flex; flex-direction:column; gap:10px;\">\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Project Name</label>\n"
			<< "        <input type=\"text\" value=\"" << info.name << "\" readonly style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "      </div>\n"
			<< "      <div>\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Short Description</label>\n"
			<< "        <input type=\"text\" value=\"" << info.description << "\" style=\"width:100%; background:var(--wf-bg-view,#181a1d); color:var(--wf-fg-normal,#e6e8eb); border:1px solid var(--wf-border-color,#393e46); border-radius:4px; padding:6px 10px;\">\n"
			<< "      </div>\n"
			<< "      <button class=\"wf-btn wf-btn-primary\" style=\"background:#ee6524; border-color:#ee6524; align-self:flex-start; margin-top:8px;\">Save Changes</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// 13. SweAgent Pair View
	else if(tab == "agents") {
		sidebar = ""; // Expand to full width for pair programming terminal
		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1.25rem;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px;\">\n"
			<< "    <span style=\"font-size:1.3rem; font-weight:700;\">" << VividIcon::Get("robot", 20) << " SweAgent Pair Programmer</span>\n"
			<< "    <span class=\"wf-badge wf-badge-emerald\">Autonomous Pair</span>\n"
			<< "    <span style=\"color:var(--wf-fg-inactive,#8c929a); font-size:0.85rem;\">&middot; Context: <code>" << info.name << "</code></span>\n"
			<< "  </div>\n"
			<< "  <div style=\"display:flex; gap:8px;\">\n"
			<< "    <a href=\"/repo/SeppoPakonen/" << info.name << "?tab=agents\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("layers", 14) << " Autonomous Runs</a>\n"
			<< "    <a href=\"/chat\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; display:inline-flex; align-items:center; gap:6px;\" target=\"_blank\">"
			<< VividIcon::Get("external-link", 14) << " Fullscreen Terminal</a>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< WebChatView::RenderSnippet();
	}
	else {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>" << CapitalizeWord(tab) << " &middot; " << info.title << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div class=\"wf-alert wf-alert-info\">Displaying " << tab << " tool view for SourceForge project " << info.name << ". All data synchronized with Aria scraper.</div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}

	// Interactive JavaScript helpers for SourceForge UI
	content
		<< "<script>\n"
		<< "var SourceForgeUI = (function() {\n"
		<< "  function toggleNewTicketCard() {\n"
		<< "    let el = document.getElementById('sf-new-ticket-card');\n"
		<< "    if(el) el.style.display = (el.style.display === 'none' ? 'block' : 'none');\n"
		<< "  }\n"
		<< "  function createTicket(proj) {\n"
		<< "    let sum = document.getElementById('sf-ticket-summary').value;\n"
		<< "    let pri = document.getElementById('sf-ticket-priority').value;\n"
		<< "    let mil = document.getElementById('sf-ticket-milestone').value;\n"
		<< "    let desc = document.getElementById('sf-ticket-desc').value;\n"
		<< "    if(!sum) { alert('Summary is required'); return; }\n"
		<< "    fetch('/api/sf/tickets/' + proj + '/create', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({summary: sum, priority: pri, milestone: mil, description: desc, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.href = '/p/' + proj + '/tickets/' + res.num;\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function submitComment(proj, num) {\n"
		<< "    let txt = document.getElementById('sf-comment-text').value;\n"
		<< "    if(!txt) return;\n"
		<< "    fetch('/api/sf/tickets/' + proj + '/comment', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({num: num, comment: txt, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function toggleTicket(proj, num) {\n"
		<< "    fetch('/api/sf/tickets/' + proj + '/toggle', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({num: num})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function saveWikiPage(proj, slug) {\n"
		<< "    let title = document.getElementById('sf-wiki-title').value;\n"
		<< "    let content = document.getElementById('sf-wiki-content').value;\n"
		<< "    fetch('/api/sf/wiki/' + proj + '/save', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({slug: slug, title: title, content: content, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.href = '/p/' + proj + '/wiki/' + slug + '/';\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function toggleNewTopicCard() {\n"
		<< "    let el = document.getElementById('sf-new-topic-card');\n"
		<< "    if(el) el.style.display = (el.style.display === 'none' ? 'block' : 'none');\n"
		<< "  }\n"
		<< "  function createTopic(proj) {\n"
		<< "    let forum = document.getElementById('sf-topic-forum').value;\n"
		<< "    let title = document.getElementById('sf-topic-title').value;\n"
		<< "    let post = document.getElementById('sf-topic-first-post').value;\n"
		<< "    if(!title) { alert('Title is required'); return; }\n"
		<< "    fetch('/api/sf/discussion/' + proj + '/topic/create', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({forum: forum, title: title, first_post: post, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.href = '/p/' + proj + '/discussion/topic/' + res.id;\n"
		<< "    });\n"
		<< "  }\n"
		<< "  function submitPost(proj, topicId) {\n"
		<< "    let txt = document.getElementById('sf-post-text').value;\n"
		<< "    if(!txt) return;\n"
		<< "    fetch('/api/sf/discussion/' + proj + '/post/create', {\n"
		<< "      method: 'POST',\n"
		<< "      headers: {'Content-Type': 'application/json'},\n"
		<< "      body: JSON.stringify({topic_id: topicId, text: txt, author: 'sppp'})\n"
		<< "    }).then(r => r.json()).then(res => {\n"
		<< "      if(res.ok) window.location.reload();\n"
		<< "    });\n"
		<< "  }\n"
		<< "  return {\n"
		<< "    toggleNewTicketCard: toggleNewTicketCard,\n"
		<< "    createTicket: createTicket,\n"
		<< "    submitComment: submitComment,\n"
		<< "    toggleTicket: toggleTicket,\n"
		<< "    saveWikiPage: saveWikiPage,\n"
		<< "    toggleNewTopicCard: toggleNewTopicCard,\n"
		<< "    createTopic: createTopic,\n"
		<< "    submitPost: submitPost\n"
		<< "  };\n"
		<< "})();\n"
		<< "</script>\n";

	String top_nav = SourceForgePlatformService::RenderTopNav(info);
	String sub_nav = SourceForgePlatformService::RenderProjectHeader(info, tab);
	String breadcrumb = "<a href=\"/projects\">Home</a> &gt; <a href=\"/projects\">Open Source Software</a> &gt; <a href=\"#\">" + info.categories + "</a> &gt; <span>" + info.title + "</span>";

	PageBuilder pb;
	pb.Title(info.title + " download | SourceForge.net")
	  .Brand("SourceForge", "/projects/topside/", "package")
	  .Template("sourceforge")
	  .Theme("sourceforge-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .Header(top_nav)
	  .SubNav(sub_nav)
	  .Breadcrumb(breadcrumb)
	  .Sidebar(sidebar)
	  .Content(content);
	ManagerSkylarkApp::AddMarkdownSupport(pb);
	String html = pb.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

END_UPP_NAMESPACE
