#include "ChatRoutes.h"

NAMESPACE_UPP

SKYLARK(WebChatPage, "webchat")
{
	String session_id = http["session"];
	http.ContentType("text/html; charset=utf-8") << WebChatView::RenderPage(session_id);
}

SKYLARK(ApiChatSessions, "api/chat/sessions")
{
	ChatSessionManager& mgr = ChatSessionManager::Get();

	ValueArray arr;
	Vector<ChatSession> list = mgr.ListSessions();
	for(int i = 0; i < list.GetCount(); i++)
		arr.Add(ParseJSON(list[i].ToJSON()));

	ValueMap root;
	root.Add("sessions", arr);
	http.ContentType("application/json") << AsJSON(root, true);
}

SKYLARK(ApiChatCreateSession, "api/chat/sessions/create:POST_RAW")
{
	ChatSessionManager& mgr = ChatSessionManager::Get();

	String req = http.GetRequestContent();
	Value v = ParseJSON(req);
	String title = v["title"];
	String model = v["model"];
	String pair_mode = v["pair_mode"];
	String repo = v["repo"];
	String file = v["file"];

	ChatSession& s = mgr.CreateSession(title, model, pair_mode, repo, file);
	ValueMap res;
	res.Add("success", true);
	res.Add("session", ParseJSON(s.ToJSON()));
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiChatMessage, "api/chat/message:POST_RAW")
{
	ChatSessionManager& mgr = ChatSessionManager::Get();
	String req = http.GetRequestContent();
	Value v = ParseJSON(req);

	String session_id = v["session_id"];
	String content = v["content"];

	if(session_id.IsEmpty() || content.IsEmpty()) {
		ValueMap err;
		err.Add("error", "Missing session_id or content");
		http.ContentType("application/json") << AsJSON(err, true);
		return;
	}

	ChatMessage& msg = mgr.AddUserMessage(session_id, content);
	ValueMap res;
	res.Add("success", true);
	res.Add("message", ParseJSON(msg.ToJSON()));
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiChatStream, "api/chat/stream/*")
{
	String session_id = http[0];
	String prompt = http["prompt"];
	String model = http["model"];
	String role = http["role"];

	if(role.IsEmpty()) role = "Pair Programmer";

	TcpSocket* sock = http.DetachSocket();
	if(sock) {
		ChatStreamDispatcher::StreamToSocket(sock, session_id, prompt, model, role);
	}
}

SKYLARK(ApiChatFork, "api/chat/fork:POST_RAW")
{
	ChatSessionManager& mgr = ChatSessionManager::Get();
	String req = http.GetRequestContent();
	Value v = ParseJSON(req);
	String session_id = v["session_id"];
	String message_id = v["message_id"];

	ChatSession& forked = mgr.ForkSession(session_id, message_id);
	ValueMap res;
	res.Add("success", true);
	res.Add("session", ParseJSON(forked.ToJSON()));
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiChatRevert, "api/chat/revert:POST_RAW")
{
	ChatSessionManager& mgr = ChatSessionManager::Get();
	String req = http.GetRequestContent();
	Value v = ParseJSON(req);
	String session_id = v["session_id"];
	String message_id = v["message_id"];

	bool ok = mgr.RevertToMessage(session_id, message_id);
	ValueMap res;
	res.Add("success", ok);
	http.ContentType("application/json") << AsJSON(res, true);
}

SKYLARK(ApiChatDeleteMessage, "api/chat/message/delete:POST_RAW")
{
	ChatSessionManager& mgr = ChatSessionManager::Get();
	String req = http.GetRequestContent();
	Value v = ParseJSON(req);
	String session_id = v["session_id"];
	String message_id = v["message_id"];

	bool ok = mgr.DeleteMessage(session_id, message_id);
	ValueMap res;
	res.Add("success", ok);
	http.ContentType("application/json") << AsJSON(res, true);
}

END_UPP_NAMESPACE
