#pragma once

#include "EuroScopePlugIn.h"
#include "flightplan/ProcedureCatalog.h"
#include <ctime>
#include <string>

namespace AircraftSerializer
{
    std::string Serialize(EuroScopePlugIn::CFlightPlan flightPlan,
                          const ProcedureCatalog &catalog,
                          std::time_t now);
}
