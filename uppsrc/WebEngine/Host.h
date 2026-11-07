#ifndef _WebEngine_Host_h_
#define _WebEngine_Host_h_

#include <Core/Core.h>
#include <Skylark/Skylark.h>
#include "Engine.h"

NAMESPACE_UPP

// Skylark application subclass hosting the engine: the stock listener keeps
// serving the main port; one acceptor thread group is started per bound
// engine port. All sockets are dispatched through the normal Skylark
// Http::Dispatch machinery.
class WebEngineHost : public SkylarkApp {
public:
	typedef WebEngineHost CLASSNAME;

	WebEngineHost();

	WebEngine& Engine() { return TheWebEngine(); }

	// Bind an extra listener port to a site. Listeners open when the first
	// worker thread starts (after FinalizeViews).
	void BindSite(int port, const String& site_id);
	// Bind the stock (main) port to a site.
	void BindDefaultSite(const String& site_id);

	// Start listener threads manually if not using Run() (e.g. tests).
	void StartEngines();
	// Stop engine acceptor threads (the stock app keeps its own lifecycle).
	void StopEngines();

	virtual void WorkThread() override;

private:
	struct Listener {
		TcpSocket   server;
		int         port = 0;
		Mutex  accept_mutex;
	};

	void ListenerLoop(int listener_index);

	Vector<Listener*> listeners;
	Array<Thread>     listener_threads;
	Atomic            stopping;
	bool              engines_started = false;
};

END_UPP_NAMESPACE

#endif
