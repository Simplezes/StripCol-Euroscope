#pragma once

#include "EuroScopePlugIn.h"
#include "gateway/GatewayLink.h"
#include "plugin/ClearanceScreen.h"
#include "sync/AircraftTracker.h"
#include "sync/AtcListSync.h"

struct CommandContext
{
    EuroScopePlugIn::CPlugIn &plugin;
    GatewayLink &link;
    AircraftTracker &tracker;
    AtcListSync &atcList;
    ClearanceScreen *screen;
};
