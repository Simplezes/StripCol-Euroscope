#include "pch.h"
#include "gateway/GatewayLink.h"
#include "common/Constants.h"
#include "common/Text.h"
#include "gateway/JsonFields.h"
#include <nlohmann/json.hpp>

using Text::FromCString;

GatewayLink::GatewayLink() : host_(StripColConstants::DEFAULT_GATEWAY)
{
}

std::string GatewayLink::BuildRegistration(EuroScopePlugIn::CController myself) const
{
    nlohmann::json message;
    message["type"] = "register";
    message["code"] = pairingCode_;
    message["callsign"] = FromCString(myself.GetCallsign());
    message["name"] = FromCString(myself.GetFullName());
    message["facility"] = myself.GetFacility();
    message["rating"] = myself.GetRating();
    message["positionId"] = FromCString(myself.GetPositionId());
    message["frequency"] = Text::FormatFrequency(myself.GetPrimaryFrequency());
    return JsonFields::Dump(message);
}

void GatewayLink::Open(EuroScopePlugIn::CController myself, bool newPairingCode)
{
    if (newPairingCode || pairingCode_.empty())
        pairingCode_ = Text::GeneratePairingCode();

    registeredCallsign_ = FromCString(myself.GetCallsign());
    registration_ = BuildRegistration(myself);
    client_.Connect(host_, StripColConstants::GATEWAY_PORT, registration_, discover_);
}

void GatewayLink::Close()
{
    client_.Disconnect();
    pairingCode_.clear();
    registeredCallsign_.clear();
    registration_.clear();
}

void GatewayLink::RefreshRegistration(EuroScopePlugIn::CController myself)
{
    std::string current = BuildRegistration(myself);
    if (current == registration_)
        return;

    registration_ = current;
    client_.SetGreeting(registration_);
    client_.Send(registration_);
}

void GatewayLink::SetHost(const std::string &host, bool discover)
{
    host_ = host;
    discover_ = discover;
    if (IsOpen())
        client_.Connect(host_, StripColConstants::GATEWAY_PORT, registration_, discover_);
}

bool GatewayLink::Send(const std::string &message)
{
    return client_.Send(message);
}

bool GatewayLink::TakeReconnected()
{
    return client_.TakeConnectedEvent();
}

std::vector<std::string> GatewayLink::TakeInbound()
{
    return client_.TakeMessages();
}
