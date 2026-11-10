#include "Manifest.h"

NAMESPACE_UPP

bool CommandManifest::Add(const String& site_id, const String& site_title, CommandDesc cmd)
{
	Mutex::Lock lock(mutex);
	for(const Entry& e : entries)
		if(e.site_id == site_id && e.cmd.name == cmd.name)
			return false;
	Entry& e = entries.Add();
	e.site_id = site_id;
	e.site_title = site_title;
	e.cmd = pick(cmd);
	return true;
}

static ValueMap ArgToValue(const CommandArg& a)
{
	ValueMap m;
	m.Add("name", a.name);
	m.Add("type", a.type);
	m.Add("required", a.required);
	m.Add("description", a.description);
	return m;
}

static ValueMap CommandToValue(const CommandDesc& c)
{
	ValueMap m;
	m.Add("name", c.name);
	m.Add("transport", c.transport);
	m.Add("method", c.method);
	m.Add("api_path", c.api_path);
	m.Add("broker_op", c.broker_op);
	m.Add("auth", c.auth);
	m.Add("response", c.response);
	m.Add("description", c.description);
	ValueArray args;
	for(const CommandArg& a : c.args)
		args.Add(ArgToValue(a));
	m.Add("args", args);
	return m;
}

ValueMap CommandManifest::Describe(const String& site_id) const
{
	Mutex::Lock lock(mutex);
	Vector<String> order;
	for(const Entry& e : entries)
		if(site_id.IsEmpty() || e.site_id == site_id) {
			bool seen = false;
			for(const String& id : order)
				seen = seen || id == e.site_id;
			if(!seen)
				order.Add(e.site_id);
		}

	ValueArray sites;
	for(const String& id : order) {
		ValueMap site;
		site.Add("id", id);
		for(const Entry& e : entries)
			if(e.site_id == id) {
				site.Add("title", e.site_title);
				break;
			}
		ValueArray commands;
		for(const Entry& e : entries)
			if(e.site_id == id)
				commands.Add(CommandToValue(e.cmd));
		site.Add("commands", commands);
		sites.Add(site);
	}

	ValueMap out;
	out.Add("schema", "skylark-command-manifest/1");
	out.Add("sites", sites);
	return out;
}

bool CommandManifest::HasSite(const String& site_id) const
{
	Mutex::Lock lock(mutex);
	for(const Entry& e : entries)
		if(e.site_id == site_id)
			return true;
	return false;
}

int CommandManifest::GetCount() const
{
	Mutex::Lock lock(mutex);
	return entries.GetCount();
}

// "name:type[:required]" -> CommandArg. The type defaults to string.
static CommandArg ParseArg(const String& spec)
{
	CommandArg a;
	Vector<String> parts = Split(spec, ':');
	a.name = parts.GetCount() > 0 ? parts[0] : String();
	a.type = parts.GetCount() > 1 && !parts[1].IsEmpty() ? parts[1] : String("string");
	a.required = parts.GetCount() > 2 && parts[2] == "required";
	return a;
}

static Vector<CommandArg> ParseArgs(const Vector<String>& specs)
{
	Vector<CommandArg> out;
	for(const String& s : specs)
		out.Add(ParseArg(s));
	return out;
}

CommandDesc HttpCommand(const char *name, const char *method, const char *api_path, const char *auth,
                        const char *response, const char *description, const Vector<String>& args)
{
	CommandDesc c;
	c.name = name;
	c.transport = "http";
	c.method = method;
	c.api_path = api_path;
	c.auth = auth;
	c.response = response;
	c.description = description;
	c.args = ParseArgs(args);
	return c;
}

CommandDesc BrokerCommand(const char *name, const char *broker_op, const char *auth,
                          const char *description, const Vector<String>& args)
{
	CommandDesc c;
	c.name = name;
	c.transport = "broker";
	c.broker_op = broker_op;
	c.auth = auth;
	c.response = "broker";
	c.description = description;
	c.args = ParseArgs(args);
	return c;
}

END_UPP_NAMESPACE
