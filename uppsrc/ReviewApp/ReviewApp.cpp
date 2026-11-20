#include "ReviewApp.h"

NAMESPACE_UPP

bool ReviewQueue::ValidDecision(const String& d)
{
	return d == "accept" || d == "correct" || d == "reject" || d == "uncertain";
}

bool ReviewQueue::ValidStatusFilter(const String& s)
{
	return s.IsEmpty() || s == "open" || ValidDecision(s);
}

ReviewQueue::ReviewQueue(const String& site_id, const String& data_root)
	: items(site_id, data_root, "review-items"), labels(site_id, data_root, "review-labels")
{
}

static bool ValidText(const String& s, int max_len)
{
	return s.GetCount() <= max_len && s.Find('\0') < 0;
}

StoreResult ReviewQueue::PutLabel(const ReviewLabel& label, const String& expected_revision)
{
	if(!JsonFileStore::ValidId(label.id))
		return "invalid label id: " + label.id;
	if(label.name.IsEmpty() || !ValidText(label.name, 200))
		return "label name is required (at most 200 bytes)";
	if(expected_revision.IsEmpty())
		return "revision required: use the current revision, or RecordAbsent for a new label";
	String json = StoreAsJson(label), revision;
	return labels.Put(label.id, json, expected_revision, revision);
}

StoreResult ReviewQueue::GetLabel(const String& id, ReviewLabel& out)
{
	String json, revision;
	StoreResult r = labels.Get(id, json, revision);
	if(!r)
		return r;
	try {
		LoadFromJson(out, json);
	}
	catch(Exc& e) {
		return "label record is corrupt: " + e;
	}
	out.revision = revision;
	return true;
}

StoreResult ReviewQueue::ListLabels(Vector<ReviewLabel>& out)
{
	out.Clear();
	Vector<String> ids;
	StoreResult listed = labels.List(ids);
	if(!listed)
		return listed;
	for(const String& id : ids) {
		ReviewLabel l;
		if(GetLabel(id, l))
			out.Add(pick(l));
	}
	return true;
}

StoreResult ReviewQueue::Enqueue(const ReviewItem& in)
{
	if(!JsonFileStore::ValidId(in.id))
		return "invalid item id: " + in.id;
	if(!JsonFileStore::ValidId(in.image_blob))
		return "invalid image blob id: " + in.image_blob;
	if(!ValidText(in.source_ref, 500))
		return "source reference is too long (at most 500 bytes)";
	if(in.proposed_label.GetCount()) {
		ReviewLabel l;
		if(!GetLabel(in.proposed_label, l))
			return "unknown label '" + in.proposed_label + "'";
	}
	ReviewItem item;
	item.id = in.id;
	item.image_blob = in.image_blob;
	item.source_ref = in.source_ref;
	item.proposed_label = in.proposed_label;
	item.status = "open";
	String revision;
	return items.Put(item.id, StoreAsJson(item), RecordAbsent(), revision);
}

StoreResult ReviewQueue::Get(const String& id, ReviewItem& out)
{
	String json, revision;
	StoreResult r = items.Get(id, json, revision);
	if(!r)
		return r;
	try {
		LoadFromJson(out, json);
	}
	catch(Exc& e) {
		return "item record is corrupt: " + e;
	}
	out.revision = revision;
	return true;
}

StoreResult ReviewQueue::Decide(const String& id, const String& decision, const String& label,
                                const String& text, const String& reviewer,
                                const String& expected_revision, ReviewItem& out)
{
	if(!ValidDecision(decision))
		return "unknown decision '" + decision + "'";
	if(reviewer.IsEmpty() || !ValidText(reviewer, 100))
		return "reviewer is required (at most 100 bytes)";
	if(!ValidText(text, 2000))
		return "text is too long (at most 2000 bytes)";
	if(expected_revision.IsEmpty())
		return "revision required: decide on the revision you reviewed";

	ReviewItem cur;
	StoreResult got = Get(id, cur);
	if(!got)
		return got;

	String final_label;
	if(decision == "accept") {
		if(cur.proposed_label.IsEmpty())
			return "accept needs a proposed label on the item";
		final_label = cur.proposed_label;
	}
	else if(decision == "correct") {
		if(label.IsEmpty())
			return "correct needs a label";
		final_label = label;
	}

	ReviewLabel lab;
	if(final_label.GetCount()) {
		if(!GetLabel(final_label, lab))
			return "unknown label '" + final_label + "'";
		if(lab.requires_text && (decision == "accept" || decision == "correct") && text.IsEmpty())
			return "label '" + final_label + "' requires text";
	}

	cur.status = decision;
	cur.label = final_label;
	cur.text = text;
	cur.reviewer = reviewer;
	String new_revision;
	StoreResult stored = items.Put(id, StoreAsJson(cur), expected_revision, new_revision);
	if(!stored)
		return stored;
	cur.revision = new_revision;
	out = cur;
	return true;
}

StoreResult ReviewQueue::Page(const String& status, int offset, int limit, ReviewPage& out)
{
	if(!ValidStatusFilter(status) || offset < 0 || limit < 1 || limit > 100)
		return "invalid page request";
	out = ReviewPage();
	out.offset = offset;
	out.limit = limit;
	Vector<String> ids;
	StoreResult listed = items.List(ids);
	if(!listed)
		return listed;
	for(const String& id : ids) {
		ReviewItem it;
		if(!Get(id, it))
			continue;
		if(status.GetCount() && it.status != status)
			continue;
		if(out.total >= offset && out.items.GetCount() < limit)
			out.items.Add(pick(it));
		out.total++;
	}
	return true;
}

END_UPP_NAMESPACE
