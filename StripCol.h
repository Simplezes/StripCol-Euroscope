#pragma once

#include "EuroScopePlugIn.h"
#include "WebSocketClient.h"
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <queue>
#include <atomic>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Custom Tag IDs
const int TAG_ITEM_SIMULATED_CLEARANCE = 101;
const int TAG_FUNC_TOGGLE_CLEARANCE = 101;


class StripCol : public EuroScopePlugIn::CPlugIn {
private:
    // WebSocket Client
    std::unique_ptr<WebSocketClient> wsClient;
    std::mutex gatewayMutex;
    std::string gatewayAddress = "127.0.0.1";
    std::atomic<bool> connectionRequested{ false };
    
    // Pairing code
    std::string pairingCode;
    std::mutex codeMutex;

    // Aircraft tracking
    std::unordered_set<std::string> assumedAircraft;
    std::unordered_map<std::string, std::string> lastAircraftData;
    std::string lastAtcListJson;
    std::mutex aircraftMutex;

    std::unordered_set<std::string> transferToMeAircraft;
    std::mutex transferMutex;

    // Simulated Clearance Flags
    std::unordered_map<std::string, bool> customClearanceFlags;
    std::mutex clearanceMutex;


    // Command Queue
    struct PendingTask {
        std::string type;
        std::string json;
    };
    std::queue<PendingTask> taskQueue;
    std::mutex queueMutex;

public:
    StripCol();
    virtual ~StripCol();

    // EuroScope Overrides
    void OnTimer(int Counter) override;

    void OnControllerPositionUpdate(EuroScopePlugIn::CController Controller) override;
    void OnControllerDisconnect(EuroScopePlugIn::CController Controller) override;
    void OnFlightPlanFlightPlanDataUpdate(EuroScopePlugIn::CFlightPlan fp) override;
    void OnFlightPlanControllerAssignedDataUpdate(EuroScopePlugIn::CFlightPlan fp, int DataType) override;
    void OnFlightPlanDisconnect(EuroScopePlugIn::CFlightPlan fp) override;
    bool OnCompileCommand(const char* sCommandLine) override;

    // Custom Tag Handlers
    void OnGetTagItem(EuroScopePlugIn::CFlightPlan FlightPlan,
        EuroScopePlugIn::CRadarTarget RadarTarget,
        int ItemCode,
        int TagData,
        char sItemString[16],
        int* pColorCode,
        COLORREF* pRGB,
        double* pFontSize) override;

    void OnFunctionCall(int FunctionId,
        const char* sItemString,
        POINT Pt,
        RECT Area) override;


private:
    void ConnectToGateway();
    void DisconnectFromGateway();
    void HandleWebSocketMessage(const std::string& message);
    void ProcessPendingTasks();
    bool GetCustomClearance(const std::string& callsign);
    void SendRegistration();

    void SendAtcList(bool force = false);
    void CheckAllFlightPlans();
    
    void HandleTransferState(EuroScopePlugIn::CFlightPlan& fp, const std::string& callsign, int state);
    void HandleAssumedState(EuroScopePlugIn::CFlightPlan& fp, const std::string& callsign, int state);

    // Command Handlers
    void HandleSetClearedAltitude(const std::string& jsonStr);
    void HandleSetAssignedHeading(const std::string& jsonStr);
    void HandleSetAssignedSpeed(const std::string& jsonStr);
    void HandleSetFinalAltitude(const std::string& jsonStr);
    void HandleAcceptHandoff(const std::string& jsonStr);
    void HandleEndTracking(const std::string& jsonStr);
    void HandleSetSquawk(const std::string& jsonStr);
    void HandleSetDepartureTime(const std::string& jsonStr);
    void HandleSetDirectPoint(const std::string& jsonStr);
    void HandleSetSid(const std::string& jsonStr);
    void HandleSetStar(const std::string& jsonStr);
    void HandleSetAssignedMach(const std::string& jsonStr);
    void HandleRefuseHandoff(const std::string& jsonStr);
    void HandleAtcTransfer(const std::string& jsonStr);
    void HandleSync(const std::string& jsonStr);
    void HandleAssumeAircraft(const std::string& jsonStr);
    void HandleGetNearbyAircraft(const std::string& jsonStr);
    void HandleSetClearance(const std::string& jsonStr);


    std::string GetJsonValue(const std::string& jsonStr, const std::string& key);
};
