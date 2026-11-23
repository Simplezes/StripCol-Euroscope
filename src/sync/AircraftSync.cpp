#include "pch.h"
#include "plugin/StripCol.h"
#include "flightplan/FlightPlanManager.h"
#include <nlohmann/json.hpp>

using namespace EuroScopePlugIn;
using json = nlohmann::json;

void StripCol::HandleSync(const std::string &jsonStr)
{
    CController myself = ControllerMyself();
    if (myself.IsValid())
    {
        json j;
        j["type"] = "register";
        {
            std::lock_guard<std::mutex> lock(codeMutex);
            j["code"] = pairingCode;
        }
        j["callsign"] = myself.GetCallsign();
        j["name"] = myself.GetFullName();
        j["facility"] = myself.GetFacility();
        j["rating"] = myself.GetRating();
        j["positionId"] = myself.GetPositionId();
        char freqStr[16];
        snprintf(freqStr, sizeof(freqStr), "%.3f", myself.GetPrimaryFrequency());
        j["frequency"] = freqStr;
        wsClient->SendMessage(j.dump());
    }

    lastAtcListJson = "";
    SendAtcList(true);

    std::lock_guard<std::mutex> lock(aircraftMutex);
    lastAircraftData.clear();
    for (const auto &callsign : assumedAircraft)
    {
        CFlightPlan fp = FlightPlanSelect(callsign.c_str());
        if (fp.IsValid())
        {
            std::string acJson = FlightPlanManager::BuildAircraftJson(fp);
            json update;
            update["type"] = "aircraft";
            update["data"] = json::parse(acJson);

            wsClient->SendMessage(update.dump());
            lastAircraftData[callsign] = acJson;
        }
    }
}

void StripCol::CheckAllFlightPlans()
{
    CFlightPlan fp = FlightPlanSelectFirst();
    while (fp.IsValid())
    {
        std::string callsign = fp.GetCallsign();
        int state = fp.GetState();
        HandleTransferState(fp, callsign, state);
        HandleAssumedState(fp, callsign, state);
        fp = FlightPlanSelectNext(fp);
    }
}

void StripCol::HandleTransferState(CFlightPlan &fp, const std::string &callsign, int state)
{
    std::lock_guard<std::mutex> lock(transferMutex);
    bool inSet = (transferToMeAircraft.find(callsign) != transferToMeAircraft.end());
    bool isTransferring = (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED);

    if (isTransferring && !inSet)
    {
        transferToMeAircraft.insert(callsign);
        json j;
        j["type"] = "transfer";
        j["data"] = json::parse(FlightPlanManager::BuildAircraftJson(fp));
        wsClient->SendMessage(j.dump());
    }
    else if (!isTransferring && inSet)
    {

        transferToMeAircraft.erase(callsign);
    }
}

void StripCol::HandleAssumedState(CFlightPlan &fp, const std::string &callsign, int state)
{
    std::lock_guard<std::mutex> lock(aircraftMutex);
    bool inSet = (assumedAircraft.find(callsign) != assumedAircraft.end());
    bool isAssumed = (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED);

    if (isAssumed && !inSet)
    {
        assumedAircraft.insert(callsign);
        std::string acJson = FlightPlanManager::BuildAircraftJson(fp);
        json j;
        j["type"] = "aircraft";
        j["data"] = json::parse(acJson);

        wsClient->SendMessage(j.dump());
        lastAircraftData[callsign] = acJson;
    }
    else if (!isAssumed && inSet)
    {
        assumedAircraft.erase(callsign);
        json j;
        j["type"] = "release";
        j["callsign"] = callsign;
        wsClient->SendMessage(j.dump());
        lastAircraftData.erase(callsign);
    }
    else if (isAssumed && inSet)
    {
        std::string acJson = FlightPlanManager::BuildAircraftJson(fp);
        if (acJson != lastAircraftData[callsign])
        {
            json j;

            j["type"] = "fpupdate";
            j["data"] = json::parse(acJson);
            wsClient->SendMessage(j.dump());
            lastAircraftData[callsign] = acJson;
        }
    }
}

void StripCol::OnFlightPlanFlightPlanDataUpdate(CFlightPlan fp)
{
    if (!fp.IsValid() || !wsClient->IsConnected())
        return;
    std::string callsign = fp.GetCallsign();
    {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        dirtyAircraft.insert(callsign);
    }
}

void StripCol::OnFlightPlanControllerAssignedDataUpdate(CFlightPlan fp, int DataType)
{
    if (!fp.IsValid() || !wsClient->IsConnected())
        return;
    std::string callsign = fp.GetCallsign();
    {
        std::lock_guard<std::mutex> lock(dirtyMutex);
        dirtyAircraft.insert(callsign);
    }
}

void StripCol::OnFlightPlanDisconnect(CFlightPlan fp)
{
    if (!fp.IsValid() || !wsClient->IsConnected())
        return;
    if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED)
    {
        std::string callsign = fp.GetCallsign();

        json j;

        j["type"] = "release";
        j["callsign"] = callsign;
        wsClient->SendMessage(j.dump());
        std::lock_guard<std::mutex> lock2(aircraftMutex);
        assumedAircraft.erase(callsign);
        lastAircraftData.erase(callsign);
    }
}
