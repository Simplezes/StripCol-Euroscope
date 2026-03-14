#include "pch.h"
#include "StripCol.h"
#include "FlightPlanManager.h"
#include "Utils.h"
#include "Constants.h"
#include <algorithm>

using namespace EuroScopePlugIn;

StripCol::StripCol()
    : CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, 
              StripColConstants::PLUGIN_NAME.c_str(), 
              StripColConstants::PLUGIN_VERSION.c_str(), 
              StripColConstants::PLUGIN_AUTHOR.c_str(), 
              StripColConstants::PLUGIN_COPYRIGHT.c_str()) {
    
    wsClient = std::make_unique<WebSocketClient>(gatewayAddress, "3000", 
        [this](const std::string& msg) { this->HandleWebSocketMessage(msg); });

    if (WebSocketClient::CheckAvailability(gatewayAddress, "3000")) {
        Utils::LogMessage("Gateway available at " + gatewayAddress);
    } else {
        Utils::LogMessage("Gateway not available - will retry on position connect");
    }

    RegisterTagItemType("Simulated Clearance", TAG_ITEM_SIMULATED_CLEARANCE);
    RegisterTagItemFunction("Toggle Clearance", TAG_FUNC_TOGGLE_CLEARANCE);
}

StripCol::~StripCol() {
    DisconnectFromGateway();
}

void StripCol::ConnectToGateway() {
    if (wsClient->IsRunning()) return;

    Utils::LogMessage("ConnectToGateway: Preparing fresh connection...");
    
    CController myself = ControllerMyself();
    if (myself.IsValid()) {
        json j;
        j["type"] = "register";

        {
            std::lock_guard<std::mutex> lock(codeMutex);
            if (pairingCode.empty()) {
                pairingCode = Utils::GeneratePairingCode();
                std::string message = "StripCol Pairing Code: " + pairingCode;
                DisplayUserMessage("StripCol", "Pairing", message.c_str(), true, true, false, false, false);
            }
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

        wsClient->SetRegistrationMessage(j.dump());
    }

    wsClient->Connect();
    Utils::LogMessage("WebSocket client started.");
}

void StripCol::DisconnectFromGateway() {
    wsClient->Disconnect();
}

void StripCol::HandleWebSocketMessage(const std::string& message) {
    try {
        auto j = json::parse(message);
        if (j.contains("type") && j["type"].is_string()) {
            std::string type = j["type"];
            std::lock_guard<std::mutex> lock(queueMutex);
            taskQueue.push({ type, message });
        }
    } catch (const json::parse_error& e) {
        Utils::LogMessage("JSON parse error: " + std::string(e.what()));
    } catch (...) {
        Utils::LogMessage("Error handling command");
    }
}

void StripCol::ProcessPendingTasks() {
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!taskQueue.empty()) {
        PendingTask task = taskQueue.front();
        taskQueue.pop();

        static const std::unordered_map<std::string, void(StripCol::*)(const std::string&)> handlers = {
            {"set-cleared-alt", &StripCol::HandleSetClearedAltitude},
            {"set-assigned-heading", &StripCol::HandleSetAssignedHeading},
            {"set-assigned-speed", &StripCol::HandleSetAssignedSpeed},
            {"set-final-alt", &StripCol::HandleSetFinalAltitude},
            {"accept-handoff", &StripCol::HandleAcceptHandoff},
            {"end-tracking", &StripCol::HandleEndTracking},
            {"set-squawk", &StripCol::HandleSetSquawk},
            {"set-departureTime", &StripCol::HandleSetDepartureTime},
            {"set-direct-point", &StripCol::HandleSetDirectPoint},
            {"set-sid", &StripCol::HandleSetSid},
            {"set-star", &StripCol::HandleSetStar},
            {"set-assigned-mach", &StripCol::HandleSetAssignedMach},
            {"refuse-handoff", &StripCol::HandleRefuseHandoff},
            {"ATC-transfer", &StripCol::HandleAtcTransfer},
            {"sync", &StripCol::HandleSync},
            {"assume-aircraft", &StripCol::HandleAssumeAircraft},
            {"get-nearby-aircraft", &StripCol::HandleGetNearbyAircraft},
            {"set-clearance", &StripCol::HandleSetClearance}
        };

        auto it = handlers.find(task.type);
        if (it != handlers.end()) {
            try {
                (this->*(it->second))(task.json);
            } catch (...) {
                Utils::LogMessage("Exception in handler: " + task.type);
            }
        }
    }
}

