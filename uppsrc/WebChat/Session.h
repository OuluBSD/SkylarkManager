#ifndef _WebChat_Session_h_
#define _WebChat_Session_h_

#include <Core/Core.h>

NAMESPACE_UPP

struct ChatMessage : Moveable<ChatMessage> {
	String id;
	String role;       // "user", "assistant", "system", "tool"
	String sender;     // "Human Operator", "Pair Programmer", "AI Agent"
	String content;    // text or markdown
	String thought;    // reasoning trace
	int64  timestamp;  // epoch seconds
	String status;     // "streaming", "complete", "error"

	ChatMessage();
	ChatMessage(const ChatMessage& o);
	ChatMessage& operator=(const ChatMessage& o);

	void Jsonize(JsonIO& io);
	String ToJSON(bool pretty = true) const;
	bool FromJSON(const String& json);
};

struct ChatSession : Moveable<ChatSession> {
	String              id;
	String              title;
	String              model;
	String              pair_mode;     // "pair-programmer", "code-reviewer", "architect", "debugger"
	String              context_repo;  // e.g. "ai-upp"
	String              context_file;  // e.g. "uppsrc/Manager/main.cpp"
	String              system_prompt;
	int64               created_at;
	int64               updated_at;
	Vector<ChatMessage> messages;
	bool                is_streaming;

	ChatSession();
	ChatSession(const ChatSession& o);
	ChatSession& operator=(const ChatSession& o);

	void Jsonize(JsonIO& io);
	String ToJSON(bool pretty = true) const;
	bool FromJSON(const String& json);

	ChatMessage* FindMessage(const String& msg_id);
};

class ChatSessionManager {
public:
	ChatSessionManager();

	ChatSession& CreateSession(const String& title = "",
	                           const String& model = "nvidia/nemotron-3.5-super-vl",
	                           const String& pair_mode = "pair-programmer",
	                           const String& repo = "ai-upp",
	                           const String& file = "");

	ChatSession* GetSession(const String& id);
	const ChatSession* GetSession(const String& id) const;
	Vector<ChatSession> ListSessions() const;
	bool DeleteSession(const String& id);

	ChatMessage& AddUserMessage(const String& session_id, const String& content);
	ChatMessage& StartAssistantMessage(const String& session_id, const String& sender = "Pair Programmer");
	bool AppendAssistantDelta(const String& session_id, const String& delta);
	bool CompleteAssistantMessage(const String& session_id, const String& final_content, const String& thought = "");

	ChatSession& ForkSession(const String& session_id, const String& message_id);
	bool RevertToMessage(const String& session_id, const String& message_id);
	bool DeleteMessage(const String& session_id, const String& message_id);

	static ChatSessionManager& Get();

private:
	Mutex                         mutex;
	ArrayMap<String, ChatSession> sessions;

	void InitDefaultSession();
};

END_UPP_NAMESPACE

#endif
