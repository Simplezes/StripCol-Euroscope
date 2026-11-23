#pragma once

#include "EuroScopePlugIn.h"
#include "gateway/GatewayLink.h"
#include "sync/AircraftTracker.h"
#include "sync/AtcListSync.h"

struct CommandContext
{
    EuroScopePlugIn::CPlugIn &plugin;
    GatewayLink &link;
    AircraftTracker &tracker;
    AtcListSync &atcList;
};
