#include "pch.h"
#include "FlightPlanManager.h"
#include <algorithm>
#include <ctime>
#include <fstream>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include "../network/HttpClient.h"

using json = nlohmann::json;

extern HMODULE g_hModule;

namespace FlightPlanManager
{

    static json g_procedures;

    bool LoadProcedures(const std::string &filename, const std::string &baseUrl, const std::string &apiKey)
    {
        if (!baseUrl.empty())
        {
            try
            {
                std::string response = HttpClient::Get(baseUrl + "/files/procedures", apiKey);
                if (!response.empty())
                {
                    json parsed = json::parse(response);
                    g_procedures = std::move(parsed);
                    return true;
                }
            }
            catch (...)
            {
            }
        }

        try
        {
            std::string fullPath = filename;
            char dllPath[MAX_PATH];
            if (GetModuleFileNameA(g_hModule, dllPath, MAX_PATH) != 0)
            {
                std::string path(dllPath);
                std::string dir = path.substr(0, path.find_last_of("\\/"));
                fullPath = dir + "\\" + filename;
            }

            std::ifstream file(fullPath);
            if (!file.is_open())
                return false;

            file >> g_procedures;
        }
        catch (...)
        {
            g_procedures = json::object();
            return false;
        }

        return false;
    }

    static std::vector<std::string> GetProcedureWaypoints(
        const std::string &airport, const std::string &type,
        const std::string &runway, const std::string &procName)
    {
        try
        {
            auto &byRunway = g_procedures.at(airport).at(type).at(runway);
            auto it = byRunway.find(procName);
            if (it != byRunway.end())
                return it->get<std::vector<std::string>>();
            std::string needle = procName;
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            for (auto &[key, val] : byRunway.items())
            {
                std::string k = key;
                std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                if (k == needle)
                    return val.get<std::vector<std::string>>();
            }
        }
        catch (...)
        {
        }
        return {};
    }

    bool IsProcedurePattern(const std::string &segment)
    {
        bool hasNumbers = segment.find_first_of("0123456789") != std::string::npos;
        bool hasLetters = segment.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos;
        return hasNumbers && hasLetters;
    }

