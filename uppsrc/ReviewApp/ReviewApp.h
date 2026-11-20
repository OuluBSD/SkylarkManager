#ifndef _ReviewApp_ReviewApp_h_
#define _ReviewApp_ReviewApp_h_

#include <WebStorage/WebStorage.h>

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// ReviewApp: a queue of items waiting for a reviewer's decision.
//
// An item refers to an image blob (BlobStore id), a source reference (opaque text)
// and a proposed label. A reviewer records one decision per item:
//   accept     the proposed label is confirmed
//   correct    another known label is chosen
//   reject     the item is dropped from the labelled set
//   uncertain  the reviewer cannot decide; the item stays in the review set with a note
// Labels come from a label set (id, name, requires_text). Items and labels are JsonFileStore
// records, so every write is revision-checked: a stale revision is a conflict, never an overwrite.
// ---------------------------------------------------------------------------

struct ReviewLabel : Moveable<ReviewLabel> {
	String id;
	String name;
	bool   requires_text = false;
	String revision;          // from the envelope; not stored in the data

	void Jsonize(JsonIO& io) {
		io("id", id)("name", name)("requires_text", requires_text);
	}
};

struct ReviewItem : Moveable<ReviewItem> {
	String id;
	String image_blob;        // BlobStore id of the image
	String source_ref;        // where the item came from (opaque, at most 500 bytes)
	String proposed_label;    // label id, or empty
	String status;            // "open" until decided, then the decision name
	String label;             // the confirmed or corrected label id, or empty
	String text;              // free text from the reviewer
	String reviewer;          // reviewer id of the last decision
	String revision;          // from the envelope; not stored in the data

	void Jsonize(JsonIO& io) {
		io("id", id)
		  ("image_blob", image_blob)
		  ("source_ref", source_ref)
		  ("proposed_label", proposed_label)
		  ("status", status)
		  ("label", label)
		  ("text", text)
		  ("reviewer", reviewer)
		;
	}
};

struct ReviewPage : Moveable<ReviewPage> {
	int               total = 0;     // items matching the status filter
	int               offset = 0;
	int               limit = 0;
	Vector<ReviewItem> items;
};

class ReviewQueue {
public:
	ReviewQueue(const String& site_id, const String& data_root);

	static bool ValidDecision(const String& decision);   // accept, correct, reject, uncertain
	static bool ValidStatusFilter(const String& status); // empty, open, or a decision

	// Labels. expected_revision is RecordAbsent() for a new label, else the current revision.
	StoreResult PutLabel(const ReviewLabel& label, const String& expected_revision);
	StoreResult GetLabel(const String& id, ReviewLabel& out);
	StoreResult ListLabels(Vector<ReviewLabel>& out);

	// Adds an open item. The item id must be new; the proposed label, when set, must be a known label.
	StoreResult Enqueue(const ReviewItem& item);
	StoreResult Get(const String& id, ReviewItem& out);

	// Records a decision. expected_revision is required: the revision the reviewer saw.
	// accept uses the proposed label; correct needs a known label; a label with requires_text needs text
	// for accept and correct. reject and uncertain clear the label and keep the text.
	StoreResult Decide(const String& id, const String& decision, const String& label, const String& text,
	                   const String& reviewer, const String& expected_revision, ReviewItem& out);

	// Items sorted by id. status "" lists all; "open" lists undecided items; a decision lists those.
	// limit is 1..100.
	StoreResult Page(const String& status, int offset, int limit, ReviewPage& out);

private:
	JsonFileStore items;
	JsonFileStore labels;
};

END_UPP_NAMESPACE

#endif
