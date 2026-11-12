#include <Core/Core.h>
#include "V2Calendar.h"

NAMESPACE_UPP

// Pure checks for the v2 date helpers and the /calendar month renderer (task 025).
// The fixture month is fixed (2026-11), so the checks do not depend on today's date.

static bool V2CalendarCheck(bool cond, const String& what)
{
	if(cond)
		Cout() << "ok    " << what << "\n";
	else
		Cout() << "FAILED: " << what << "\n";
	return cond;
}

static void WriteFixture(const String& path, const String& text)
{
	RealizeDirectory(GetFileFolder(path));
	SaveFile(path, text);
}

bool RunV2CalendarSelfTests(const String& scratch_dir)
{
	bool all = true;

	// ParseV2Date: date-only, with a time, invalid and impossible dates.
	int key = 0;
	String time;
	all &= V2CalendarCheck(ParseV2Date("2026-11-05", key, time) && key == 20261105 && time.IsEmpty(),
	             "v2 date: a date-only string gives its YYYYMMDD key with no time");
	all &= V2CalendarCheck(ParseV2Date("2026-11-05T14:30:00Z", key, time) && key == 20261105 && time == "14:30",
	             "v2 date: a datetime keeps HH:MM as the time key");
	all &= V2CalendarCheck(ParseV2Date("2026-11-05 09:15", key, time) && key == 20261105 && time == "09:15",
	             "v2 date: a space separator also carries a time");
	all &= V2CalendarCheck(!ParseV2Date("", key, time), "v2 date: an empty string is no date");
	all &= V2CalendarCheck(!ParseV2Date("garbage", key, time), "v2 date: garbage is no date");
	all &= V2CalendarCheck(!ParseV2Date("2026-11-5", key, time), "v2 date: a short day is no date");
	all &= V2CalendarCheck(!ParseV2Date("2026-1a-05", key, time), "v2 date: a non-digit month is no date");
	all &= V2CalendarCheck(!ParseV2Date("2026-13-01", key, time), "v2 date: month 13 is refused");
	all &= V2CalendarCheck(!ParseV2Date("2026-02-29", key, time), "v2 date: Feb 29 in a common year is refused");
	all &= V2CalendarCheck(ParseV2Date("2028-02-29", key, time) && key == 20280229, "v2 date: Feb 29 in a leap year is accepted");

	// ParseV2Month: the ?m= parameter.
	int year = 0;
	int month = 0;
	all &= V2CalendarCheck(ParseV2Month("2026-11", year, month) && year == 2026 && month == 11,
	             "v2 month: YYYY-MM parses");
	all &= V2CalendarCheck(!ParseV2Month("2026-00", year, month) && !ParseV2Month("2026-13", year, month),
	             "v2 month: month 00 and 13 are refused");
	all &= V2CalendarCheck(!ParseV2Month("garbage", year, month) && !ParseV2Month("2026-1", year, month),
	             "v2 month: garbage and short forms are refused");

	// A small v2 tree: one goal with two dated tasks, one dated release, one bad date.
	String root = AppendFileName(scratch_dir, "v2");
	WriteFixture(AppendFileName(root, "goals/mgr/goal-cal.json"),
	             "{\"id\":\"goal-cal\",\"kind\":\"goal\",\"repo\":\"mgr\",\"title\":\"Calendar goal\","
	             "\"tasks\":["
	             "{\"kind\":\"task\",\"id\":\"t-due\",\"title\":\"Ship the calendar\",\"status\":\"active\",\"due_at\":\"2026-11-05T14:30:00Z\"},"
	             "{\"kind\":\"task\",\"id\":\"t-fish\",\"title\":\"Fish & <chips>\",\"status\":\"planned\",\"planned_at\":\"2026-11-07\"},"
	             "{\"kind\":\"task\",\"id\":\"t-bad\",\"title\":\"Bad date\",\"status\":\"planned\",\"due_at\":\"2026-13-40\"}"
	             "]}");
	WriteFixture(AppendFileName(root, "releases/rel-1.json"),
	             "{\"id\":\"rel-1\",\"kind\":\"release\",\"title\":\"Milestone One\",\"status\":\"planned\","
	             "\"planned_at\":\"2026-11-20\",\"released_at\":\"\"}");

	V2Store store;
	store.Load(root);
	all &= V2CalendarCheck(store.GetGoals().GetCount() == 1 && store.GetReleases().GetCount() == 1,
	             "v2 store: the additive date fields load with the goal and release");
	all &= V2CalendarCheck(store.GetGoals()[0].tasks[0].due_at == "2026-11-05T14:30:00Z",
	             "v2 store: a task's due_at is read");
	all &= V2CalendarCheck(store.GetReleases()[0].planned_at == "2026-11-20",
	             "v2 store: a release's planned_at is read");

	String nov = RenderV2CalendarMonth(store, 2026, 11);
	all &= V2CalendarCheck(nov.Find("Release planned: Milestone One") >= 0, "calendar: the planned release appears on its day");
	all &= V2CalendarCheck(nov.Find("14:30 Due: Ship the calendar") >= 0, "calendar: a timed task entry shows its time");
	all &= V2CalendarCheck(nov.Find("Planned: Fish &amp; &lt;chips&gt;") >= 0, "calendar: titles are escaped");
	all &= V2CalendarCheck(nov.Find("href=\"/goal/goal-cal\"") >= 0, "calendar: a task entry links to its goal");
	all &= V2CalendarCheck(nov.Find("href=\"/releases\"") >= 0, "calendar: a release entry links to /releases");
	all &= V2CalendarCheck(nov.Find("Bad date") < 0, "calendar: an invalid date adds no entry");
	all &= V2CalendarCheck(nov.Find("/calendar?m=2026-10") >= 0 && nov.Find("/calendar?m=2026-12") >= 0,
	             "calendar: previous and next month links");
	// 2026-11-01 is a Sunday, so the Monday-first grid starts day 1 after six empty cells.
	all &= V2CalendarCheck(nov.Find("<tr><td></td><td></td><td></td><td></td><td></td><td></td><td><strong>1</strong>") >= 0,
	             "calendar: the grid starts on the right weekday (Nov 1 2026 is a Sunday)");

	String dec = RenderV2CalendarMonth(store, 2026, 12);
	all &= V2CalendarCheck(dec.Find("No dated entries this month") >= 0, "calendar: an empty month renders with the empty note");
	all &= V2CalendarCheck(dec.Find("<strong>31</strong>") >= 0, "calendar: an empty month still renders its full grid");

	String repo_less = RenderV2CalendarMonth(V2Store(), 2026, 11);
	all &= V2CalendarCheck(repo_less.Find("No dated entries this month") >= 0, "calendar: no v2 data renders an empty month");

	return all;
}

END_UPP_NAMESPACE
