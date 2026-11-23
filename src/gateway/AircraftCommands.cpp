#include "pch.h"
#include "plugin/StripCol.h"
#include "flightplan/FlightPlanManager.h"
#include <nlohmann/json.hpp>

using namespace EuroScopePlugIn;
using json = nlohmann::json;

namespace
{
    constexpr int MAX_ASSIGNED_SPEED = 500;
    constexpr double MACH_SCALE = 100.0;
    constexpr double NEARBY_AIRCRAFT_RANGE_NM = 50.0;
}

CFlightPlan StripCol::SelectFlightPlan(const std::string &jsonStr)
{
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    if (callsign.empty())
        return CFlightPlan();

    return FlightPlanSelect(callsign.c_str());
}

void StripCol::ApplyIntegerCommand(const std::string &jsonStr, const char *key,
                                   const std::function<void(CFlightPlan &, int)> &apply)
{
    std::string valueText = GetJsonValue(jsonStr, key);
    if (valueText.empty())
        return;

    try
    {
        int value = std::stoi(valueText);
        CFlightPlan fp = SelectFlightPlan(jsonStr);
        if (fp.IsValid())
            apply(fp, value);
    }
    catch (...)
    {
    }
}

void StripCol::HandleSetClearedAltitude(const std::string &jsonStr)
{
    ApplyIntegerCommand(jsonStr, "clearedAltitude", [](CFlightPlan &fp, int altitude)
                        { fp.GetControllerAssignedData().SetClearedAltitude(altitude); });
}

void StripCol::HandleSetAssignedHeading(const std::string &jsonStr)
{
    ApplyIntegerCommand(jsonStr, "assignedHeading", [](CFlightPlan &fp, int heading)
                        { fp.GetControllerAssignedData().SetAssignedHeading(heading); });
}

void StripCol::HandleSetAssignedSpeed(const std::string &jsonStr)
{
    ApplyIntegerCommand(jsonStr, "assignedSpeed", [](CFlightPlan &fp, int speed)
                        {
        if (speed >= 0 && speed <= MAX_ASSIGNED_SPEED)
            fp.GetControllerAssignedData().SetAssignedSpeed(speed); });
}

void StripCol::HandleSetFinalAltitude(const std::string &jsonStr)
{
    ApplyIntegerCommand(jsonStr, "finalAltitude", [](CFlightPlan &fp, int altitude)
                        { fp.GetControllerAssignedData().SetFinalAltitude(altitude); });
}

void StripCol::HandleSetAssignedMach(const std::string &jsonStr)
{
    std::string machText = GetJsonValue(jsonStr, "assignedMach");
    if (machText.empty())
        return;

    try
    {
        int scaledMach = static_cast<int>(std::stod(machText) * MACH_SCALE);
        CFlightPlan fp = SelectFlightPlan(jsonStr);
        if (fp.IsValid())
            fp.GetControllerAssignedData().SetAssignedMach(scaledMach);
    }
    catch (...)
    {
    }
}

void StripCol::HandleSetSquawk(const std::string &jsonStr)
{
    std::string squawk = GetJsonValue(jsonStr, "squawk");
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (!squawk.empty() && fp.IsValid())
        fp.GetControllerAssignedData().SetSquawk(squawk.c_str());
}

void StripCol::HandleSetDepartureTime(const std::string &jsonStr)
{
    std::string departureTime = GetJsonValue(jsonStr, "Dtime");
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (!departureTime.empty() && fp.IsValid())
        fp.GetFlightPlanData().SetActualDepartureTime(departureTime.c_str());
}

void StripCol::HandleSetDirectPoint(const std::string &jsonStr)
{
    std::string pointName = GetJsonValue(jsonStr, "pointName");
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (pointName.empty() || !fp.IsValid())
        return;

    CFlightPlanExtractedRoute route = fp.GetExtractedRoute();
    for (int index = 0; index < route.GetPointsNumber(); index++)
    {
        const char *name = route.GetPointName(index);
        if (name && pointName == name)
        {
            fp.GetControllerAssignedData().SetDirectToPointName(pointName.c_str());
            return;
        }
    }
}

void StripCol::HandleSetSid(const std::string &jsonStr)
{
    std::string sid = GetJsonValue(jsonStr, "sid");
    std::string runway = GetJsonValue(jsonStr, "runway");
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (sid.empty() || !fp.IsValid())
        return;

    std::string message;
    FlightPlanManager::SetSidForFlight(fp, sid, runway, message);
}

void StripCol::HandleSetStar(const std::string &jsonStr)
{
    std::string star = GetJsonValue(jsonStr, "star");
    std::string runway = GetJsonValue(jsonStr, "runway");
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (star.empty() || !fp.IsValid())
        return;

    std::string message;
    FlightPlanManager::SetStarForFlight(fp, star, runway, message);
}

void StripCol::HandleAcceptHandoff(const std::string &jsonStr)
{
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (fp.IsValid())
        fp.AcceptHandoff();
}

void StripCol::HandleRefuseHandoff(const std::string &jsonStr)
{
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (fp.IsValid())
        fp.RefuseHandoff();
}

void StripCol::HandleAtcTransfer(const std::string &jsonStr)
{
    std::string targetController = GetJsonValue(jsonStr, "targetATC");
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (!targetController.empty() && fp.IsValid())
        fp.InitiateHandoff(targetController.c_str());
}

void StripCol::HandleAssumeAircraft(const std::string &jsonStr)
{
    CFlightPlan fp = SelectFlightPlan(jsonStr);
    if (fp.IsValid() && fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED)
        fp.StartTracking();
}

void StripCol::HandleEndTracking(const std::string &jsonStr)
{
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    if (callsign.empty())
        return;

    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid())
        fp.EndTracking();

    {
        std::lock_guard<std::mutex> lock(aircraftMutex);
        assumedAircraft.erase(callsign);
        lastAircraftData.erase(callsign);
    }

    json release;
    release["type"] = "release";
    release["callsign"] = callsign;
    wsClient->SendMessage(release.dump());
}

void StripCol::HandleGetNearbyAircraft(const std::string &jsonStr)
{
    CController myself = ControllerMyself();
    if (!myself.IsValid())
        return;

    std::vector<std::string> callsigns;
    CPosition myPosition = myself.GetPosition();

    for (CFlightPlan fp = FlightPlanSelectFirst(); fp.IsValid(); fp = FlightPlanSelectNext(fp))
    {
        int state = fp.GetState();
        if (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED ||
            state == EuroScopePlugIn::FLIGHT_PLAN_STATE_REDUNDANT)
            continue;

        CRadarTarget target = RadarTargetSelect(fp.GetCallsign());
        if (!target.IsValid())
            continue;

        double distance = myPosition.DistanceTo(target.GetPosition().GetPosition());
        if (distance <= NEARBY_AIRCRAFT_RANGE_NM)
            callsigns.push_back(fp.GetCallsign());
    }

    json response;
    response["type"] = "nearby-aircraft";
    response["data"] = callsigns;
    wsClient->SendMessage(response.dump());
}
