#include "SiteCli.h"

#include <Core/Ext/TOON.h>

NAMESPACE_UPP

static int st_checks = 0;
static int st_failures = 0;

static void STCheck(bool cond, const String& what)
{
	st_checks++;
	if(!cond)
		st_failures++;
	Cout() << (cond ? "ok   " : "FAIL ") << what << "\n";
}

static ValueMap Arg(const char *name, const char *type, bool required)
{
	ValueMap a;
	a.Add("name", name);
	a.Add("type", type);
	a.Add("required", required);
	a.Add("description", "");
	return a;
}

static ValueMap Command(const char *method, const char *api_path, const char *transport, ValueArray args)
{
	ValueMap c;
	c.Add("name", "test");
	c.Add("transport", transport);
	c.Add("method", method);
	c.Add("api_path", api_path);
	c.Add("args", args);
	return c;
}

static Vector<String> Words(const char *a = nullptr, const char *b = nullptr, const char *c = nullptr,
                            const char *d = nullptr, const char *e = nullptr)
{
	Vector<String> w;
	const char *all[] = {a, b, c, d, e};
	for(const char *s : all)
		if(s)
			w.Add(s);
	return w;
}

int SiteCliSelfTests()
{
	st_checks = 0;
	st_failures = 0;

	// PrivateHub issue-create: owner and repo fill the two path segments, the rest is a form body.
	{
		ValueArray args;
		args.Add(Arg("owner", "string", true));
		args.Add(Arg("repo", "string", true));
		args.Add(Arg("title", "string", true));
		args.Add(Arg("body", "text", false));
		ValueMap cmd = Command("POST", "privatehub/*/*/issues/create", "http", args);
		SiteCliRequest req;
		String err;
		bool ok = SiteCliBuildRequest(cmd, Words("acme", "widget", "title=Bug report", "body=Line one"), req, err);
		STCheck(ok, "positional owner and repo fill the path, keyed fields fill the form");
		STCheck(req.url_path == "privatehub/acme/widget/issues/create", "path segments take the first declared arguments");
		STCheck(req.body == "title=Bug%20report&body=Line%20one", "form body is percent-encoded");
		STCheck(req.content_type == "application/x-www-form-urlencoded", "non-api route sends a form body");
	}

	// Errors name the argument; nothing is sent when one is missing.
	{
		ValueArray args;
		args.Add(Arg("owner", "string", true));
		args.Add(Arg("repo", "string", true));
		args.Add(Arg("title", "string", true));
		ValueMap cmd = Command("POST", "privatehub/*/*/issues/create", "http", args);
		SiteCliRequest req;
		String err;
		STCheck(!SiteCliBuildRequest(cmd, Words("acme", "widget"), req, err) &&
		        err == "missing required argument 'title'",
		        "a missing required argument is named in the error");
		STCheck(!SiteCliBuildRequest(cmd, Words("acme", "widget", "title=x", "colour=red"), req, err) &&
		        err == "unknown argument 'colour'",
		        "an unknown keyed argument is refused");
		STCheck(!SiteCliBuildRequest(cmd, Words("acme", "widget", "x", "y", "z"), req, err),
		        "too many positional arguments are refused");
	}

	// api/ routes send JSON, with int and number arguments as JSON numbers.
	{
		ValueArray args;
		args.Add(Arg("id", "string", true));
		args.Add(Arg("expected_paper_revision", "int", true));
		args.Add(Arg("score", "number", false));
		ValueMap cmd = Command("POST", "api/papers/*/article", "http", args);
		SiteCliRequest req;
		String err;
		bool ok = SiteCliBuildRequest(cmd, Words("2612.00002", "3", "score=1.5"), req, err);
		STCheck(ok && req.content_type == "application/json", "api route sends a JSON body");
		Value v = ParseJSON(req.body);
		STCheck(req.url_path == "api/papers/2612.00002/article", "api path takes the id segment");
		STCheck((int)v["expected_paper_revision"] == 3 && (double)v["score"] == 1.5,
		        "int and number arguments are JSON numbers");
		STCheck(IsNull(v["id"]), "the path argument is not repeated in the body");
		STCheck(!SiteCliBuildRequest(cmd, Words("2612.00002", "three"), req, err) &&
		        err == "argument 'expected_paper_revision' must be an integer",
		        "an int argument with text is refused before sending");
	}

	// GET sends its arguments as a query string.
	{
		ValueArray args;
		args.Add(Arg("q", "string", false));
		ValueMap cmd = Command("GET", "api/papers", "http", args);
		SiteCliRequest req;
		String err;
		SiteCliBuildRequest(cmd, Words("q=a b"), req, err);
		STCheck(req.method == "GET" && req.url_path == "api/papers?q=a%20b" && req.body.IsEmpty(),
		        "GET arguments go in the query string");
	}

	// A path segment with no declared argument is an error, not a silent empty segment.
	{
		ValueMap cmd = Command("GET", "api/gh/repos/*", "http", ValueArray());
		SiteCliRequest req;
		String err;
		STCheck(!SiteCliBuildRequest(cmd, Words(), req, err) && err.Find("path argument") >= 0,
		        "a route segment with no argument is refused");
	}

	// Did-you-mean: shared prefix or substring, nothing unrelated.
	{
		Vector<String> names;
		names.Add("issues-list");
		names.Add("pulls-list");
		names.Add("repos-list");
		Vector<String> near = SiteCliSuggest("issue", names);
		STCheck(near.GetCount() == 1 && near[0] == "issues-list", "suggestion finds the close section name");
		STCheck(SiteCliSuggest("zzzz", names).IsEmpty(), "no suggestion for an unrelated name");
	}

	// TOON is the same value as JSON: a round trip keeps it.
	{
		Value v = ParseJSON("{\"ok\":true,\"items\":[1,2],\"user\":\"alice\"}");
		Value back = ParseTOON(AsTOON(v));
		ValueArray items = back["items"];
		STCheck((bool)back["ok"] && (String)back["user"] == "alice" && items.GetCount() == 2,
		        "TOON output parses back to the same value");
	}

	Cout() << "\nSiteCli self-tests: " << (st_checks - st_failures) << "/" << st_checks << "\n";
	return st_failures;
}

END_UPP_NAMESPACE
