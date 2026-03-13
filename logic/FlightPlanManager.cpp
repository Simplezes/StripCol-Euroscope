#include "pch.h"
#include "FlightPlanManager.h"
#include <algorithm>
#include <ctime>

namespace FlightPlanManager {

    bool IsProcedurePattern(const std::string& segment) {
        bool hasNumbers = segment.find_first_of("0123456789") != std::string::npos;
        bool hasLetters = segment.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos;
        return hasNumbers && hasLetters;
    }

    std::string BuildAircraftJson(EuroScopePlugIn::CFlightPlan fp) {
        if (!fp.IsValid()) return "{}";

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
        j["clearedFlag"] = fp.GetClearenceFlag() ? 1 : 0;

        const char* origin = data.GetOrigin();
        const char* destination = data.GetDestination();

        auto isNotificationPoint = [&](const char* name) {
            if (!name || strlen(name) <= 1) return false;
            if (origin && strcmp(name, origin) == 0) return false;
            if (destination && strcmp(name, destination) == 0) return false;

            for (const char* p = name; *p; ++p) {
                if (isdigit((unsigned char)*p)) return false;
            }
            return true;
        };

        auto getPointsList = [&](const std::vector<std::pair<std::string, int>>& points) {
            json arr = json::array();
            time_t now = time(nullptr);
            for (const auto& p : points) {
                int totalMinutes = (int)((now / 60) % 1440) + p.second;
                char timeStr[8];
                snprintf(timeStr, sizeof(timeStr), "%02d%02d", (totalMinutes / 60) % 24, totalMinutes % 60);
                
                json point;
                point["name"] = p.first;
                point["eta"] = timeStr;
                arr.push_back(point);
            }
            return arr;
        };

        int pointsCount = route.GetPointsNumber();
        int originIdx = -1, destIdx = -1;

        for (int i = 0; i < pointsCount; i++) {
            const char* name = route.GetPointName(i);
            if (name) {
                if (originIdx == -1 && origin && strcmp(name, origin) == 0) originIdx = i;
                if (destination && strcmp(name, destination) == 0) destIdx = i;
            }
        }

        if (originIdx == -1) originIdx = 0;
        if (destIdx == -1) destIdx = pointsCount - 1;

        std::vector<std::pair<std::string, int>> upcomingPoints;
        for (int i = 0; i < pointsCount && upcomingPoints.size() < 5; i++) {
            int minutes = route.GetPointDistanceInMinutes(i);
            if (minutes >= 0) {
                const char* name = route.GetPointName(i);
                if (isNotificationPoint(name)) {
                    upcomingPoints.emplace_back(name, minutes);
                }
            }
        }

        std::vector<std::pair<std::string, int>> arrivalPoints;
        for (int i = destIdx - 1; i > originIdx && arrivalPoints.size() < 5; i--) {
            const char* name = route.GetPointName(i);
            if (isNotificationPoint(name)) {
                arrivalPoints.emplace_back(name, route.GetPointDistanceInMinutes(i));
            }
        }
        std::reverse(arrivalPoints.begin(), arrivalPoints.end());

        j["departurePoints"] = getPointsList(upcomingPoints);
        j["arrivalPoints"] = getPointsList(arrivalPoints);

        j["clearedAltitude"] = assignedData.GetClearedAltitude();
        j["assignedHeading"] = assignedData.GetAssignedHeading();
        j["assignedMach"] = assignedData.GetAssignedMach() / 100.0;
        j["assignedSpeed"] = assignedData.GetAssignedSpeed();
        j["entryPoint"] = fp.GetEntryCoordinationPointName() ? fp.GetEntryCoordinationPointName() : "";
        j["exitPoint"] = fp.GetExitCoordinationPointName() ? fp.GetExitCoordinationPointName() : "";
        j["nextCopxPoint"] = fp.GetNextCopxPointName() ? fp.GetNextCopxPointName() : "";
        j["nextFirCopxPoint"] = fp.GetNextFirCopxPointName() ? fp.GetNextFirCopxPointName() : "";

        return j.dump();
    }

    bool SetSidForFlight(EuroScopePlugIn::CFlightPlan fp, const std::string& sidName, const std::string& runway, std::string& messageOut) {
        if (!fp.IsValid()) return false;
        if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) return false;

        auto fpd = fp.GetFlightPlanData();
        std::string route = fpd.GetRoute() ? fpd.GetRoute() : "";

        size_t firstSpace = route.find(' ');
        if (firstSpace != std::string::npos) {
            std::string firstSegment = route.substr(0, firstSpace);
            if (IsProcedurePattern(firstSegment)) {
                size_t slashPos = firstSegment.find('/');
                if (slashPos != std::string::npos) {
                    route = route.substr(firstSpace + 1);
                } else {
                    size_t secondSpace = route.find(' ', firstSpace + 1);
                    if (secondSpace != std::string::npos) {
                        std::string secondSegment = route.substr(firstSpace + 1, secondSpace - firstSpace - 1);
                        if (IsProcedurePattern(secondSegment)) route = route.substr(secondSpace + 1);
                        else route = route.substr(firstSpace + 1);
                    } else {
                        route = route.substr(firstSpace + 1);
                    }
                }
                size_t firstValidChar = route.find_first_not_of(" ,");
                if (firstValidChar != std::string::npos) route = route.substr(firstValidChar);
                else route.clear();
            }
        }

        std::string fullSid = sidName + "/" + runway;
        route = route.empty() ? fullSid : fullSid + " " + route;

        if (fpd.SetRoute(route.c_str())) return fpd.AmendFlightPlan();
        return false;
    }

    bool SetStarForFlight(EuroScopePlugIn::CFlightPlan fp, const std::string& starName, const std::string& runway, std::string& messageOut) {
        if (!fp.IsValid()) return false;
        if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) return false;

        auto fpd = fp.GetFlightPlanData();
        std::string route = fpd.GetRoute() ? fpd.GetRoute() : "";

        size_t lastSpace = route.find_last_of(' ');
        if (lastSpace != std::string::npos) {
            std::string lastSegment = route.substr(lastSpace + 1);
            if (IsProcedurePattern(lastSegment)) {
                size_t slashPos = lastSegment.find('/');
                if (slashPos != std::string::npos) {
                    route = route.substr(0, lastSpace);
                } else {
                    size_t prevSpace = route.find_last_of(' ', lastSpace - 1);
                    if (prevSpace != std::string::npos) {
                        std::string prevSegment = route.substr(prevSpace + 1, lastSpace - prevSpace - 1);
                        if (IsProcedurePattern(prevSegment)) route = route.substr(0, prevSpace);
                        else route = route.substr(0, lastSpace);
                    } else route = route.substr(0, lastSpace);
                }
                size_t lastValidChar = route.find_last_not_of(" ,");
                if (lastValidChar != std::string::npos) route = route.substr(0, lastValidChar + 1);
                else route.clear();
            }
        }

        std::string fullStar = starName + "/" + runway;
        route = route.empty() ? fullStar : route + " " + fullStar;

        if (fpd.SetRoute(route.c_str())) return fpd.AmendFlightPlan();
        return false;
    }
}
