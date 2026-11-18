#include <Core/Core.h>

NAMESPACE_UPP
void RunMultiAtlasReplayTests();
void RunMultiAtlasSchedulerTests();
END_UPP_NAMESPACE

CONSOLE_APP_MAIN
{
	using namespace Upp;
	RunMultiAtlasReplayTests();
	RunMultiAtlasSchedulerTests();
}