    std::string BuildAircraftJson(EuroScopePlugIn::CFlightPlan fp, bool customClearance)
    {
        if (!fp.IsValid())
            return "{}";

        auto data = fp.GetFlightPlanData();
        auto assignedData = fp.GetControllerAssignedData();
        auto route = fp.GetExtractedRoute();

        json j;
        j["callsign"] = fp.GetCallsign();
        j["aircraftType"] = data.GetAircraftFPType() ? data.GetAircraftFPType() : "";
        j["groundSpeed"] = data.GetTrueAirspeed();
        j["departure"] = data.GetOrigin() ? data.GetOrigin() : "";
        j["arrival"] = data.GetDestination() ? data.GetDestination() : "";
        j["squawk"] = assignedData.GetSquawk() ? assignedData.GetSquawk() : "";
        j["atd"] = data.GetActualDepartureTime() ? data.GetActualDepartureTime() : "";
        j["sid"] = data.GetSidName() ? data.GetSidName() : "";
        j["star"] = data.GetStarName() ? data.GetStarName() : "";
        j["departureRwy"] = data.GetDepartureRwy() ? data.GetDepartureRwy() : "";
        j["arrivalRwy"] = data.GetArrivalRwy() ? data.GetArrivalRwy() : "";
        j["finalAltitude"] = fp.GetFinalAltitude();
        j["directTo"] = assignedData.GetDirectToPointName() ? assignedData.GetDirectToPointName() : "";
        j["route"] = data.GetRoute() ? data.GetRoute() : "";
        j["remarks"] = data.GetRemarks() ? data.GetRemarks() : "";
        j["groundState"] = fp.GetGroundState();
        j["clearedFlag"] = (customClearance || fp.GetClearenceFlag()) ? 1 : 0;

        const char *origin = data.GetOrigin();
        const char *destination = data.GetDestination();

        int pointsCount = route.GetPointsNumber();
        std::unordered_map<std::string, int> routePointMinutes;
        for (int i = 0; i < pointsCount; i++)
        {
            const char *name = route.GetPointName(i);
            if (name && name[0] && routePointMinutes.find(name) == routePointMinutes.end())
            {
                routePointMinutes[name] = route.GetPointDistanceInMinutes(i);
            }
        }

        std::string depAirport = origin ? origin : "";
        std::string arrAirport = destination ? destination : "";
        std::string depRwy = data.GetDepartureRwy() ? data.GetDepartureRwy() : "";
        std::string arrRwy = data.GetArrivalRwy() ? data.GetArrivalRwy() : "";
        std::string sidName = data.GetSidName() ? data.GetSidName() : "";
        std::string starName = data.GetStarName() ? data.GetStarName() : "";

        auto depWaypoints = GetProcedureWaypoints(depAirport, "SID", depRwy, sidName);
        auto arrWaypoints = GetProcedureWaypoints(arrAirport, "STAR", arrRwy, starName);

        time_t now = time(nullptr);
        auto buildPointsList = [&](const std::vector<std::string> &waypoints)
        {
            json arr = json::array();
            for (const auto &wp : waypoints)
            {
                auto it = routePointMinutes.find(wp);
                std::string eta;
                if (it != routePointMinutes.end() && it->second >= 0)
                {
                    int totalMinutes = (int)((now / 60) % 1440) + it->second;
                    char timeStr[8];
                    snprintf(timeStr, sizeof(timeStr), "%02d%02d",
                             (totalMinutes / 60) % 24, totalMinutes % 60);
                    eta = timeStr;
                }
                arr.push_back({{"name", wp}, {"eta", eta}});
            }
            return arr;
        };

        json depPoints = buildPointsList(depWaypoints);
        json arrPoints = buildPointsList(arrWaypoints);

        std::unordered_set<std::string> addedPoints(depWaypoints.begin(), depWaypoints.end());
        addedPoints.insert(arrWaypoints.begin(), arrWaypoints.end());

        for (int i = 0; i < pointsCount; i++)
        {
            const char *name = route.GetPointName(i);
            if (!name || !name[0]) continue;
            std::string wpName = name;
            if (!addedPoints.insert(wpName).second) continue;

            auto it = routePointMinutes.find(wpName);
            if (it == routePointMinutes.end() || it->second < 0) continue;

            int totalMinutes = (int)((now / 60) % 1440) + it->second;
            char timeStr[8];
            snprintf(timeStr, sizeof(timeStr), "%02d%02d",
                     (totalMinutes / 60) % 24, totalMinutes % 60);
            arrPoints.push_back({{"name", wpName}, {"eta", std::string(timeStr)}});
        }

        j["departurePoints"] = depPoints;
        j["arrivalPoints"] = arrPoints;
        j["clearedAltitude"] = assignedData.GetClearedAltitude();
        j["assignedHeading"] = assignedData.GetAssignedHeading();
        j["assignedMach"] = assignedData.GetAssignedMach() / 100.0;
        j["assignedSpeed"] = assignedData.GetAssignedSpeed();
        j["entryPoint"] = fp.GetEntryCoordinationPointName() ? fp.GetEntryCoordinationPointName() : "";
        j["exitPoint"] = fp.GetExitCoordinationPointName() ? fp.GetExitCoordinationPointName() : "";
        j["nextCopxPoint"] = fp.GetNextCopxPointName() ? fp.GetNextCopxPointName() : "";
        j["nextFirCopxPoint"] = fp.GetNextFirCopxPointName() ? fp.GetNextFirCopxPointName() : "";
        j["planType"] = data.GetPlanType() ? data.GetPlanType() : "";
        j["aircraftInfo"] = data.GetAircraftInfo() ? data.GetAircraftInfo() : "";
        j["wtc"] = std::string(1, data.GetAircraftWtc());
        j["engineNumber"] = data.GetEngineNumber();
        j["engineType"] = std::string(1, data.GetEngineType());
        j["capabilities"] = std::string(1, data.GetCapibilities());
        j["rvsm"] = data.IsRvsm();
        j["alternate"] = data.GetAlternate() ? data.GetAlternate() : "";
        j["communicationType"] = std::string(1, data.GetCommunicationType());
        j["estimatedDepartureTime"] = data.GetEstimatedDepartureTime() ? data.GetEstimatedDepartureTime() : "";
        j["enrouteHours"] = data.GetEnrouteHours() ? data.GetEnrouteHours() : "";
        j["enrouteMinutes"] = data.GetEnrouteMinutes() ? data.GetEnrouteMinutes() : "";
        j["fuelHours"] = data.GetFuelHours() ? data.GetFuelHours() : "";
        j["fuelMinutes"] = data.GetFuelMinutes() ? data.GetFuelMinutes() : "";
        j["assignedFinalAltitude"] = assignedData.GetFinalAltitude();
        j["assignedRate"] = assignedData.GetAssignedRate();
        j["scratchPad"] = assignedData.GetScratchPadString() ? assignedData.GetScratchPadString() : "";
        j["assignedCommunicationType"] = std::string(1, assignedData.GetCommunicationType());

        json annotations = json::array();
        for (int i = 0; i <= 8; i++)
        {
            const char *ann = assignedData.GetFlightStripAnnotation(i);
            annotations.push_back(ann ? ann : "");
        }

        j["flightStripAnnotations"] = annotations;
        j["trackingControllerId"] = fp.GetTrackingControllerId() ? fp.GetTrackingControllerId() : "";
        j["handoffTargetControllerId"] = fp.GetHandoffTargetControllerId() ? fp.GetHandoffTargetControllerId() : "";

        return j.dump();
    }

