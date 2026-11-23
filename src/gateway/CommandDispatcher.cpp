#include "pch.h"
#include "gateway/CommandDispatcher.h"
#include "common/Log.h"
#include "gateway/AircraftCommands.h"
#include "gateway/JsonFields.h"
#include <unordered_map>

namespace
{
    using Handler = void (*)(CommandContext &, const nlohmann::json &);

    const std::unordered_map<std::string, Handler> &Handlers()
    {
        static const std::unordered_map<std::string, Handler> handlers = {
            {"sync", AircraftCommands::ResendState},
            {"get-nearby-aircraft", AircraftCommands::ListNearbyAircraft},
            {"assume-aircraft", AircraftCommands::AssumeAircraft},
            {"accept-handoff", AircraftCommands::AcceptHandoff},
            {"refuse-handoff", AircraftCommands::RefuseHandoff},
            {"ATC-transfer", AircraftCommands::TransferToController},
            {"end-tracking", AircraftCommands::EndTracking},
            {"set-cleared-alt", AircraftCommands::SetClearedAltitude},
            {"set-final-alt", AircraftCommands::SetFinalAltitude},
            {"set-assigned-heading", AircraftCommands::SetAssignedHeading},
            {"set-assigned-speed", AircraftCommands::SetAssignedSpeed},
            {"set-assigned-mach", AircraftCommands::SetAssignedMach},
            {"set-squawk", AircraftCommands::SetSquawk},
            {"set-clearance", AircraftCommands::SetClearance},
            {"set-departureTime", AircraftCommands::SetDepartureTime},
            {"set-direct-point", AircraftCommands::SetDirectPoint},
            {"set-sid", AircraftCommands::SetSid},
            {"set-star", AircraftCommands::SetStar}};
        return handlers;
    }
}

namespace CommandDispatcher
{
    void Dispatch(CommandContext &context, const std::string &rawMessage)
    {
        nlohmann::json message = nlohmann::json::parse(rawMessage, nullptr, false);
        if (!message.is_object())
        {
            Log::Write("Ignored a gateway message that is not a JSON object");
            return;
        }

        const std::string type = JsonFields::String(message, "type");
        auto handler = Handlers().find(type);
        if (handler == Handlers().end())
        {
            Log::Write("Unhandled gateway message type: " + type);
            return;
        }

        try
        {
            handler->second(context, message);
        }
        catch (const std::exception &error)
        {
            Log::Write("Command " + type + " failed: " + error.what());
        }
    }
}
