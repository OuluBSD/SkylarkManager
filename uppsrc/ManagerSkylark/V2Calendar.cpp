#include "V2Calendar.h"

NAMESPACE_UPP

struct V2CalendarEntry : Moveable<V2CalendarEntry> {
	int    date_key;
	String time_key;
	String text;
	String href;
};

static String CalEscape(const String& s)
{
	String r;
	for(int i = 0; i < s.GetCount(); i++) {
		char c = s[i];
		if(c == '&') r << "&amp;";
		else if(c == '<') r << "&lt;";
		else if(c == '>') r << "&gt;";
		else if(c == '"') r << "&quot;";
		else r.Cat(c);
	}
	return r;
}

// Sakamoto's method: 0 = Sunday.
static int Weekday(int year, int month, int day)
{
	static const int offset[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	if(month < 3)
		year--;
	return (year + year / 4 - year / 100 + year / 400 + offset[month - 1] + day) % 7;
}

static void AddEntry(Vector<V2CalendarEntry>& out, const String& iso, const String& text, const String& href)
{
	V2CalendarEntry e;
	if(ParseV2Date(iso, e.date_key, e.time_key)) {
		e.text = text;
		e.href = href;
		out.Add(e);
	}
}

String RenderV2CalendarMonth(const V2Store& v2, int year, int month)
{
	Vector<V2CalendarEntry> all;
	const Vector<V2Release>& releases = v2.GetReleases();
	for(int i = 0; i < releases.GetCount(); i++) {
		const V2Release& r = releases[i];
		AddEntry(all, r.planned_at, "Release planned: " + r.title, "/releases");
		AddEntry(all, r.released_at, "Released: " + r.title, "/releases");
	}
	const Vector<V2Goal>& goals = v2.GetGoals();
	for(int i = 0; i < goals.GetCount(); i++) {
		const V2Goal& g = goals[i];
		for(int j = 0; j < g.tasks.GetCount(); j++) {
			const V2GoalNode& n = g.tasks[j];
			AddEntry(all, n.planned_at, "Planned: " + n.title, "/goal/" + g.id);
			AddEntry(all, n.due_at, "Due: " + n.title, "/goal/" + g.id);
		}
	}

	// Keep this month and sort by day, then time of day (date-only first), then text.
	int ym = year * 100 + month;
	Vector<V2CalendarEntry> entries;
	for(int i = 0; i < all.GetCount(); i++)
		if(all[i].date_key / 100 == ym)
			entries.Add(all[i]);
	Sort(entries, [](const V2CalendarEntry& a, const V2CalendarEntry& b) {
		if(a.date_key != b.date_key)
			return a.date_key < b.date_key;
		if(a.time_key != b.time_key)
			return a.time_key < b.time_key;
		return a.text < b.text;
	});

	int prev_y = month == 1 ? year - 1 : year;
	int prev_m = month == 1 ? 12 : month - 1;
	int next_y = month == 12 ? year + 1 : year;
	int next_m = month == 12 ? 1 : month + 1;

	String b;
	b << "<div class=\"card\">\n"
	  << "  <div class=\"card-header\"><span>Calendar: " << Format("%04d-%02d", year, month) << "</span></div>\n"
	  << "  <p style=\"margin-bottom:1rem;\">"
	  << "<a href=\"/calendar?m=" << Format("%04d-%02d", prev_y, prev_m) << "\">&larr; Previous month</a> | "
	  << "<a href=\"/calendar?m=" << Format("%04d-%02d", next_y, next_m) << "\">Next month &rarr;</a></p>\n";

	if(entries.IsEmpty())
		b << "  <p style=\"font-size:0.85rem; color:#94a3b8;\">No dated entries this month.</p>\n";

	static const char* names[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
	b << "  <table class=\"calendar\"><thead><tr>";
	for(int c = 0; c < 7; c++)
		b << "<th>" << names[c] << "</th>";
	b << "</tr></thead><tbody>\n";

	// Monday-first grid: offset is the number of empty cells before day 1.
	int total = V2DaysInMonth(year, month);
	int offset = (Weekday(year, month, 1) + 6) % 7;
	int rows = (offset + total + 6) / 7;
	for(int r = 0; r < rows; r++) {
		b << "<tr>";
		for(int c = 0; c < 7; c++) {
			int day = r * 7 + c - offset + 1;
			if(day < 1 || day > total) {
				b << "<td></td>";
				continue;
			}
			b << "<td><strong>" << day << "</strong><ul style=\"list-style:none; padding:0; margin:0.25rem 0 0;\">";
			int date_key = ym * 100 + day;
			for(int i = 0; i < entries.GetCount(); i++) {
				const V2CalendarEntry& e = entries[i];
				if(e.date_key != date_key)
					continue;
				b << "<li style=\"font-size:0.8rem;\"><a href=\"" << CalEscape(e.href) << "\">"
				  << (e.time_key.IsEmpty() ? "" : CalEscape(e.time_key) + " ")
				  << CalEscape(e.text) << "</a></li>";
			}
			b << "</ul></td>";
		}
		b << "</tr>\n";
	}
	b << "</tbody></table>\n</div>\n";
	return b;
}

END_UPP_NAMESPACE
