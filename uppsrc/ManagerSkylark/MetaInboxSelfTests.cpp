#include <Core/Core.h>
#include <MetaAccount/MetaInbox.h>

NAMESPACE_UPP

// Pure unit checks for the meta identity inbox (task 024 item 2): push, list
// order, read state, the type allowlist, the message cap and retention. Each
// run uses its own scratch store; no HTTP.

static bool InboxCheck(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

bool RunMetaInboxSelfTests(const String& scratch_dir)
{
	bool all = true;
	RealizeDirectory(scratch_dir);
	MetaInbox inbox("meta-inbox-test", scratch_dir);

	// Refusals store nothing. (A literal refusal once bound the StoreResult(bool) constructor and
	// reported ok; the const char* constructor fixes that, task 026.)
	all &= InboxCheck(!inbox.Push("alice", "meta", "consent.bogus", "meta:alice", "x", "").ok,
	             "inbox: an event type off the allowlist is refused");
	all &= InboxCheck(!inbox.Push("", "meta", "consent.granted", "meta:alice", "x", "").ok,
	             "inbox: an event without a subject is refused");
	all &= InboxCheck(!inbox.Push("alice", "", "consent.granted", "meta:alice", "x", "").ok,
	             "inbox: an event without a source site is refused");
	Vector<InboxEvent> none;
	inbox.List("alice", none, -1);
	all &= InboxCheck(none.IsEmpty(), "inbox: refused events store nothing");

	// Message cap: longer text is cut to MessageLimit().
	String long_message(' ', MetaInbox::MessageLimit() + 50);
	all &= InboxCheck(inbox.Push("alice", "meta", "consent.requested", "meta:alice", long_message, "").ok,
	             "inbox: a long message is accepted and capped");
	Sleep(1100); // keep the next event in a later second, so the list order is deterministic
	Vector<InboxEvent> capped;
	inbox.List("alice", capped, -1);
	all &= InboxCheck(capped.GetCount() == 1 && capped[0].message.GetCount() == MetaInbox::MessageLimit(),
	             "inbox: the stored message is exactly MessageLimit() long");

	// Newest first. Event times have one-second resolution, so each push is
	// spaced by more than a second from the one before.
	inbox.MarkAllRead("alice");
	all &= InboxCheck(inbox.Push("alice", "meta", "consent.granted", "meta:alice", "first", "").ok,
	             "inbox: first event is pushed");
	Sleep(1100);
	all &= InboxCheck(inbox.Push("alice", "meta", "consent.denied", "meta:alice", "second", "meta:/consent").ok,
	             "inbox: second event is pushed");
	Vector<InboxEvent> listed;
	all &= InboxCheck(inbox.List("alice", listed, -1).ok, "inbox: list succeeds");
	all &= InboxCheck(listed.GetCount() == 3 && listed[0].message == "second" && listed[1].message == "first",
	             "inbox: list returns the newest event first");
	Vector<InboxEvent> one;
	inbox.List("alice", one, 1);
	all &= InboxCheck(one.GetCount() == 1 && one[0].message == "second", "inbox: a limit keeps the newest event");

	// Subject isolation and read state.
	inbox.Push("bob", "meta", "tf.grant_requested", "tf:bob", "bob's event", "");
	Vector<InboxEvent> alice_only;
	inbox.List("alice", alice_only, -1);
	bool isolated = true;
	for(const InboxEvent& e : alice_only)
		isolated = isolated && e.subject_user == "alice";
	all &= InboxCheck(isolated && alice_only.GetCount() == 3, "inbox: a subject sees only its own events");
	all &= InboxCheck(inbox.UnreadCount("alice") == 2 && inbox.UnreadCount("bob") == 1,
	             "inbox: unread counts start unread per subject");
	inbox.MarkAllRead("alice");
	all &= InboxCheck(inbox.UnreadCount("alice") == 0 && inbox.UnreadCount("bob") == 1,
	             "inbox: marking read affects only that subject");

	// Retention: push RetainPerSubject() + 5 events for one subject; the store
	// keeps exactly RetainPerSubject(), and other subjects are untouched.
	const int extra = 5;
	for(int i = 0; i < MetaInbox::RetainPerSubject() + extra; i++)
		inbox.Push("carol", "meta", "consent.requested", "meta:carol", AsString(i), "");
	Vector<InboxEvent> carol;
	inbox.List("carol", carol, -1);
	all &= InboxCheck(carol.GetCount() == MetaInbox::RetainPerSubject(),
	             "inbox: retention keeps RetainPerSubject() events per subject");
	Vector<InboxEvent> still_alice;
	inbox.List("alice", still_alice, -1);
	all &= InboxCheck(still_alice.GetCount() == 3, "inbox: retention does not prune another subject");

	return all;
}

END_UPP_NAMESPACE
