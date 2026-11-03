#include "Session.h"

NAMESPACE_UPP

ChatMessage::ChatMessage()
	: timestamp(GetSysTime().Get())
	, status("complete")
{
}

ChatMessage::ChatMessage(const ChatMessage& o)
	: id(o.id)
	, role(o.role)
	, sender(o.sender)
	, content(o.content)
	, thought(o.thought)
	, timestamp(o.timestamp)
	, status(o.status)
{
}

ChatMessage& ChatMessage::operator=(const ChatMessage& o)
{
	if(this != &o) {
		id = o.id;
		role = o.role;
		sender = o.sender;
		content = o.content;
		thought = o.thought;
		timestamp = o.timestamp;
		status = o.status;
	}
	return *this;
}

void ChatMessage::Jsonize(JsonIO& io)
{
	io("id", id);
	io("role", role);
	io("sender", sender);
	io("content", content);
	io("thought", thought);
	io("timestamp", timestamp);
	io("status", status);
}

String ChatMessage::ToJSON(bool pretty) const
{
	ChatMessage copy = *this;
	return StoreAsJson(copy, pretty);
}

bool ChatMessage::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
}

// ---------------------------------------------------------------------------

ChatSession::ChatSession()
	: created_at(GetSysTime().Get())
	, updated_at(GetSysTime().Get())
	, is_streaming(false)
{
}

ChatSession::ChatSession(const ChatSession& o)
	: id(o.id)
	, title(o.title)
	, model(o.model)
	, pair_mode(o.pair_mode)
	, context_repo(o.context_repo)
	, context_file(o.context_file)
	, system_prompt(o.system_prompt)
	, created_at(o.created_at)
	, updated_at(o.updated_at)
	, messages(clone(o.messages))
	, is_streaming(o.is_streaming)
{
}

ChatSession& ChatSession::operator=(const ChatSession& o)
{
	if(this != &o) {
		id = o.id;
		title = o.title;
		model = o.model;
		pair_mode = o.pair_mode;
		context_repo = o.context_repo;
		context_file = o.context_file;
		system_prompt = o.system_prompt;
		created_at = o.created_at;
		updated_at = o.updated_at;
		messages = clone(o.messages);
		is_streaming = o.is_streaming;
	}
	return *this;
}

void ChatSession::Jsonize(JsonIO& io)
{
	io("id", id);
	io("title", title);
	io("model", model);
	io("pair_mode", pair_mode);
	io("context_repo", context_repo);
	io("context_file", context_file);
	io("system_prompt", system_prompt);
	io("created_at", created_at);
	io("updated_at", updated_at);
	io("messages", messages);
	io("is_streaming", is_streaming);
}

String ChatSession::ToJSON(bool pretty) const
{
	ChatSession copy = *this;
	return StoreAsJson(copy, pretty);
}

bool ChatSession::FromJSON(const String& json)
{
	return LoadFromJson(*this, json);
}

ChatMessage* ChatSession::FindMessage(const String& msg_id)
{
	for(int i = 0; i < messages.GetCount(); i++)
		if(messages[i].id == msg_id)
			return &messages[i];
	return NULL;
}

// ---------------------------------------------------------------------------

ChatSessionManager::ChatSessionManager()
{
	InitDefaultSession();
}

ChatSessionManager& ChatSessionManager::Get()
{
	static ChatSessionManager s_mgr;
	return s_mgr;
}

void ChatSessionManager::InitDefaultSession()
{
	CreateSession("Pair Programming on ai-upp",
	              "nvidia/nemotron-3.5-super-vl",
	              "pair-programmer",
	              "ai-upp",
	              "uppsrc/Manager/main.cpp");
}

ChatSession& ChatSessionManager::CreateSession(const String& title,
                                               const String& model,
                                               const String& pair_mode,
                                               const String& repo,
                                               const String& file)
{
	Mutex::Lock __(mutex);
	String id = "sess-" + AsString((int64)GetSysTime().Get()) + "-" + AsString((int)Random(10000));
	ChatSession& s = sessions.GetAdd(id);
	s.id = id;
	s.title = title.IsEmpty() ? "New Pair-Programming Session" : title;
	s.model = model.IsEmpty() ? "nvidia/nemotron-3.5-super-vl" : model;
	s.pair_mode = pair_mode.IsEmpty() ? "pair-programmer" : pair_mode;
	s.context_repo = repo;
	s.context_file = file;
	s.created_at = GetSysTime().Get();
	s.updated_at = s.created_at;
	s.is_streaming = false;

	// Initial system introduction message
	ChatMessage intro;
	intro.id = Format("msg-%d-0", (int64)s.created_at);
	intro.role = "assistant";
	intro.sender = "AI Pair Programmer";
	intro.content = String("Hello! I am your AI pair-programming partner for `") + (repo.IsEmpty() ? String("this workspace") : repo) + "`.\n\n"
	                "You can discuss architecture, review diffs, draft implementations, or instruct me to make changes.\n"
	                "What would you like to work on together today?";
	intro.timestamp = s.created_at;
	intro.status = "complete";
	s.messages.Add(intro);

	return s;
}