std::string StripCol::GetJsonValue(const std::string& jsonStr, const std::string& key) {
    try {
        auto j = json::parse(jsonStr);
        if (j.contains(key)) {
            if (j[key].is_string()) return j[key];
            if (j[key].is_number()) return std::to_string(j[key].get<double>());
            if (j[key].is_boolean()) return j[key].get<bool>() ? "true" : "false";
        }
    } catch (...) {}
    return "";
}

bool StripCol::GetCustomClearance(const std::string& callsign) {
    std::lock_guard<std::mutex> lock(clearanceMutex);
    if (customClearanceFlags.count(callsign)) {
        return customClearanceFlags[callsign];
    }
    return false;
}


// Command Handlers Implementation
void StripCol::HandleSetClearedAltitude(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string altStr = GetJsonValue(jsonStr, "clearedAltitude");
    if (callsign.empty() || altStr.empty()) return;

    try {
        int altitude = std::stoi(altStr);
        CFlightPlan fp = FlightPlanSelect(callsign.c_str());
        if (fp.IsValid()) fp.GetControllerAssignedData().SetClearedAltitude(altitude);
    } catch (...) {}
}

void StripCol::HandleSetAssignedHeading(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string headingStr = GetJsonValue(jsonStr, "assignedHeading");
    if (callsign.empty() || headingStr.empty()) return;

    try {
        int heading = std::stoi(headingStr);
        CFlightPlan fp = FlightPlanSelect(callsign.c_str());
        if (fp.IsValid()) fp.GetControllerAssignedData().SetAssignedHeading(heading);
    } catch (...) {}
}

void StripCol::HandleSetAssignedSpeed(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string speedStr = GetJsonValue(jsonStr, "assignedSpeed");
    if (callsign.empty() || speedStr.empty()) return;

    try {
        int speed = std::stoi(speedStr);
        CFlightPlan fp = FlightPlanSelect(callsign.c_str());
        if (fp.IsValid() && speed >= 0 && speed <= 500) {
            fp.GetControllerAssignedData().SetAssignedSpeed(speed);
        }
    } catch (...) {}
}

void StripCol::HandleSetFinalAltitude(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string altStr = GetJsonValue(jsonStr, "finalAltitude");
    if (callsign.empty() || altStr.empty()) return;

    try {
        int altitude = std::stoi(altStr);
        CFlightPlan fp = FlightPlanSelect(callsign.c_str());
        if (fp.IsValid()) fp.GetControllerAssignedData().SetFinalAltitude(altitude);
    } catch (...) {}
}

void StripCol::HandleAcceptHandoff(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    if (callsign.empty()) return;
    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) fp.AcceptHandoff();
}

void StripCol::HandleEndTracking(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    if (callsign.empty()) return;
    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) fp.EndTracking();

    {
        std::lock_guard<std::mutex> lock(aircraftMutex);
        assumedAircraft.erase(callsign);
        lastAircraftData.erase(callsign);
    }

    json j;
    j["type"] = "release";
    j["callsign"] = callsign;
    wsClient->SendMessage(j.dump());
}

void StripCol::HandleSetSquawk(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string squawkStr = GetJsonValue(jsonStr, "squawk");
    if (callsign.empty() || squawkStr.empty()) return;
    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) fp.GetControllerAssignedData().SetSquawk(squawkStr.c_str());
}

void StripCol::HandleSetDepartureTime(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string dTimeStr = GetJsonValue(jsonStr, "Dtime");
    if (callsign.empty() || dTimeStr.empty()) return;
    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) fp.GetFlightPlanData().SetActualDepartureTime(dTimeStr.c_str());
}

void StripCol::HandleSetAssignedMach(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string machStr = GetJsonValue(jsonStr, "assignedMach");
    if (callsign.empty() || machStr.empty()) return;

    try {
        double mach = std::stod(machStr);
        int machInt = static_cast<int>(mach * 100.0);
        CFlightPlan fp = FlightPlanSelect(callsign.c_str());
        if (fp.IsValid()) fp.GetControllerAssignedData().SetAssignedMach(machInt);
    } catch (...) {}
}

void StripCol::HandleSetDirectPoint(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string pointName = GetJsonValue(jsonStr, "pointName");
    if (callsign.empty() || pointName.empty()) return;

    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) {
        int pointsCount = fp.GetExtractedRoute().GetPointsNumber();
        for (int i = 0; i < pointsCount; i++) {
            const char* name = fp.GetExtractedRoute().GetPointName(i);
            if (name && strcmp(name, pointName.c_str()) == 0) {
                fp.GetControllerAssignedData().SetDirectToPointName(pointName.c_str());
                break;
            }
        }
    }
}

