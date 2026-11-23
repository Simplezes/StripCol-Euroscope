#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Utils.h"
#include <algorithm>

using namespace EuroScopePlugIn;

void StripCol::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan,
                            EuroScopePlugIn::CRadarTarget RadarTarget,
                            int ItemCode,
                            int TagData,
                            char sItemString[16],
                            int *pColorCode,
                            COLORREF *pRGB,
                            double *pFontSize)
{

    if (!enableSquawkAssignment)
        return;

    if (ItemCode == TAG_ITEM_SQUAWK)
    {
        if (!FlightPlan.IsValid())
            return;

        const char *assr = FlightPlan.GetControllerAssignedData().GetSquawk();
        const char *pssr = RadarTarget.IsValid() ? RadarTarget.GetPosition().GetSquawk() : "";

        if (assr && strlen(assr) > 0)
        {
            strcpy_s(sItemString, 16, assr);
            if (pssr && strlen(pssr) > 0 && strcmp(assr, pssr) != 0)
            {
                *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
                *pRGB = RGB(255, 165, 0);
            }
        }
        else
        {
            strcpy_s(sItemString, 16, "SQ?");
        }
    }
}

void StripCol::OnFunctionCall(int FunctionId,
                              const char *sItemString,
                              POINT Pt,
                              RECT Area)
{

    if (!enableSquawkAssignment)
        return;

    if (FunctionId == TAG_FUNC_ASSIGN_SQUAWK)
    {
        if (sItemString && strlen(sItemString) > 0)
        {
            CFlightPlan fp = FlightPlanSelectASEL();
            if (fp.IsValid() && fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED)
            {
                std::string code = sItemString;
                if (code.length() == 4 && std::all_of(code.begin(), code.end(), [](char c)
                                                      { return c >= '0' && c <= '7'; }))
                {
                    fp.GetControllerAssignedData().SetSquawk(code.c_str());
                    Utils::LogMessage("Manually assigned squawk " + code + " to " + fp.GetCallsign());
                }
            }
        }
        else
        {
            CFlightPlan fp = FlightPlanSelectASEL();
            const char *current = fp.IsValid() ? fp.GetControllerAssignedData().GetSquawk() : "";
            OpenPopupEdit(Area, FunctionId, current);
        }
    }
}
