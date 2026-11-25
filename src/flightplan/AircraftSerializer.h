#pragma once

#include "EuroScopePlugIn.h"
#include "flightplan/ProcedureCatalog.h"
#include <ctime>
#include <string>
#include <unordered_map>

namespace AircraftSerializer
{
    using PassedTimes = std::unordered_map<std::string, std::string>;

    std::string FormatClock(std::time_t now);

    std::string Serialize(EuroScopePlugIn::CFlightPlan flightPlan,
                          const ProcedureCatalog &catalog,
                          std::time_t now,
                          const PassedTimes &passed);
}
