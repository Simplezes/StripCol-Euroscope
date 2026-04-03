#include "pch.h"
#include "SquawkManager.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include "../StripCol.h"

SquawkManager::SquawkManager() {
}

extern HMODULE g_hModule;

bool SquawkManager::LoadSquawks(const std::string& filename) {
    try {
        std::string fullPath = filename;
        char dllPath[MAX_PATH];
        if (GetModuleFileNameA(g_hModule, dllPath, MAX_PATH) != 0) {
            std::string path(dllPath);
            std::string dir = path.substr(0, path.find_last_of("\\/"));
            fullPath = dir + "\\" + filename;
        }

        std::ifstream file(fullPath);
        if (!file.is_open()) return false;

        json j;
        file >> j;

        m_squawkRanges.clear();
        m_airportToFir.clear();

        if (j.contains("airports") && j["airports"].is_array()) {
            for (const auto& entry : j["airports"]) {
                SquawkRange range;
                range.icao = entry.value("icao", "");
                range.fir = entry.value("fir", "");
                
                if (entry.contains("national") && entry["national"].is_array()) {
                    if (entry["national"].size() >= 2 && entry["national"][0].is_number()) {
                        range.national.push_back(SquawkCodeRange{entry["national"][0].get<int>(), entry["national"][1].get<int>()});
                    } else {
                        for (const auto& r : entry["national"]) {
                            if (r.is_array() && r.size() >= 2) {
                                range.national.push_back(SquawkCodeRange{r[0].get<int>(), r[1].get<int>()});
                            }
                        }
                    }
                }

                if (entry.contains("international") && entry["international"].is_array()) {
                    if (entry["international"].size() >= 2 && entry["international"][0].is_number()) {
                        range.international.push_back(SquawkCodeRange{entry["international"][0].get<int>(), entry["international"][1].get<int>()});
                    } else {
                        for (const auto& r : entry["international"]) {
                            if (r.is_array() && r.size() >= 2) {
                                range.international.push_back(SquawkCodeRange{r[0].get<int>(), r[1].get<int>()});
                            }
                        }
                    }
                }

                m_squawkRanges.push_back(range);
                if (!range.icao.empty() && !range.fir.empty()) {
                    m_airportToFir[range.icao] = range.fir;
                }
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool SquawkManager::IsNational(EuroScopePlugIn::CFlightPlan fp) {
    if (!fp.IsValid()) return false;

    std::string adep = fp.GetFlightPlanData().GetOrigin();
    std::string ades = fp.GetFlightPlanData().GetDestination();

    if (m_airportToFir.count(adep) && m_airportToFir.count(ades)) {
        return m_airportToFir[adep] == m_airportToFir[ades];
    }

    return false;
}

bool SquawkManager::IsModeSCapable(EuroScopePlugIn::CFlightPlan fp) {
    if (!fp.IsValid()) return false;

    char gear = fp.GetFlightPlanData().GetCapibilities();
    if (gear == 'S' || gear == 'H' || gear == 'E' || gear == 'L') return true;
    return false;
}

bool SquawkManager::IsModeSDetected(EuroScopePlugIn::CFlightPlan fp) {
    if (!fp.IsValid()) return false;
    auto rt = fp.GetCorrelatedRadarTarget();
    if (!rt.IsValid()) return false;
    
    return (rt.GetPosition().GetRadarFlags() & 4) != 0;
}

bool SquawkManager::GetRange(const std::string& adep, bool national, SquawkRange& outRange) {
    for (const auto& range : m_squawkRanges) {
        if (range.icao == adep) {
            outRange = range;
            return true;
        }
    }
    return false;
}

std::string SquawkManager::FormatSquawk(int code) {
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(4) << code;
    return ss.str();
}

bool SquawkManager::IsSquawkUsed(const std::string& squawk, EuroScopePlugIn::CPlugIn* pPlugin) {
    StripCol* stripColPlugin = dynamic_cast<StripCol*>(pPlugin);
    if (stripColPlugin && stripColPlugin->IsSquawkUsedGlobally(squawk)) {
        return true;
    }

    for (auto fp = pPlugin->FlightPlanSelectFirst(); fp.IsValid(); fp = pPlugin->FlightPlanSelectNext(fp)) {
        if (std::string(fp.GetControllerAssignedData().GetSquawk()) == squawk) return true;
    }

    for (auto rt = pPlugin->RadarTargetSelectFirst(); rt.IsValid(); rt = pPlugin->RadarTargetSelectNext(rt)) {
        if (std::string(rt.GetPosition().GetSquawk()) == squawk) return true;
    }

    return false;
}

std::string SquawkManager::AssignSquawk(EuroScopePlugIn::CFlightPlan fp, EuroScopePlugIn::CPlugIn* pPlugin) {
    if (!fp.IsValid()) return "";

    if (IsModeSCapable(fp) || IsModeSDetected(fp)) {
        return "1000";
    }
    bool national = IsNational(fp);
    std::string adep = fp.GetFlightPlanData().GetOrigin();
    
    SquawkRange range;
    if (GetRange(adep, national, range)) {
        const auto& ranges = (national && !range.national.empty()) ? range.national : range.international;

        for (const auto& r : ranges) {
            for (int code = r.start; code <= r.end; ++code) {
                int d1 = (code / 1000) % 10;
                int d2 = (code / 100) % 10;
                int d3 = (code / 10) % 10;
                int d4 = code % 10;

                if (d1 > 7 || d2 > 7 || d3 > 7 || d4 > 7) continue;

                std::string s = FormatSquawk(code);
                if (!IsSquawkUsed(s, pPlugin)) {
                    return s;
                }
            }
        }
    }

    return "";
}
