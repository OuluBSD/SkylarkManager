#ifndef _SkylarkGuiExample_SkylarkGuiExample_h
#define _SkylarkGuiExample_SkylarkGuiExample_h

#include <CtrlLib/CtrlLib.h>
#include "WebInterface.h"

using namespace Upp;

#if __has_include(<examples/SkylarkGuiExample/SkylarkGuiExample.lay>)
#define LAYOUTFILE <examples/SkylarkGuiExample/SkylarkGuiExample.lay>
#elif __has_include(<SkylarkGuiExample/SkylarkGuiExample.lay>)
#define LAYOUTFILE <SkylarkGuiExample/SkylarkGuiExample.lay>
#else
#define LAYOUTFILE <SkylarkGuiExample.lay>
#endif
#if __has_include(<Ctrl/Core/lay.h>)
#include <Ctrl/Core/lay.h>
#else
#include <CtrlCore/lay.h>
#endif

class SkylarkGuiExample : public WithSkylarkGuiExampleLayout<TopWindow> {
	
	static Callback1<int> WhenSignal;
	WebInterface web;
	
	static void SignalHandler(int signal);

public:
	typedef SkylarkGuiExample CLASSNAME;
	void Run();
	SkylarkGuiExample();
};

#endif
