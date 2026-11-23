#pragma once

#include "EuroScopePlugIn.h"
#include <vector>

class ClearanceScreen : public EuroScopePlugIn::CRadarScreen
{
public:
    explicit ClearanceScreen(std::vector<ClearanceScreen *> &registry);

    void OnAsrContentToBeClosed() override;
    void ToggleClearedFlag(EuroScopePlugIn::CPlugIn &plugin, EuroScopePlugIn::CFlightPlan flightPlan);

private:
    std::vector<ClearanceScreen *> &registry_;
};
