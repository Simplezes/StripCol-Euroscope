#pragma once

#include "EuroScopePlugIn.h"
#include <string>

namespace RouteEditor
{
    bool IsProcedurePattern(const std::string &segment);

    std::string WithSid(const std::string &route, const std::string &sid, const std::string &runway);
    std::string WithStar(const std::string &route, const std::string &star, const std::string &runway);

    bool ApplySid(EuroScopePlugIn::CFlightPlan flightPlan, const std::string &sid, const std::string &runway);
    bool ApplyStar(EuroScopePlugIn::CFlightPlan flightPlan, const std::string &star, const std::string &runway);
}