    bool SetSidForFlight(EuroScopePlugIn::CFlightPlan fp, const std::string &sidName, const std::string &runway, std::string &messageOut)
    {
        if (!fp.IsValid())
            return false;
        if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED)
            return false;

        auto fpd = fp.GetFlightPlanData();
        std::string route = fpd.GetRoute() ? fpd.GetRoute() : "";

        size_t firstSpace = route.find(' ');
        if (firstSpace != std::string::npos)
        {
            std::string firstSegment = route.substr(0, firstSpace);
            if (IsProcedurePattern(firstSegment))
            {
                size_t slashPos = firstSegment.find('/');
                if (slashPos != std::string::npos)
                {
                    route = route.substr(firstSpace + 1);
                }
                else
                {
                    size_t secondSpace = route.find(' ', firstSpace + 1);
                    if (secondSpace != std::string::npos)
                    {
                        std::string secondSegment = route.substr(firstSpace + 1, secondSpace - firstSpace - 1);
                        if (IsProcedurePattern(secondSegment))
                            route = route.substr(secondSpace + 1);
                        else
                            route = route.substr(firstSpace + 1);
                    }
                    else
                    {
                        route = route.substr(firstSpace + 1);
                    }
                }
                size_t firstValidChar = route.find_first_not_of(" ,");
                if (firstValidChar != std::string::npos)
                    route = route.substr(firstValidChar);
                else
                    route.clear();
            }
        }

        std::string fullSid = sidName + "/" + runway;
        route = route.empty() ? fullSid : fullSid + " " + route;

        if (fpd.SetRoute(route.c_str()))
            return fpd.AmendFlightPlan();
        return false;
    }

    bool SetStarForFlight(EuroScopePlugIn::CFlightPlan fp, const std::string &starName, const std::string &runway, std::string &messageOut)
    {
        if (!fp.IsValid())
            return false;
        if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED)
            return false;

        auto fpd = fp.GetFlightPlanData();
        std::string route = fpd.GetRoute() ? fpd.GetRoute() : "";

        size_t lastSpace = route.find_last_of(' ');
        if (lastSpace != std::string::npos)
        {
            std::string lastSegment = route.substr(lastSpace + 1);
            if (IsProcedurePattern(lastSegment))
            {
                size_t slashPos = lastSegment.find('/');
                if (slashPos != std::string::npos)
                {
                    route = route.substr(0, lastSpace);
                }
                else
                {
                    size_t prevSpace = route.find_last_of(' ', lastSpace - 1);
                    if (prevSpace != std::string::npos)
                    {
                        std::string prevSegment = route.substr(prevSpace + 1, lastSpace - prevSpace - 1);
                        if (IsProcedurePattern(prevSegment))
                            route = route.substr(0, prevSpace);
                        else
                            route = route.substr(0, lastSpace);
                    }
                    else
                        route = route.substr(0, lastSpace);
                }
                size_t lastValidChar = route.find_last_not_of(" ,");
                if (lastValidChar != std::string::npos)
                    route = route.substr(0, lastValidChar + 1);
                else
                    route.clear();
            }
        }

        std::string fullStar = starName + "/" + runway;
        route = route.empty() ? fullStar : route + " " + fullStar;

        if (fpd.SetRoute(route.c_str()))
            return fpd.AmendFlightPlan();
        return false;
    }
}