void StripCol::HandleSetSid(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string sid = GetJsonValue(jsonStr, "sid");
    std::string runway = GetJsonValue(jsonStr, "runway");
    if (callsign.empty() || sid.empty()) return;

    std::string msg;
    FlightPlanManager::SetSidForFlight(FlightPlanSelect(callsign.c_str()), sid, runway, msg);
}

void StripCol::HandleSetStar(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string star = GetJsonValue(jsonStr, "star");
    std::string runway = GetJsonValue(jsonStr, "runway");
    if (callsign.empty() || star.empty()) return;

    std::string msg;
    FlightPlanManager::SetStarForFlight(FlightPlanSelect(callsign.c_str()), star, runway, msg);
}

void StripCol::HandleRefuseHandoff(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    if (callsign.empty()) return;
    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) fp.RefuseHandoff();
}

void StripCol::HandleAtcTransfer(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string targetATC = GetJsonValue(jsonStr, "targetATC");
    if (callsign.empty() || targetATC.empty()) return;

    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) fp.InitiateHandoff(targetATC.c_str());
}

void StripCol::HandleAssumeAircraft(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    if (callsign.empty()) return;
    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid() && fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) {
        fp.StartTracking();
    }
}

void StripCol::HandleSetClearance(const std::string& jsonStr) {
    std::string callsign = GetJsonValue(jsonStr, "callsign");
    std::string clearedStr = GetJsonValue(jsonStr, "cleared");
    if (callsign.empty() || clearedStr.empty()) return;

    bool cleared = (clearedStr == "true" || clearedStr == "1");

    {
        std::lock_guard<std::mutex> lock(clearanceMutex);
        customClearanceFlags[callsign] = cleared;
    }

    CFlightPlan fp = FlightPlanSelect(callsign.c_str());
    if (fp.IsValid()) {
    }
}

void StripCol::HandleGetNearbyAircraft(const std::string& jsonStr) {
    std::vector<std::string> callsigns;
    CController myself = ControllerMyself();
    if (!myself.IsValid()) return;

    CPosition myPos = myself.GetPosition();
    CFlightPlan fp = FlightPlanSelectFirst();
    while (fp.IsValid()) {
        if (fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED &&
            fp.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_REDUNDANT) {
            CRadarTarget rt = RadarTargetSelect(fp.GetCallsign());
            if (rt.IsValid()) {
                double dist = myPos.DistanceTo(rt.GetPosition().GetPosition());
                if (dist <= 50.0) callsigns.push_back(fp.GetCallsign());
            }
        }
        fp = FlightPlanSelectNext(fp);
    }

    json root;
    root["type"] = "nearby-aircraft";
    root["data"] = callsigns;
    wsClient->SendMessage(root.dump());
}