ChatSession* ChatSessionManager::GetSession(const String& id)
{
	Mutex::Lock __(mutex);
	int q = sessions.Find(id);
	return q >= 0 ? &sessions[q] : NULL;
}

const ChatSession* ChatSessionManager::GetSession(const String& id) const
{
	int q = sessions.Find(id);
	return q >= 0 ? &sessions[q] : NULL;
}

Vector<ChatSession> ChatSessionManager::ListSessions() const
{
	Vector<ChatSession> res;
	for(int i = 0; i < sessions.GetCount(); i++)
		res.Add(sessions[i]);
	return res;
}

bool ChatSessionManager::DeleteSession(const String& id)
{
	Mutex::Lock __(mutex);
	int q = sessions.Find(id);
	if(q >= 0) {
		sessions.Remove(q);
		return true;
	}
	return false;
}

ChatMessage& ChatSessionManager::AddUserMessage(const String& session_id, const String& content)
{
	Mutex::Lock __(mutex);
	ChatSession* s = GetSession(session_id);
	if(!s) {
		s = &CreateSession("Pair Programming Session");
	}

	s->updated_at = GetSysTime().Get();
	ChatMessage msg;
	msg.id = Format("msg-%d-%d", (int64)s->updated_at, s->messages.GetCount() + 1);
	msg.role = "user";
	msg.sender = "Human Operator";
	msg.content = content;
	msg.timestamp = s->updated_at;
	msg.status = "complete";
	s->messages.Add(msg);
	return s->messages.Top();
}

ChatMessage& ChatSessionManager::StartAssistantMessage(const String& session_id, const String& sender)
{
	Mutex::Lock __(mutex);
	ChatSession* s = GetSession(session_id);
	if(!s) {
		s = &CreateSession("Pair Programming Session");
	}

	s->updated_at = GetSysTime().Get();
	s->is_streaming = true;

	ChatMessage msg;
	msg.id = Format("msg-%d-%d", (int64)s->updated_at, s->messages.GetCount() + 1);
	msg.role = "assistant";
	msg.sender = sender.IsEmpty() ? "Pair Programmer" : sender;
	msg.content = "";
	msg.timestamp = s->updated_at;
	msg.status = "streaming";
	s->messages.Add(msg);
	return s->messages.Top();
}

bool ChatSessionManager::AppendAssistantDelta(const String& session_id, const String& delta)
{
	Mutex::Lock __(mutex);
	ChatSession* s = GetSession(session_id);
	if(!s || s->messages.IsEmpty()) return false;
	ChatMessage& top = s->messages.Top();
	if(top.role == "assistant") {
		top.content.Cat(delta);
		return true;
	}
	return false;
}

bool ChatSessionManager::CompleteAssistantMessage(const String& session_id, const String& final_content, const String& thought)
{
	Mutex::Lock __(mutex);
	ChatSession* s = GetSession(session_id);
	if(!s || s->messages.IsEmpty()) return false;
	s->is_streaming = false;
	ChatMessage& top = s->messages.Top();
	if(top.role == "assistant") {
		if(!final_content.IsEmpty())
			top.content = final_content;
		top.thought = thought;
		top.status = "complete";
		return true;
	}
	return false;
}

ChatSession& ChatSessionManager::ForkSession(const String& session_id, const String& message_id)
{
	Mutex::Lock __(mutex);
	ChatSession* orig = GetSession(session_id);
	String new_id = "sess-" + AsString((int64)GetSysTime().Get()) + "-" + AsString((int)Random(10000));
	ChatSession& s = sessions.GetAdd(new_id);
	s.id = new_id;
	s.created_at = GetSysTime().Get();
	s.updated_at = s.created_at;
	s.is_streaming = false;

	if(orig) {
		s.title = orig->title + " (Fork)";
		s.model = orig->model;
		s.pair_mode = orig->pair_mode;
		s.context_repo = orig->context_repo;
		s.context_file = orig->context_file;
		s.system_prompt = orig->system_prompt;
		for(int i = 0; i < orig->messages.GetCount(); i++) {
			s.messages.Add(orig->messages[i]);
			if(orig->messages[i].id == message_id)
				break;
		}
	} else {
		s.title = "Forked Session";
	}
	return s;
}

bool ChatSessionManager::RevertToMessage(const String& session_id, const String& message_id)
{
	Mutex::Lock __(mutex);
	ChatSession* s = GetSession(session_id);
	if(!s) return false;
	int target_idx = -1;
	for(int i = 0; i < s->messages.GetCount(); i++) {
		if(s->messages[i].id == message_id) {
			target_idx = i;
			break;
		}
	}
	if(target_idx >= 0) {
		s->messages.Trim(target_idx + 1);
		s->updated_at = GetSysTime().Get();
		return true;
	}
	return false;
}

bool ChatSessionManager::DeleteMessage(const String& session_id, const String& message_id)
{
	Mutex::Lock __(mutex);
	ChatSession* s = GetSession(session_id);
	if(!s) return false;
	for(int i = 0; i < s->messages.GetCount(); i++) {
		if(s->messages[i].id == message_id) {
			s->messages.Remove(i);
			s->updated_at = GetSysTime().Get();
			return true;
		}
	}
	return false;
}

END_UPP_NAMESPACE
