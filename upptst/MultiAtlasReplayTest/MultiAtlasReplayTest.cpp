#include <Core/Core.h>

NAMESPACE_UPP
void RunMultiAtlasReplayTests();
void RunMultiAtlasSchedulerTests();
void RunMultiAtlasReplayEngineTests();
void RunMultiAtlasWarningTests();
END_UPP_NAMESPACE

CONSOLE_APP_MAIN
{
	using namespace Upp;
	RunMultiAtlasReplayTests();
	RunMultiAtlasSchedulerTests();
	RunMultiAtlasReplayEngineTests();
	RunMultiAtlasWarningTests();
}
