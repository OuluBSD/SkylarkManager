#include "WebEngine.h"
#include "Host.h"

NAMESPACE_UPP

// ---------------------------------------------------------------------------
// Gateway catch-alls. Stock routes always win over these (Skylark best-match
// prefers more specific paths); anything unmatched reaches the engine, which
// resolves the site from the listener port. Defined in Host.cpp so the object
// file is retained by any reference to WebEngineHost.
// ---------------------------------------------------------------------------

SKYLARK(EngineGatewayGet, "**")          { TheWebEngine().Gateway(http); }
SKYLARK(EngineGatewayPost, "**:POST_RAW"){ TheWebEngine().Gateway(http); }
SKYLARK(EngineGatewayPut, "**:PUT")      { TheWebEngine().Gateway(http); }
SKYLARK(EngineGatewayPatch, "**:PATCH")  { TheWebEngine().Gateway(http); }
SKYLARK(EngineGatewayHead, "**:HEAD")    { TheWebEngine().Gateway(http); }
SKYLARK(EngineGatewayOptions, "**:OPTIONS") { TheWebEngine().Gateway(http); }

// ---------------------------------------------------------------------------
// WebEngineHost
// ---------------------------------------------------------------------------

WebEngineHost::WebEngineHost()
{
	// The engine host is a genuine single process: no preforked children,
	// because engine acceptor threads and broker state must not be forked.
	prefork = 0;
}

void WebEngineHost::BindSite(int port, const String& site_id)
{
	ASSERT_(port > 0 && port != this->port, "engine listener port must be positive and differ from the stock port");
	Listener* L = new Listener;
	L->port = port;
	listeners.Add(L);
	TheWebEngine().BindPort(port, site_id);
}

void WebEngineHost::BindDefaultSite(const String& site_id)
{
	TheWebEngine().BindDefaultPort(port, site_id);
}

void WebEngineHost::StartEngines()
{
	if(engines_started)
		return;
	engines_started = true;
	LOG("WebEngineHost: starting " << listeners.GetCount() << " engine listeners");
	stopping = 0;
	for(int i = 0; i < listeners.GetCount(); i++) {
		Listener& L = *listeners[i];
		IpAddrInfo ipinfo;
		ipinfo.Execute(ip, L.port, IpAddrInfo::FAMILY_IPV4);
		if(!L.server.Listen(ipinfo, L.port, 5, false, true)) {
			LOG("WebEngineHost: cannot listen on port " << L.port << ": " << L.server.GetErrorDesc());
			Exit(1);
		}
		listener_threads.Add().Run(THISBACK1(ListenerLoop, i));
	}
}

void WebEngineHost::StopEngines()
{
	stopping = 1;
	for(Listener* L : listeners) {
		TcpSocket poke;
		poke.Timeout(100);
		poke.Connect("127.0.0.1", L->port);
		poke.Close();
	}
}

void WebEngineHost::WorkThread()
{
	// FinalizeViews has run by the time worker threads start (Run() calls it
	// before Main()), so handler registration is complete and it is safe to
	// open the engine listeners.
	ONCELOCK
		StartEngines();
	RunThread();
}

void WebEngineHost::ListenerLoop(int listener_index)
{
	Listener& L = *listeners[listener_index];
	for(;;) {
		TcpSocket request;
		L.accept_mutex.Enter();
		if(stopping) {
			L.accept_mutex.Leave();
			break;
		}
		bool ok = request.Accept(L.server);
		L.accept_mutex.Leave();
		if(stopping)
			break;
		if(ok) {
			// The client sends its request bytes right after the TCP
			// handshake; without a read timeout a zero-wait header read
			// would close the socket before they arrive.
			request.Timeout(30000);
			WebEngine::SetCurrentListenerPort(L.port);
			Http http(*this);
			http.Dispatch(request);
		}
		else
			LOG("WebEngineHost: engine accept failed on port " << L.port << ": " << request.GetErrorDesc());
	}
}

END_UPP_NAMESPACE
