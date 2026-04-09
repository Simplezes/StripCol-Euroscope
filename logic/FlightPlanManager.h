#pragma once

#include "EuroScopePlugIn.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace FlightPlanManager
{
    bool LoadProcedures(const std::string &filename, const std::string &baseUrl = "", const std::string &apiKey = "");
    std::string BuildAircraftJson(EuroScopePlugIn::CFlightPlan fp, bool customClearance = false);
    bool SetSidForFlight(EuroScopePlugIn::CFlightPlan fp, const std::string &sidName, const std::string &runway, std::string &messageOut);
    bool SetStarForFlight(EuroScopePlugIn::CFlightPlan fp, const std::string &starName, const std::string &runway, std::string &messageOut);
    bool IsProcedurePattern(const std::string &segment);
}
