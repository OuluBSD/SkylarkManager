#ifndef _reference_ManagerSkylark_V2Calendar_h_
#define _reference_ManagerSkylark_V2Calendar_h_

#include "V2Model.h"

NAMESPACE_UPP

// The month grid for /calendar: release and goal-node dates that fall in year-month,
// each linked to its page. Returns the card body; the caller wraps it in the page.
String RenderV2CalendarMonth(const V2Store& v2, int year, int month);

END_UPP_NAMESPACE

#endif
