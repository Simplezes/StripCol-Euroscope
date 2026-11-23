#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Utils.h"
#include <nlohmann/json.hpp>

using namespace EuroScopePlugIn;
using json = nlohmann::json;

void StripCol::HandleWebSocketMessage(const std::string &message)
{
    try
    {
        auto j = json::parse(message);
        if (j.contains("type") && j["type"].is_string())
        {
            std::string type = j["type"];
            std::lock_guard<std::mutex> lock(queueMutex);
            taskQueue.push({type, message});
        }
    }
    catch (const json::parse_error &e)
    {
        Utils::LogMessage("JSON parse error: " + std::string(e.what()));
    }
    catch (...)
    {
        Utils::LogMessage("Error handling command");
    }
}

void StripCol::ProcessPendingTasks()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    while (!taskQueue.empty())
    {
        PendingTask task = taskQueue.front();
        taskQueue.pop();

        static const std::unordered_map<std::string, void (StripCol::*)(const std::string &)> handlers = {
            {"sync", &StripCol::HandleSync},
            {"ATC-transfer", &StripCol::HandleAtcTransfer},
            {"accept-handoff", &StripCol::HandleAcceptHandoff},
            {"end-tracking", &StripCol::HandleEndTracking},
            {"assume-aircraft", &StripCol::HandleAssumeAircraft},
            {"get-nearby-aircraft", &StripCol::HandleGetNearbyAircraft},
            {"set-cleared-alt", &StripCol::HandleSetClearedAltitude},
            {"set-assigned-heading", &StripCol::HandleSetAssignedHeading},
            {"set-assigned-speed", &StripCol::HandleSetAssignedSpeed},
            {"set-final-alt", &StripCol::HandleSetFinalAltitude},
            {"set-squawk", &StripCol::HandleSetSquawk},
            {"set-departureTime", &StripCol::HandleSetDepartureTime},
            {"set-direct-point", &StripCol::HandleSetDirectPoint},
            {"set-sid", &StripCol::HandleSetSid},
            {"set-star", &StripCol::HandleSetStar},
            {"set-assigned-mach", &StripCol::HandleSetAssignedMach},
            {"refuse-handoff", &StripCol::HandleRefuseHandoff}};

        auto it = handlers.find(task.type);
        if (it != handlers.end())
        {
            try
            {
                (this->*(it->second))(task.json);
            }
            catch (...)
            {
                Utils::LogMessage("Exception in handler: " + task.type);
            }
        }
    }
}

std::string StripCol::GetJsonValue(const std::string &jsonStr, const std::string &key)
{
    try
    {
        auto j = json::parse(jsonStr);
        if (j.contains(key))
        {
            if (j[key].is_string())
                return j[key];
            if (j[key].is_number())
                return std::to_string(j[key].get<double>());
            if (j[key].is_boolean())
                return j[key].get<bool>() ? "true" : "false";
        }
    }
    catch (...)
    {
    }
    return "";
}
