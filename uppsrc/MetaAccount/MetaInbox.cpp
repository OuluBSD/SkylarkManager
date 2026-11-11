#include "MetaInbox.h"

NAMESPACE_UPP

MetaInbox::MetaInbox(const String& site_id, const String& data_root)
{
	store.Create(site_id, data_root, "inbox");
}

bool MetaInbox::ValidType(const String& type)
{
	return type == "consent.requested" || type == "consent.granted"
	    || type == "consent.denied"   || type == "consent.revoked"
	    || type == "tf.grant_requested" || type == "tf.candidate_created"
	    || type == "tf.proposal_submitted";
}

void MetaInbox::PruneLocked(const String& subject_user)
{
	// Keep the latest RetainPerSubject() events of this subject, delete the
	// older ones. Called with the mutex held.
	Vector<String> ids;
	Store().List(ids);
	Vector<InboxEvent> all;
	Vector<String> all_ids;
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			InboxEvent e;
			LoadFromJsonValue(e, v);
			if(e.subject_user != subject_user)
				continue;
			all.Add(e);
			all_ids.Add(id);
		}
		catch(...) {}
	}
	if(all.GetCount() <= RetainPerSubject())
		return;
	// Sort indices by created_at, newest first.
	Vector<int> order;
	for(int i = 0; i < all.GetCount(); i++)
		order.Add(i);
	Sort(order, [&](int a, int b) { return all[a].created_at > all[b].created_at; });
	for(int i = RetainPerSubject(); i < order.GetCount(); i++)
		Store().Delete(all_ids[order[i]], String());
}

StoreResult MetaInbox::Push(const String& subject_user, const String& source_site, const String& type,
                            const String& actor, const String& message, const String& refs)
{
	if(subject_user.IsEmpty())
		return StoreResult("an inbox event needs a subject");
	if(!ValidType(type))
		return StoreResult("unknown inbox event type: " + type);
	if(source_site.IsEmpty())
		return StoreResult("an inbox event needs a source site");
	InboxEvent e;
	e.event_id = "evt_" + AsString(Uuid::Create());
	e.subject_user = subject_user;
	e.source_site = source_site;
	e.type = type;
	e.actor = actor;
	e.message = message.Left(MessageLimit());
	e.refs = refs;
	e.created_at = GetSysTime();
	e.read = false;
	mutex.Enter();
	String empty_rev;
	String new_rev;
	StoreResult r = Store().Put(e.event_id, StoreAsJson(e, true), empty_rev, new_rev);
	if(r.ok)
		PruneLocked(subject_user);
	mutex.Leave();
	return r;
}

StoreResult MetaInbox::List(const String& subject_user, Vector<InboxEvent>& out, int limit)
{
	out.Clear();
	mutex.Enter();
	Vector<String> ids;
	Store().List(ids);
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			InboxEvent e;
			LoadFromJsonValue(e, v);
			if(e.subject_user != subject_user)
				continue;
			out.Add(e);
		}
		catch(...) {}
	}
	mutex.Leave();
	Sort(out, [](const InboxEvent& a, const InboxEvent& b) {
		return a.created_at > b.created_at;
	});
	if(limit > 0 && out.GetCount() > limit)
		out.Remove(limit, out.GetCount() - limit);
	return StoreResult(true);
}

StoreResult MetaInbox::MarkAllRead(const String& subject_user)
{
	mutex.Enter();
	Vector<String> ids;
	Store().List(ids);
	for(const String& id : ids) {
		String json, rev;
		if(!Store().Get(id, json, rev))
			continue;
		try {
			Value v = ParseJSON(json);
			if(!v.Is<ValueMap>())
				continue;
			InboxEvent e;
			LoadFromJsonValue(e, v);
			if(e.subject_user != subject_user || e.read)
				continue;
			e.read = true;
			Store().Put(id, StoreAsJson(e, true), rev, rev);
		}
		catch(...) {}
	}
	mutex.Leave();
	return StoreResult(true);
}

int MetaInbox::UnreadCount(const String& subject_user)
{
	Vector<InboxEvent> all;
	List(subject_user, all, -1);
	int n = 0;
	for(const InboxEvent& e : all)
		if(!e.read)
			n++;
	return n;
}

END_UPP_NAMESPACE