void StripCol::HandleSync(const std::string& jsonStr) {
    // Send registration again
    CController myself = ControllerMyself();
    if (myself.IsValid()) {
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
    for (const auto& callsign : assumedAircraft) {
        CFlightPlan fp = FlightPlanSelect(callsign.c_str());
        if (fp.IsValid()) {
            std::string acJson = FlightPlanManager::BuildAircraftJson(fp, GetCustomClearance(callsign));
            json update;
            update["type"] = "aircraft";
            update["data"] = json::parse(acJson);

            wsClient->SendMessage(update.dump());
            lastAircraftData[callsign] = acJson;
        }
    }
}

void StripCol::SendAtcList(bool force) {
    json data = json::array();
    CController ctrl = ControllerSelectFirst();
    while (ctrl.IsValid()) {
        double freq = ctrl.GetPrimaryFrequency();
        char freqBuf[16];
        snprintf(freqBuf, sizeof(freqBuf), "%.3f", freq);

        json j;
        j["callsign"] = ctrl.GetCallsign();
        j["positionId"] = ctrl.GetPositionId();
        j["fullName"] = ctrl.GetFullName();
        j["rating"] = ctrl.GetRating();
        j["frequency"] = freqBuf;
        data.push_back(j);

        ctrl = ControllerSelectNext(ctrl);
    }

    json root;
    root["type"] = "atclist";
    root["data"] = data;
    std::string finalJson = root.dump();

    if (force || finalJson != lastAtcListJson) {
        wsClient->SendMessage(finalJson);
        lastAtcListJson = finalJson;
    }
}

void StripCol::CheckAllFlightPlans() {
    CFlightPlan fp = FlightPlanSelectFirst();
    while (fp.IsValid()) {
        std::string callsign = fp.GetCallsign();
        int state = fp.GetState();
        HandleTransferState(fp, callsign, state);
        HandleAssumedState(fp, callsign, state);
        fp = FlightPlanSelectNext(fp);
    }
}

void StripCol::HandleTransferState(CFlightPlan& fp, const std::string& callsign, int state) {
    std::lock_guard<std::mutex> lock(transferMutex);
    bool inSet = (transferToMeAircraft.find(callsign) != transferToMeAircraft.end());
    bool isTransferring = (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED);

    if (isTransferring && !inSet) {
        transferToMeAircraft.insert(callsign);
        json j;
        j["type"] = "transfer";
        j["data"] = json::parse(FlightPlanManager::BuildAircraftJson(fp, GetCustomClearance(callsign)));
        wsClient->SendMessage(j.dump());
    } else if (!isTransferring && inSet) {

        transferToMeAircraft.erase(callsign);
    }
}

void StripCol::HandleAssumedState(CFlightPlan& fp, const std::string& callsign, int state) {
    std::lock_guard<std::mutex> lock(aircraftMutex);
    bool inSet = (assumedAircraft.find(callsign) != assumedAircraft.end());
    bool isAssumed = (state == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED);

    if (isAssumed && !inSet) {
        assumedAircraft.insert(callsign);
        std::string acJson = FlightPlanManager::BuildAircraftJson(fp, GetCustomClearance(callsign));
        json j;
        j["type"] = "aircraft";
        j["data"] = json::parse(acJson);

        wsClient->SendMessage(j.dump());
        lastAircraftData[callsign] = acJson;
    } else if (!isAssumed && inSet) {
        assumedAircraft.erase(callsign);
        json j;
        j["type"] = "release";
        j["callsign"] = callsign;
        wsClient->SendMessage(j.dump());
        lastAircraftData.erase(callsign);
    } else if (isAssumed && inSet) {
        std::string acJson = FlightPlanManager::BuildAircraftJson(fp, GetCustomClearance(callsign));
        if (acJson != lastAircraftData[callsign]) {
            json j;

            j["type"] = "fpupdate";
            j["data"] = json::parse(acJson);
            wsClient->SendMessage(j.dump());
            lastAircraftData[callsign] = acJson;
        }
    }
}

// Event Handlers
void StripCol::OnControllerPositionUpdate(CController Controller) {
    connectionRequested = true;
}

void StripCol::OnTimer(int Counter) {
    if (connectionRequested && !wsClient->IsRunning()) {
        connectionRequested = false;
        ConnectToGateway();
    }

    ProcessPendingTasks();

    if (wsClient->IsConnected()) {
        CheckAllFlightPlans();
        if (Counter % StripColConstants::ATCLIST_SYNC_INTERVAL_TICKS == 0) {
            SendAtcList();
        }
    }
}

void StripCol::OnControllerDisconnect(CController Controller) {
    Utils::LogMessage("Controller disconnected, stopping plugin...");
    DisconnectFromGateway();
    std::lock_guard<std::mutex> lock(aircraftMutex);
    assumedAircraft.clear();
    lastAircraftData.clear();
}

void StripCol::OnFlightPlanFlightPlanDataUpdate(CFlightPlan fp) {
    if (!fp.IsValid() || !wsClient->IsConnected()) return;
    std::string callsign = fp.GetCallsign();
    int state = fp.GetState();
    HandleTransferState(fp, callsign, state);
    HandleAssumedState(fp, callsign, state);
}

void StripCol::OnFlightPlanControllerAssignedDataUpdate(CFlightPlan fp, int DataType) {
    if (!fp.IsValid() || !wsClient->IsConnected()) return;
    if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) {
        std::string callsign = fp.GetCallsign();
        std::string acJson = FlightPlanManager::BuildAircraftJson(fp, GetCustomClearance(callsign));
        std::lock_guard<std::mutex> lock2(aircraftMutex);

        if (acJson != lastAircraftData[callsign]) {
            json j;
            j["type"] = "fpupdate";
            j["data"] = json::parse(acJson);
            wsClient->SendMessage(j.dump());
            lastAircraftData[callsign] = acJson;
        }
    }
}

