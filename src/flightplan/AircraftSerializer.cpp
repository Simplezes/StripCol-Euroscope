#include "pch.h"
#include "flightplan/AircraftSerializer.h"
#include "common/Constants.h"
#include "common/Text.h"
#include "gateway/JsonFields.h"
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using Text::FromCString;

namespace AircraftSerializer
{
    namespace
    {
        constexpr int MINUTES_PER_DAY = 1440;

        using RouteMinutes = std::unordered_map<std::string, int>;

        RouteMinutes ReadRouteMinutes(EuroScopePlugIn::CFlightPlanExtractedRoute &route)
        {
            RouteMinutes minutes;
            const int count = route.GetPointsNumber();
            for (int i = 0; i < count; i++)
            {
                const char *name = route.GetPointName(i);
                if (name && name[0])
                    minutes.emplace(name, route.GetPointDistanceInMinutes(i));
            }
            return minutes;
        }

        std::string FormatEta(int minuteOfDay, int minutesAhead)
        {
            const int total = minuteOfDay + minutesAhead;
            char buffer[8];
            snprintf(buffer, sizeof(buffer), "%02d%02d", (total / 60) % 24, total % 60);
            return buffer;
        }

        json MakePoint(const std::string &name, const RouteMinutes &minutes, int minuteOfDay)
        {
            auto found = minutes.find(name);
            std::string eta;
            if (found != minutes.end() && found->second >= 0)
                eta = FormatEta(minuteOfDay, found->second);
            return {{"name", name}, {"eta", eta}};
        }

        json MakePoints(const std::vector<std::string> &waypoints, const RouteMinutes &minutes, int minuteOfDay)
        {
            json points = json::array();
            for (const std::string &name : waypoints)
                points.push_back(MakePoint(name, minutes, minuteOfDay));
            return points;
        }

        void AppendRemainingRoutePoints(json &points,
                                        EuroScopePlugIn::CFlightPlanExtractedRoute &route,
                                        const RouteMinutes &minutes,
                                        std::unordered_set<std::string> &listed,
                                        int minuteOfDay)
        {
            const int count = route.GetPointsNumber();
            for (int i = 0; i < count; i++)
            {
                const char *name = route.GetPointName(i);
                if (!name || !name[0] || !listed.insert(name).second)
                    continue;

                auto found = minutes.find(name);
                if (found == minutes.end() || found->second < 0)
                    continue;

                points.push_back({{"name", std::string(name)}, {"eta", FormatEta(minuteOfDay, found->second)}});
            }
        }

        std::string EstimateArrival(EuroScopePlugIn::CFlightPlanExtractedRoute &route, int minuteOfDay)
        {
            const int last = route.GetPointsNumber() - 1;
            if (last < 0)
                return "";

            const int minutesAhead = route.GetPointDistanceInMinutes(last);
            return minutesAhead >= 0 ? FormatEta(minuteOfDay, minutesAhead) : "";
        }

        json ReadRoutePoints(EuroScopePlugIn::CFlightPlanExtractedRoute &route, int minuteOfDay, const PassedTimes &passed)
        {
            json points = json::array();
            const int count = route.GetPointsNumber();
            for (int i = 0; i < count; i++)
            {
                const char *name = route.GetPointName(i);
                if (!name || !name[0])
                    continue;

                const int minutesAhead = route.GetPointDistanceInMinutes(i);
                auto found = passed.find(name);
                points.push_back({{"name", std::string(name)},
                                  {"eta", minutesAhead >= 0 ? FormatEta(minuteOfDay, minutesAhead) : ""},
                                  {"ato", found != passed.end() ? found->second : ""}});
            }
            return points;
        }

        std::string SingleCharacter(char value)
        {
            return std::string(1, value);
        }

        json ReadAnnotations(EuroScopePlugIn::CFlightPlanControllerAssignedData &assigned)
        {
            json annotations = json::array();
            for (int i = 0; i < StripColConstants::FLIGHT_STRIP_ANNOTATION_COUNT; i++)
                annotations.push_back(FromCString(assigned.GetFlightStripAnnotation(i)));
            return annotations;
        }
    }

    std::string FormatClock(std::time_t now)
    {
        return FormatEta(static_cast<int>((now / 60) % MINUTES_PER_DAY), 0);
    }

