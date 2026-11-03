#include "Streaming.h"

NAMESPACE_UPP

ChatStreamDispatcher::ChatStreamDispatcher()
{
}

String ChatStreamDispatcher::GeneratePairProgrammingReply(const ChatSession& session, const String& user_prompt)
{
	String p = ToLower(user_prompt);

	String reply;
	if(p.Find("hello") >= 0 || p.Find("hi") >= 0 || p.Find("hey") >= 0) {
		reply << "Hello! Ready to pair program on **" << (session.context_repo.IsEmpty() ? "the project" : session.context_repo) << "**.\n\n"
		      << "Here is what we can do together:\n"
		      << "- **Review code**: Paste snippets or specify files to audit.\n"
		      << "- **Refactor**: Discuss patterns, abstractions, and performance.\n"
		      << "- **Implement features**: Draft designs and generate unit tests.\n\n"
		      << "What area shall we tackle first?";
	}
	else if(p.Find("test") >= 0 || p.Find("check") >= 0) {
		reply << "### Test Plan & Verification\n\n"
		      << "I recommend verifying the implementation using automated self-tests and headless gates:\n\n"
		      << "```bash\n"
		      << "# Build and verify with CLANG\n"
		      << "bin/build -m CLANG reference/ManagerSkylark\n"
		      << "./bin/CLANG/ManagerSkylark --test\n"
		      << "```\n\n"
		      << "Would you like me to generate a dedicated unit test suite for this module?";
	}
	else if(p.Find("refactor") >= 0 || p.Find("clean") >= 0) {
		reply << "### Refactoring Proposal\n\n"
		      << "Let's review the code structure according to the U++ guidelines:\n\n"
		      << "```cpp\n"
		      << "// Moveable container policy: use Moveable<T> and clone()\n"
		      << "struct MyComponent : Moveable<MyComponent> {\n"
		      << "    String id;\n"
		      << "    Vector<String> tags;\n"
		      << "    \n"
		      << "    MyComponent(const MyComponent& o)\n"
		      << "        : id(o.id), tags(clone(o.tags)) {}\n"
		      << "};\n"
		      << "```\n\n"
		      << "This avoids implicit deleted copy constructors and ensures clean BLITZ linkage.\n"
		      << "Shall I apply this refactoring to the target files?";
	}
	else if(p.Find("diff") >= 0 || p.Find("change") >= 0) {
		reply << "### Proposed Changes (Diff Preview)\n\n"
		      << "Here is the proposed diff for discussion:\n\n"
		      << "```diff\n"
		      << "--- a/reference/ManagerSkylark/ManagerSkylark.h\n"
		      << "+++ b/reference/ManagerSkylark/ManagerSkylark.h\n"
		      << "@@ -20,6 +20,8 @@\n"
		      << "+    // WebChat integration\n"
		      << "+    void RegisterChatHandlers();\n"
		      << "+    WebChatSessionManager& GetChat();\n"
		      << "```\n\n"
		      << "Does this patch look good to commit?";
	}
	else {
		reply << "### Pair-Programming Analysis\n\n"
		      << "Regarding: *\"" << user_prompt << "\"*\n\n"
		      << "In this context (`" << (session.context_repo.IsEmpty() ? "ai-upp" : session.context_repo) << "`):\n\n"
		      << "1. **Architecture**: Keeping presentation logic in Skylark while maintaining headless business models in Core.\n"
		      << "2. **Modularity**: Every feature is reusable across both CLI and Web frontends.\n"
		      << "3. **Execution**: Real-time token streaming gives immediate visibility into decisions.\n\n"
		      << "```cpp\n"
		      << "// Next actionable step\n"
		      << "void Proceed() {\n"
		      << "    LOG(\"Executing step with operator guidance...\");\n"
		      << "}\n"
		      << "```\n\n"
		      << "Let me know if you would like to proceed or refine the approach.";
	}
	return reply;
}

void ChatStreamDispatcher::StreamToSocket(TcpSocket* sock,
                                          const String& session_id,
                                          const String& user_prompt,
                                          const String& model,
                                          const String& role)
{
	ChatSession* s = ChatSessionManager::Get().GetSession(session_id);
	if(!s) return;

	if(sock) {
		sock->PutAll("HTTP/1.1 200 OK\r\n"
		             "Content-Type: text/event-stream; charset=utf-8\r\n"
		             "Cache-Control: no-cache\r\n"
		             "Connection: keep-alive\r\n"
		             "Access-Control-Allow-Origin: *\r\n\r\n");
	}

	ChatMessage& msg = ChatSessionManager::Get().StartAssistantMessage(session_id, role);

	if(sock) {
		sock->PutAll(Format("data: {\"type\":\"start\",\"id\":\"%s\",\"role\":\"assistant\",\"sender\":\"%s\"}\n\n",
		                    msg.id, role));
	}

	String full_response = GeneratePairProgrammingReply(*s, user_prompt);

	// Stream response in chunks
	Vector<String> chunks;
	int pos = 0;
	while(pos < full_response.GetCount()) {
		int next_space = full_response.Find(" ", pos);
		int next_newline = full_response.Find("\n", pos);
		int end = -1;
		if(next_space >= 0 && next_newline >= 0)
			end = min(next_space, next_newline);
		else if(next_space >= 0)
			end = next_space;
		else
			end = next_newline;

		if(end >= 0) {
			chunks.Add(full_response.Mid(pos, end - pos + 1));
			pos = end + 1;
		}
		else {
			chunks.Add(full_response.Mid(pos));
			break;
		}
	}

	for(int i = 0; i < chunks.GetCount(); i++) {
		const String& chunk = chunks[i];
		ChatSessionManager::Get().AppendAssistantDelta(session_id, chunk);

		if(sock) {
			sock->PutAll(Format("data: {\"type\":\"token\",\"delta\":%s}\n\n", AsJSON(chunk)));
			Sleep(10); // Natural streaming pace
		}
	}

	ChatSessionManager::Get().CompleteAssistantMessage(session_id, full_response);

	if(sock) {
		sock->PutAll("data: {\"type\":\"done\"}\n\n");
		sock->Close();
	}
}

END_UPP_NAMESPACE
