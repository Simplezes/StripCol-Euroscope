#pragma once

#include "EuroScopePlugIn.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct SquawkRange {
    std::string icao;
    std::string fir;
    int nationalStart;
    int nationalEnd;
    int internationalStart;
    int internationalEnd;
};

class SquawkManager {
public:
    SquawkManager();
    bool LoadSquawks(const std::string& filename);
    
    std::string AssignSquawk(EuroScopePlugIn::CFlightPlan fp, EuroScopePlugIn::CPlugIn* pPlugin);
    
    bool IsNational(EuroScopePlugIn::CFlightPlan fp);

    bool IsModeSCapable(EuroScopePlugIn::CFlightPlan fp);

    bool IsModeSDetected(EuroScopePlugIn::CFlightPlan fp);

    bool GetRange(const std::string& adep, bool national, SquawkRange& outRange);

private:
    std::vector<SquawkRange> m_squawkRanges;
    std::unordered_map<std::string, std::string> m_airportToFir;

    std::string FormatSquawk(int code);
    bool IsSquawkUsed(const std::string& squawk, EuroScopePlugIn::CPlugIn* pPlugin);
};
