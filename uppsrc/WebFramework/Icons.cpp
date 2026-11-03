#include "WebFramework.h"

NAMESPACE_UPP

String VividIcon::Get(const String& name, int size, const String& color)
{
	String s = AsString(size);
	String prefix = Format("<svg width=\"%s\" height=\"%s\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%s\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" class=\"vivid-icon icon-%s\">",
	                       s, s, color, name);
	String suffix = "</svg>";

	String body;
	if(name == "logo" || name == "vivid") {
		body = "<path d=\"M12 2L2 7l10 5 10-5-10-5z\"/><path d=\"M2 17l10 5 10-5\"/><path d=\"M2 12l10 5 10-5\"/>";
	}
	else if(name == "home") {
		body = "<path d=\"M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z\"/><polyline points=\"9 22 9 12 15 12 15 22\"/>";
	}
	else if(name == "dashboard") {
		body = "<rect x=\"3\" y=\"3\" width=\"7\" height=\"9\"/><rect x=\"14\" y=\"3\" width=\"7\" height=\"5\"/><rect x=\"14\" y=\"12\" width=\"7\" height=\"9\"/><rect x=\"3\" y=\"16\" width=\"7\" height=\"5\"/>";
	}
	else if(name == "folder") {
		body = "<path d=\"M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z\"/>";
	}
	else if(name == "file") {
		body = "<path d=\"M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z\"/><polyline points=\"13 2 13 9 20 9\"/>";
	}
	else if(name == "git" || name == "git-branch") {
		body = "<line x1=\"6\" y1=\"3\" x2=\"6\" y2=\"15\"/><circle cx=\"18\" cy=\"6\" r=\"3\"/><circle cx=\"6\" cy=\"18\" r=\"3\"/><path d=\"M18 9a9 9 0 0 1-9 9\"/>";
	}
	else if(name == "git-commit") {
		body = "<circle cx=\"12\" cy=\"12\" r=\"4\"/><line x1=\"1.05\" y1=\"12\" x2=\"7\" y2=\"12\"/><line x1=\"17\" y1=\"12\" x2=\"22.95\" y2=\"12\"/>";
	}
	else if(name == "git-diff" || name == "diff") {
		body = "<line x1=\"12\" y1=\"5\" x2=\"12\" y2=\"19\"/><line x1=\"5\" y1=\"12\" x2=\"19\" y2=\"12\"/><line x1=\"5\" y1=\"5\" x2=\"19\" y2=\"19\"/>";
	}
	else if(name == "chat" || name == "message") {
		body = "<path d=\"M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z\"/>";
	}
	else if(name == "robot" || name == "agent") {
		body = "<rect x=\"3\" y=\"11\" width=\"18\" height=\"10\" rx=\"2\"/><circle cx=\"12\" cy=\"5\" r=\"2\"/><path d=\"M12 7v4\"/><line x1=\"8\" y1=\"16\" x2=\"8\" y2=\"16\"/><line x1=\"16\" y1=\"16\" x2=\"16\" y2=\"16\"/>";
	}
	else if(name == "terminal") {
		body = "<polyline points=\"4 17 10 11 4 5\"/><line x1=\"12\" y1=\"19\" x2=\"20\" y2=\"19\"/>";
	}
	else if(name == "code") {
		body = "<polyline points=\"16 18 22 12 16 6\"/><polyline points=\"8 6 2 12 8 18\"/>";
	}
	else if(name == "cog" || name == "settings") {
		body = "<circle cx=\"12\" cy=\"12\" r=\"3\"/><path d=\"M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z\"/>";
	}
	else if(name == "search") {
		body = "<circle cx=\"11\" cy=\"11\" r=\"8\"/><line x1=\"21\" y1=\"21\" x2=\"16.65\" y2=\"16.65\"/>";
	}
	else if(name == "bell") {
		body = "<path d=\"M18 8A6 6 0 0 0 6 8c0 7-3 9-3 9h18s-3-2-3-9\"/><path d=\"M13.73 21a2 2 0 0 1-3.46 0\"/>";
	}
	else if(name == "palette" || name == "theme") {
		body = "<circle cx=\"13.5\" cy=\"6.5\" r=\".5\" fill=\"currentColor\"/><circle cx=\"17.5\" cy=\"10.5\" r=\".5\" fill=\"currentColor\"/><circle cx=\"8.5\" cy=\"7.5\" r=\".5\" fill=\"currentColor\"/><circle cx=\"6.5\" cy=\"12.5\" r=\".5\" fill=\"currentColor\"/><path d=\"M12 2C6.49 2 2 6.49 2 12c0 3.31 2.69 6 6 6h1.5a1.5 1.5 0 0 1 1.5 1.5c0 .41-.17.79-.44 1.06A5.95 5.95 0 0 0 14 22c5.51 0 10-4.49 10-10 0-5.51-4.49-10-10-10z\"/>";
	}
	else if(name == "layout" || name == "template") {
		body = "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\"/><line x1=\"3\" y1=\"9\" x2=\"21\" y2=\"9\"/><line x1=\"9\" y1=\"21\" x2=\"9\" y2=\"9\"/>";
	}
	else if(name == "sliders" || name == "density") {
		body = "<line x1=\"4\" y1=\"21\" x2=\"4\" y2=\"14\"/><line x1=\"4\" y1=\"10\" x2=\"4\" y2=\"3\"/><line x1=\"12\" y1=\"21\" x2=\"12\" y2=\"12\"/><line x1=\"12\" y1=\"8\" x2=\"12\" y2=\"3\"/><line x1=\"20\" y1=\"21\" x2=\"20\" y2=\"16\"/><line x1=\"20\" y1=\"12\" x2=\"20\" y2=\"3\"/><line x1=\"1\" y1=\"14\" x2=\"7\" y2=\"14\"/><line x1=\"9\" y1=\"8\" x2=\"15\" y2=\"8\"/><line x1=\"17\" y1=\"16\" x2=\"23\" y2=\"16\"/>";
	}
	else if(name == "volume") {
		body = "<polygon points=\"11 5 6 9 2 9 2 15 6 15 11 19 11 5\"/><path d=\"M19.07 4.93a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07\"/>";
	}
	else if(name == "volume-mute") {
		body = "<polygon points=\"11 5 6 9 2 9 2 15 6 15 11 19 11 5\"/><line x1=\"23\" y1=\"9\" x2=\"17\" y2=\"15\"/><line x1=\"17\" y1=\"9\" x2=\"23\" y2=\"15\"/>";
	}
	else if(name == "chevron-down") {
		body = "<polyline points=\"6 9 12 15 18 9\"/>";
	}
	else if(name == "chevron-right") {
		body = "<polyline points=\"9 18 15 12 9 6\"/>";
	}
	else if(name == "chevron-left") {
		body = "<polyline points=\"15 18 9 12 15 6\"/>";
	}
	else if(name == "folder-open") {
		body = "<path d=\"M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z\"/><line x1=\"2\" y1=\"10\" x2=\"22\" y2=\"10\"/>";
	}
	else if(name == "file-text") {
		body = "<path d=\"M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z\"/><polyline points=\"14 2 14 8 20 8\"/><line x1=\"16\" y1=\"13\" x2=\"8\" y2=\"13\"/><line x1=\"16\" y1=\"17\" x2=\"8\" y2=\"17\"/>";
	}
	else if(name == "hash") {
		body = "<line x1=\"4\" y1=\"9\" x2=\"20\" y2=\"9\"/><line x1=\"4\" y1=\"15\" x2=\"20\" y2=\"15\"/><line x1=\"10\" y1=\"3\" x2=\"8\" y2=\"21\"/><line x1=\"16\" y1=\"3\" x2=\"14\" y2=\"21\"/>";
	}
	else if(name == "menu" || name == "bars") {
		body = "<line x1=\"3\" y1=\"12\" x2=\"21\" y2=\"12\"/><line x1=\"3\" y1=\"6\" x2=\"21\" y2=\"6\"/><line x1=\"3\" y1=\"18\" x2=\"21\" y2=\"18\"/>";
	}
	else if(name == "check") {
		body = "<polyline points=\"20 6 9 17 4 12\"/>";
	}
	else if(name == "copy") {
		body = "<rect x=\"9\" y=\"9\" width=\"13\" height=\"13\" rx=\"2\" ry=\"2\"/><path d=\"M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1\"/>";
	}
	else {
		// Generic bullet icon fallback
		body = "<circle cx=\"12\" cy=\"12\" r=\"4\"/>";
	}

	return prefix + body + suffix;
}

Vector<String> VividIcon::ListNames()
{
	Vector<String> list;
	list.Add("logo");
	list.Add("home");
	list.Add("dashboard");
	list.Add("folder");
	list.Add("file");
	list.Add("git-branch");
	list.Add("git-commit");
	list.Add("git-diff");
	list.Add("chat");
	list.Add("agent");
	list.Add("terminal");
	list.Add("code");
	list.Add("settings");
	list.Add("search");
	list.Add("bell");
	list.Add("palette");
	list.Add("layout");
	list.Add("density");
	list.Add("volume");
	list.Add("chevron-down");
	list.Add("chevron-right");
	list.Add("chevron-left");
	list.Add("folder-open");
	list.Add("file-text");
	list.Add("hash");
	list.Add("menu");
	list.Add("check");
	list.Add("copy");
	return list;
}

END_UPP_NAMESPACE
