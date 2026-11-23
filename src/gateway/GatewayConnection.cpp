#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Utils.h"
#include "common/Constants.h"
#include "network/HttpClient.h"
#include <nlohmann/json.hpp>

using namespace EuroScopePlugIn;
using json = nlohmann::json;

void StripCol::ConnectToGateway()
{
    if (wsClient->IsRunning())
        return;

    Utils::LogMessage("ConnectToGateway: Preparing fresh connection...");

    CController myself = ControllerMyself();
    if (myself.IsValid())
    {
        json j;
        j["type"] = "register";

        {
            std::lock_guard<std::mutex> lock(codeMutex);
            if (pairingCode.empty())
            {
                pairingCode = Utils::GeneratePairingCode();
                std::string message = "StripCol Pairing Code: " + pairingCode;
                DisplayUserMessage("StripCol", "Pairing", message.c_str(), true, true, false, false, false);
            }
            j["code"] = pairingCode;
            registeredCallsign = myself.GetCallsign();
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

void StripCol::DisconnectFromGateway()
{
    wsClient->Disconnect();
}

void StripCol::GetPointTime(const std::string &callsign, const std::string &points, std::function<void(bool, const std::string &)> callback)
{
    std::string code;
    std::string addr;
    {
        std::lock_guard<std::mutex> lock(codeMutex);
        code = pairingCode;
    }
    {
        std::lock_guard<std::mutex> lock(gatewayMutex);
        addr = gatewayAddress;
    }

    if (code.empty() || addr.empty())
    {
        if (callback)
            callback(false, "");
        return;
    }

    std::string url = "http://" + addr + ":" + StripColConstants::DEFAULT_PORT +
                      "/api/point-time?code=" + code +
                      "&callsign=" + callsign;
    if (!points.empty())
        url += "&points=" + points;

    HttpClient::GetAsync(url, "", callback);
}
