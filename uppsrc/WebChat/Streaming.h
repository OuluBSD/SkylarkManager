#ifndef _WebChat_Streaming_h_
#define _WebChat_Streaming_h_

#include <Core/Core.h>
#include <SweAgent/SweAgent.h>
#include "Session.h"

NAMESPACE_UPP

class ChatStreamDispatcher {
public:
	ChatStreamDispatcher();

	// Dispatches generation for a session:
	// If sock is provided, streams SSE events directly to client in real-time.
	// Also appends tokens to ChatSessionManager.
	static void StreamToSocket(TcpSocket* sock,
	                           const String& session_id,
	                           const String& user_prompt,
	                           const String& model = "",
	                           const String& role = "Pair Programmer");

	// Generates a pair-programming response for a given prompt and context
	static String GeneratePairProgrammingReply(const ChatSession& session, const String& user_prompt);
};

END_UPP_NAMESPACE

#endif
