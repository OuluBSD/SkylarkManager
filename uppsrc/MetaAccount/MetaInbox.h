#ifndef _MetaAccount_MetaInbox_h_
#define _MetaAccount_MetaInbox_h_

#include <Core/Core.h>
#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// One inbox event: an identity-scoped notification carrying ids and states
// only (never scopes, grant ids, or credentials).
struct InboxEvent : Moveable<InboxEvent> {
	String event_id;
	String subject_user;   // meta identity the event belongs to
	String source_site;    // "meta" | "taskfoundry"
	String type;           // allowlisted, dotted (see MetaInbox::ValidType)
	String actor;          // short label, e.g. "meta:mallory" or "operator"
	String message;        // one line, capped by MetaInbox::MessageLimit()
	String refs;           // optional deep link, e.g. "meta:/consent"
	Time   created_at;
	bool   read = false;

	InboxEvent() {}
	InboxEvent(const InboxEvent& o)
		: event_id(o.event_id), subject_user(o.subject_user), source_site(o.source_site)
		, type(o.type), actor(o.actor), message(o.message), refs(o.refs)
		, created_at(o.created_at), read(o.read) {}
	InboxEvent& operator=(const InboxEvent& o) {
		if(this != &o) {
			event_id = o.event_id;
			subject_user = o.subject_user;
			source_site = o.source_site;
			type = o.type;
			actor = o.actor;
			message = o.message;
			refs = o.refs;
			created_at = o.created_at;
			read = o.read;
		}
		return *this;
	}

	void Jsonize(JsonIO& io) {
		io("event_id", event_id)("subject_user", subject_user)("source_site", source_site)
		  ("type", type)("actor", actor)("message", message)("refs", refs)
		  ("created_at", created_at)("read", read);
	}
};

// The identity inbox of the meta site. The store is the meta site's own
// (engine/meta/inbox): written by the meta site itself (consent transitions)
// and by client sites only through the broker `notify` op, which is recorded
// in the diagnostics ledger. Pull-based: the subject lists and marks read.
class MetaInbox {
public:
	MetaInbox(const String& site_id, const String& data_root);

	StoreResult Push(const String& subject_user, const String& source_site, const String& type,
	                 const String& actor, const String& message, const String& refs);
	// Latest events for the subject, newest first.
	StoreResult List(const String& subject_user, Vector<InboxEvent>& out, int limit);
	StoreResult MarkAllRead(const String& subject_user);
	int         UnreadCount(const String& subject_user);

	static bool ValidType(const String& type);
	static int  MessageLimit()   { return 300; }
	static int  RetainPerSubject() { return 200; }

private:
	void PruneLocked(const String& subject_user);

	JsonFileStore& Store() { return *store; }

	One<JsonFileStore> store;
	Mutex mutex;
};

END_UPP_NAMESPACE

#endif
