#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Log.h"
#include "common/Text.h"
#include <cstring>

using namespace EuroScopePlugIn;

namespace
{
    constexpr COLORREF MISMATCH_COLOR = RGB(255, 165, 0);
    constexpr const char *MISSING_SQUAWK_TEXT = "SQ?";
}

void StripCol::OnGetTagItem(CFlightPlan FlightPlan,
                            CRadarTarget RadarTarget,
                            int ItemCode,
                            int,
                            char sItemString[16],
                            int *pColorCode,
                            COLORREF *pRGB,
                            double *)
{
    if (!squawkAssignmentEnabled_ || ItemCode != TAG_ITEM_SQUAWK || !FlightPlan.IsValid())
        return;

    const std::string assigned = Text::FromCString(FlightPlan.GetControllerAssignedData().GetSquawk());
    if (assigned.empty())
    {
        strcpy_s(sItemString, 16, MISSING_SQUAWK_TEXT);
        return;
    }

    strcpy_s(sItemString, 16, assigned.c_str());

    if (!RadarTarget.IsValid())
        return;

    const char *transponder = RadarTarget.GetPosition().GetSquawk();
    if (transponder && transponder[0] != '\0' && assigned != transponder)
    {
        *pColorCode = TAG_COLOR_RGB_DEFINED;
        *pRGB = MISMATCH_COLOR;
    }
}

void StripCol::OnFunctionCall(int FunctionId, const char *sItemString, POINT, RECT Area)
{
    if (!squawkAssignmentEnabled_ || FunctionId != TAG_FUNC_ASSIGN_SQUAWK)
        return;

    CFlightPlan flightPlan = FlightPlanSelectASEL();
    if (!flightPlan.IsValid())
        return;

    const std::string entered = Text::FromCString(sItemString);
    if (entered.empty())
    {
        OpenPopupEdit(Area, FunctionId, flightPlan.GetControllerAssignedData().GetSquawk());
        return;
    }

    if (flightPlan.GetState() != FLIGHT_PLAN_STATE_ASSUMED || !Text::IsValidSquawk(entered))
        return;

    flightPlan.GetControllerAssignedData().SetSquawk(entered.c_str());
    tracker_.MarkDirty(flightPlan.GetCallsign());
    Log::Write("Squawk " + entered + " set on " + flightPlan.GetCallsign());
}
