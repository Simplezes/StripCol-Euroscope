#pragma once

#include "gateway/CommandContext.h"
#include <nlohmann/json.hpp>

namespace AircraftCommands
{
    void SetClearedAltitude(CommandContext &context, const nlohmann::json &message);
    void SetFinalAltitude(CommandContext &context, const nlohmann::json &message);
    void SetAssignedHeading(CommandContext &context, const nlohmann::json &message);
    void SetAssignedSpeed(CommandContext &context, const nlohmann::json &message);
    void SetAssignedMach(CommandContext &context, const nlohmann::json &message);
    void SetSquawk(CommandContext &context, const nlohmann::json &message);
    void SetDepartureTime(CommandContext &context, const nlohmann::json &message);
    void SetDirectPoint(CommandContext &context, const nlohmann::json &message);
    void SetSid(CommandContext &context, const nlohmann::json &message);
    void SetStar(CommandContext &context, const nlohmann::json &message);

    void AssumeAircraft(CommandContext &context, const nlohmann::json &message);
    void AcceptHandoff(CommandContext &context, const nlohmann::json &message);
    void RefuseHandoff(CommandContext &context, const nlohmann::json &message);
    void TransferToController(CommandContext &context, const nlohmann::json &message);
    void EndTracking(CommandContext &context, const nlohmann::json &message);

    void ListNearbyAircraft(CommandContext &context, const nlohmann::json &message);
    void ResendState(CommandContext &context, const nlohmann::json &message);
}
