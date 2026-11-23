#include "pch.h"
#include "plugin/StripCol.h"
#include "flightplan/FlightPlanManager.h"
#include "common/Utils.h"
#include "common/Constants.h"
#include "network/WebSocketClient.h"

using namespace EuroScopePlugIn;

StripCol::StripCol()
    : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE,
              StripColConstants::PLUGIN_NAME.c_str(),
              StripColConstants::PLUGIN_VERSION.c_str(),
              StripColConstants::PLUGIN_AUTHOR.c_str(),
              StripColConstants::PLUGIN_COPYRIGHT.c_str())
{
    wsClient = std::make_unique<WebSocketClient>(gatewayAddress, "3000",
                                                 [this](const std::string &msg)
                                                 { this->HandleWebSocketMessage(msg); });

    if (WebSocketClient::CheckAvailability(gatewayAddress, "3000"))
    {
        Utils::LogMessage("Gateway available at " + gatewayAddress);
    }
    else
    {
        Utils::LogMessage("Gateway not available - will retry on position connect");
    }

    if (FlightPlanManager::LoadProcedures("data/procedures.json"))
    {
        Utils::LogMessage("Procedures configuration loaded successfully.");
    }
    else
    {
        Utils::LogMessage("Failed to load data/procedures.json configuration.");
    }

    if (enableSquawkAssignment)
    {
        RegisterTagItemType("Squawk Assignment", TAG_ITEM_SQUAWK);
        RegisterTagItemFunction("Assign Squawk", TAG_FUNC_ASSIGN_SQUAWK);
    }
}

StripCol::~StripCol()
{
    DisconnectFromGateway();
}

void StripCol::OnControllerPositionUpdate(CController Controller)
{
    CController myself = ControllerMyself();
    if (!myself.IsValid() || std::string(Controller.GetCallsign()) != std::string(myself.GetCallsign()))
        return;

    if (wsClient->IsRunning())
    {
        std::string currentRegistered;
        {
            std::lock_guard<std::mutex> lock(codeMutex);
            currentRegistered = registeredCallsign;
        }
        if (currentRegistered == std::string(myself.GetCallsign()))
            return;

        DisconnectFromGateway();
        {
            std::lock_guard<std::mutex> lock(codeMutex);
            pairingCode.clear();
            registeredCallsign.clear();
        }
        std::lock_guard<std::mutex> lock(aircraftMutex);
        assumedAircraft.clear();
        lastAircraftData.clear();
    }

    connectionRequested = true;
}

void StripCol::OnTimer(int Counter)
{
    if (connectionRequested && !wsClient->IsRunning())
    {
        connectionRequested = false;
        ConnectToGateway();
    }

    ProcessPendingTasks();
    CheckAllFlightPlans();

    {
        std::unordered_set<std::string> toProcess;
        {
            std::lock_guard<std::mutex> lock(dirtyMutex);
            if (!dirtyAircraft.empty())
            {
                toProcess.swap(dirtyAircraft);
            }
        }

        for (const auto &callsign : toProcess)
        {
            CFlightPlan fp = FlightPlanSelect(callsign.c_str());
            if (fp.IsValid())
            {
                int state = fp.GetState();
                HandleTransferState(fp, callsign, state);
                HandleAssumedState(fp, callsign, state);
            }
        }
    }

    if (Counter % StripColConstants::ATCLIST_SYNC_INTERVAL_TICKS == 0)
    {
        SendAtcList();
    }
}

void StripCol::OnControllerDisconnect(CController Controller)
{
    std::string dcCallsign = Controller.GetCallsign();
    {
        std::lock_guard<std::mutex> lock(codeMutex);
        if (dcCallsign != registeredCallsign)
            return;
    }

    Utils::LogMessage("My position disconnected, stopping plugin...");
    DisconnectFromGateway();
    {
        std::lock_guard<std::mutex> lock(codeMutex);
        pairingCode.clear();
        registeredCallsign.clear();
    }
    std::lock_guard<std::mutex> lock(aircraftMutex);
    assumedAircraft.clear();
    lastAircraftData.clear();
}
