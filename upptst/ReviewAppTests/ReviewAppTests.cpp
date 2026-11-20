#include <ReviewApp/ReviewApp.h>
#include <WebAppHost/WebAppHost.h>

using namespace Upp;

static int checks = 0;
static int failures = 0;

static bool Check(bool cond, const String& what)
{
	checks++;
	if(cond)
		Cout() << "ok    " << what << "\n";
	else {
		failures++;
		Cout() << "FAILED: " << what << "\n";
	}
	return cond;
}

static ReviewItem Item(const String& id, const String& label)
{
	ReviewItem it;
	it.id = id;
	it.image_blob = "img-" + id;
	it.source_ref = "source-" + id;
	it.proposed_label = label;
	return it;
}

CONSOLE_APP_MAIN
{
	String root = AppendFileName(GetCurrentDirectory(), "review-selftest-data");
	DeleteFolderDeep(root);
	RealizeDirectory(root);
	ReviewQueue q("selftest", root);

	ReviewLabel a; a.id = "a"; a.name = "Label A";
	ReviewLabel b; b.id = "b"; b.name = "Label B";
	ReviewLabel c; c.id = "c"; c.name = "Label C (needs text)"; c.requires_text = true;
	Check(q.PutLabel(a, RecordAbsent()) && q.PutLabel(b, RecordAbsent()) && q.PutLabel(c, RecordAbsent()),
	      "labels: three labels are stored with RecordAbsent");
	Check(!q.PutLabel(a, RecordAbsent()) && !q.PutLabel(a, "7") && !q.PutLabel(a, ""),
	      "labels: a new label over an existing id, a stale revision and an empty revision are refused");
	Vector<ReviewLabel> labels;
	Check(q.ListLabels(labels) && labels.GetCount() == 3, "labels: the label set lists all three");

	for(int i = 1; i <= 5; i++) {
		String id = "r" + AsString(i);
		Check((bool)q.Enqueue(Item(id, i % 2 ? "a" : "b")), "queue: enqueue " + id);
	}
	Check(!q.Enqueue(Item("r1", "a")), "queue: an existing item id is refused");
	Check(!q.Enqueue(Item("x1", "zzz")), "label: an unknown proposed label is refused on enqueue");
	Check(!q.Enqueue(Item("../bad", "a")), "queue: an unsafe item id is refused");

	ReviewPage page;
	Check(q.Page("", 0, 2, page) && page.total == 5 && page.items.GetCount() == 2
	      && page.items[0].id == "r1" && page.items[1].id == "r2",
	      "paging: the first page holds two items of five, sorted by id");
	Check(q.Page("", 4, 2, page) && page.total == 5 && page.items.GetCount() == 1 && page.items[0].id == "r5",
	      "paging: the last page holds the remaining item");
	Check(q.Page("", 9, 2, page) && page.total == 5 && page.items.IsEmpty(),
	      "paging: an offset past the end gives an empty page with the total");
	Check(!q.Page("", 0, 0, page) && !q.Page("", 0, 101, page) && !q.Page("", -1, 2, page)
	      && !q.Page("bogus", 0, 2, page),
	      "paging: a zero or oversized limit, a negative offset and a bad filter are refused");

	// accept: confirms the proposed label
	ReviewItem out;
	Check(q.Get("r1", out) && out.revision == "1", "decision: a new item starts at revision 1");
	Check(q.Decide("r1", "accept", "", "", "alice", out.revision, out)
	      && out.status == "accept" && out.label == "a" && out.reviewer == "alice" && out.revision == "2",
	      "decision accept: keeps the proposed label, records the reviewer and advances the revision");

	// correct: another known label
	Check(q.Get("r2", out) && q.Decide("r2", "correct", "c", "note", "bob", out.revision, out)
	      && out.status == "correct" && out.label == "c" && out.text == "note" && out.reviewer == "bob",
	      "decision correct: the chosen label replaces the proposed one");
	Check(q.Get("r3", out) && !q.Decide("r3", "correct", "", "", "bob", out.revision, out)
	      && q.Get("r3", out) && out.status == "open",
	      "decision correct: a correction without a label is refused and the item stays open");

	// requires_text: accept and correct with label c need text
	Check(q.Get("r5", out) && !q.Decide("r5", "correct", "c", "", "bob", out.revision, out),
	      "label text: a label that requires text is refused when the text is empty");

	// reject: drops the label, keeps the text
	Check(q.Get("r4", out) && q.Decide("r4", "reject", "", "blurred", "carol", out.revision, out)
	      && out.status == "reject" && out.label.IsEmpty() && out.text == "blurred",
	      "decision reject: the label is cleared and the note kept");

	// uncertain: stays for later review
	Check(q.Get("r3", out) && q.Decide("r3", "uncertain", "", "two people", "carol", out.revision, out)
	      && out.status == "uncertain" && out.label.IsEmpty() && out.revision == "2",
	      "decision uncertain: no label, the note is kept, the revision advances");

	// revision conflict: a stale revision is refused and leaves the record unchanged
	ReviewItem stale;
	StoreResult conflict = q.Decide("r1", "reject", "", "", "dave", "1", stale);
	ReviewItem now;
	Check(!conflict && conflict.conflict && q.Get("r1", now) && now.status == "accept" && now.revision == "2",
	      "revision conflict: a stale revision is refused and the record is unchanged");
	Check(!q.Decide("r1", "reject", "", "", "dave", "", stale), "revision conflict: an empty revision is refused");

	// unknown label refused
	Check(q.Get("r5", out) && !q.Decide("r5", "correct", "zzz", "", "bob", out.revision, out)
	      && q.Get("r5", now) && now.status == "open",
	      "unknown label: a label that is not in the set is refused and nothing changes");

	Check(!q.Decide("r5", "maybe", "", "", "bob", out.revision, out), "decision: an unknown decision is refused");
	Check(!q.Decide("r5", "reject", "", "", "", out.revision, out), "decision: a missing reviewer is refused");
	Check(!q.Decide("missing", "reject", "", "", "bob", "1", out), "decision: an unknown item is refused");

	// filtered paging after decisions
	Check(q.Page("open", 0, 10, page) && page.total == 1 && page.items[0].id == "r5",
	      "paging: one item is still open (r5); the uncertain item is not open");
	Check(q.Page("accept", 0, 10, page) && page.total == 1 && page.items[0].id == "r1",
	      "paging: the accept filter lists the accepted item");

	WebApp *app = CreateWebapp("ReviewApp");
	Check(app && app->Id() == "ReviewApp" && app->DefaultPort() == 18420,
	      "webapp: ReviewApp is registered from its .icpp and creates its app");
	delete app;

	DeleteFolderDeep(root);
	Cout() << "\nchecks: " << checks << ", failures: " << failures << "\n";
	SetExitCode(failures ? 1 : 0);
}