void StripCol::OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan,
    EuroScopePlugIn::CRadarTarget RadarTarget,
    int ItemCode,
    int TagData,
    char sItemString[16],
    int* pColorCode,
    COLORREF* pRGB,
    double* pFontSize) {

    if (ItemCode == TAG_ITEM_SIMULATED_CLEARANCE) {
        if (!FlightPlan.IsValid()) return;
        
        std::string callsign = FlightPlan.GetCallsign();
        bool isCleared = false;
        
        {
            std::lock_guard<std::mutex> lock(clearanceMutex);
            if (customClearanceFlags.count(callsign)) {
                isCleared = customClearanceFlags[callsign];
            }
        }

        *pFontSize = 12.0;

        if (isCleared) {
            strcpy_s(sItemString, 16, "\xA4");
            *pColorCode = EuroScopePlugIn::TAG_COLOR_RGB_DEFINED;
            *pRGB = RGB(0, 255, 0);
        } else {
            strcpy_s(sItemString, 16, "\xAC");
        }
    }
}

void StripCol::OnFunctionCall(int FunctionId,
    const char* sItemString,
    POINT Pt,
    RECT Area) {

    if (FunctionId == TAG_FUNC_TOGGLE_CLEARANCE) {
        CFlightPlan fp = FlightPlanSelectASEL();

        if (fp.IsValid()) {

            std::string callsign = fp.GetCallsign();
            bool newState = false;
            {
                std::lock_guard<std::mutex> lock(clearanceMutex);
                newState = !customClearanceFlags[callsign];
                customClearanceFlags[callsign] = newState;
            }

            // Notify Gateway
            json j;
            j["type"] = "clearance-update";
            j["callsign"] = callsign;
            j["cleared"] = newState;
            wsClient->SendMessage(j.dump());
        }
    }
}

void StripCol::OnFlightPlanDisconnect(CFlightPlan fp) {
    if (!fp.IsValid() || !wsClient->IsConnected()) return;
    if (fp.GetState() == EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED) {
        std::string callsign = fp.GetCallsign();
        
        {
            std::lock_guard<std::mutex> lock(clearanceMutex);
            customClearanceFlags.erase(callsign);
        }

        json j;

        j["type"] = "release";
        j["callsign"] = callsign;
        wsClient->SendMessage(j.dump());
        std::lock_guard<std::mutex> lock2(aircraftMutex);
        lastAircraftData.erase(callsign);
    }
}

bool StripCol::OnCompileCommand(const char* sCommandLine) {
    std::string cmd = sCommandLine;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd.find(".striprestart") == 0) {
        Utils::LogMessage("Manual restart triggered via command.");
        DisconnectFromGateway();
        ConnectToGateway();
        DisplayUserMessage("StripCol", "System", "Gateway connection restarted.", true, true, false, false, false);
        return true;
    }

    if (cmd.find(".stripset") == 0) {
        std::string original = sCommandLine;
        size_t spacePos = original.find(' ');
        std::string newAddr = (spacePos == std::string::npos) ? "127.0.0.1" : original.substr(spacePos + 1);
        newAddr.erase(0, newAddr.find_first_not_of(" \t\r\n"));
        newAddr.erase(newAddr.find_last_not_of(" \t\r\n") + 1);
        if (newAddr.empty()) newAddr = "127.0.0.1";

        {
            std::lock_guard<std::mutex> lock(gatewayMutex);
            gatewayAddress = newAddr;
            wsClient->UpdateAddress(newAddr);
        }

        std::string msg = "Gateway set to: " + newAddr;
        DisplayUserMessage("StripCol", "Gateway", msg.c_str(), true, true, false, false, false);
        DisconnectFromGateway();
        ConnectToGateway();
        return true;
    }

    if (cmd.find(".stripcode") == 0) {
        std::lock_guard<std::mutex> lock(codeMutex);
        if (!pairingCode.empty()) {
            std::string msg = "Current Pairing Code: " + pairingCode;
            DisplayUserMessage("StripCol", "System", msg.c_str(), true, true, false, false, false);
        } else {
            DisplayUserMessage("StripCol", "System", "No pairing code generated yet.", true, true, false, false, false);
        }
        return true;
    }

    return false;
}

// Plugin Entry Points
static StripCol* g_pPlugin = nullptr;

void EuroScopePlugInInit(CPlugIn** ppPlugInInstance) {
    g_pPlugin = new StripCol();
    *ppPlugInInstance = g_pPlugin;
}

void EuroScopePlugInExit(void) {
    delete g_pPlugin;
    g_pPlugin = nullptr;
}