    std::string Serialize(EuroScopePlugIn::CFlightPlan flightPlan,
                          const ProcedureCatalog &catalog,
                          std::time_t now,
                          const PassedTimes &passed)
    {
        if (!flightPlan.IsValid())
            return "{}";

        EuroScopePlugIn::CFlightPlanData data = flightPlan.GetFlightPlanData();
        EuroScopePlugIn::CFlightPlanControllerAssignedData assigned = flightPlan.GetControllerAssignedData();
        EuroScopePlugIn::CFlightPlanExtractedRoute route = flightPlan.GetExtractedRoute();

        const std::string origin = FromCString(data.GetOrigin());
        const std::string destination = FromCString(data.GetDestination());
        const std::string departureRunway = FromCString(data.GetDepartureRwy());
        const std::string arrivalRunway = FromCString(data.GetArrivalRwy());
        const std::string sidName = FromCString(data.GetSidName());
        const std::string starName = FromCString(data.GetStarName());

        const int minuteOfDay = static_cast<int>((now / 60) % MINUTES_PER_DAY);
        const RouteMinutes minutes = ReadRouteMinutes(route);

        const auto &sidWaypoints = catalog.Waypoints(origin, ProcedureType::Sid, departureRunway, sidName);
        const auto &starWaypoints = catalog.Waypoints(destination, ProcedureType::Star, arrivalRunway, starName);

        json departurePoints = MakePoints(sidWaypoints, minutes, minuteOfDay);
        json arrivalPoints = MakePoints(starWaypoints, minutes, minuteOfDay);

        std::unordered_set<std::string> listed(sidWaypoints.begin(), sidWaypoints.end());
        listed.insert(starWaypoints.begin(), starWaypoints.end());
        AppendRemainingRoutePoints(arrivalPoints, route, minutes, listed, minuteOfDay);

        json aircraft;
        aircraft["callsign"] = FromCString(flightPlan.GetCallsign());
        aircraft["aircraftType"] = FromCString(data.GetAircraftFPType());
        aircraft["groundSpeed"] = data.GetTrueAirspeed();
        aircraft["departure"] = origin;
        aircraft["arrival"] = destination;
        aircraft["squawk"] = FromCString(assigned.GetSquawk());
        aircraft["atd"] = FromCString(data.GetActualDepartureTime());
        aircraft["sid"] = sidName;
        aircraft["star"] = starName;
        aircraft["departureRwy"] = departureRunway;
        aircraft["arrivalRwy"] = arrivalRunway;
        aircraft["finalAltitude"] = flightPlan.GetFinalAltitude();
        aircraft["directTo"] = FromCString(assigned.GetDirectToPointName());
        aircraft["route"] = FromCString(data.GetRoute());
        aircraft["remarks"] = FromCString(data.GetRemarks());
        aircraft["groundState"] = FromCString(flightPlan.GetGroundState());
        aircraft["clearanceReceived"] = flightPlan.GetClearenceFlag();
        aircraft["departurePoints"] = std::move(departurePoints);
        aircraft["arrivalPoints"] = std::move(arrivalPoints);
        aircraft["routePoints"] = ReadRoutePoints(route, minuteOfDay, passed);
        aircraft["clearedAltitude"] = assigned.GetClearedAltitude();
        aircraft["assignedHeading"] = assigned.GetAssignedHeading();
        aircraft["assignedMach"] = assigned.GetAssignedMach() / StripColConstants::MACH_SCALE;
        aircraft["assignedSpeed"] = assigned.GetAssignedSpeed();
        aircraft["entryPoint"] = FromCString(flightPlan.GetEntryCoordinationPointName());
        aircraft["exitPoint"] = FromCString(flightPlan.GetExitCoordinationPointName());
        aircraft["nextCopxPoint"] = FromCString(flightPlan.GetNextCopxPointName());
        aircraft["nextFirCopxPoint"] = FromCString(flightPlan.GetNextFirCopxPointName());
        aircraft["planType"] = FromCString(data.GetPlanType());
        aircraft["aircraftInfo"] = FromCString(data.GetAircraftInfo());
        aircraft["wtc"] = SingleCharacter(data.GetAircraftWtc());
        aircraft["engineNumber"] = data.GetEngineNumber();
        aircraft["engineType"] = SingleCharacter(data.GetEngineType());
        aircraft["capabilities"] = SingleCharacter(data.GetCapibilities());
        aircraft["rvsm"] = data.IsRvsm();
        aircraft["alternate"] = FromCString(data.GetAlternate());
        aircraft["communicationType"] = SingleCharacter(data.GetCommunicationType());
        aircraft["estimatedDepartureTime"] = FromCString(data.GetEstimatedDepartureTime());
        aircraft["estimatedArrival"] = EstimateArrival(route, minuteOfDay);
        aircraft["enrouteHours"] = FromCString(data.GetEnrouteHours());
        aircraft["enrouteMinutes"] = FromCString(data.GetEnrouteMinutes());
        aircraft["fuelHours"] = FromCString(data.GetFuelHours());
        aircraft["fuelMinutes"] = FromCString(data.GetFuelMinutes());
        aircraft["assignedFinalAltitude"] = assigned.GetFinalAltitude();
        aircraft["assignedRate"] = assigned.GetAssignedRate();
        aircraft["scratchPad"] = FromCString(assigned.GetScratchPadString());
        aircraft["assignedCommunicationType"] = SingleCharacter(assigned.GetCommunicationType());
        aircraft["flightStripAnnotations"] = ReadAnnotations(assigned);
        aircraft["trackingControllerId"] = FromCString(flightPlan.GetTrackingControllerId());
        aircraft["handoffTargetControllerId"] = FromCString(flightPlan.GetHandoffTargetControllerId());

        return JsonFields::Dump(aircraft);
    }
}
