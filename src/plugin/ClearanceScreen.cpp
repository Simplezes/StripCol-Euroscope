#include "pch.h"
#include "plugin/ClearanceScreen.h"
#include "common/Log.h"
#include <algorithm>

using namespace EuroScopePlugIn;

ClearanceScreen::ClearanceScreen(std::vector<ClearanceScreen *> &registry) : registry_(registry)
{
    registry_.push_back(this);
    Log::Write("Radar screen registered, total: " + std::to_string(registry_.size()));
}

void ClearanceScreen::OnAsrContentToBeClosed()
{
    registry_.erase(std::remove(registry_.begin(), registry_.end(), this), registry_.end());
    Log::Write("Radar screen closed, remaining: " + std::to_string(registry_.size()));
    delete this;
}

void ClearanceScreen::ToggleClearedFlag(CPlugIn &plugin, CFlightPlan flightPlan)
{
    CFlightPlan previousSelection = plugin.FlightPlanSelectASEL();
    plugin.SetASELAircraft(flightPlan);

    const POINT noPoint = {0, 0};
    const RECT noArea = {0, 0, 0, 0};
    StartTagFunction(flightPlan.GetCallsign(), nullptr, 0, "", nullptr, TAG_ITEM_FUNCTION_SET_CLEARED_FLAG, noPoint,
                     noArea);

    if (previousSelection.IsValid())
        plugin.SetASELAircraft(previousSelection);
}
