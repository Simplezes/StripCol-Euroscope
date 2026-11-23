#pragma once

#include "EuroScopePlugIn.h"
#include "network/WebSocketClient.h"
#include <string>
#include <vector>

class GatewayLink
{
public:
    GatewayLink();

    void Open(EuroScopePlugIn::CController myself, bool newPairingCode);
    void Close();
    void RefreshRegistration(EuroScopePlugIn::CController myself);
    void SetHost(const std::string &host, bool discover);

    bool IsOpen() const { return !registeredCallsign_.empty(); }
    bool IsConnected() const { return client_.IsConnected(); }
    const std::string &Host() const { return host_; }
    const std::string &PairingCode() const { return pairingCode_; }
    const std::string &RegisteredCallsign() const { return registeredCallsign_; }

    bool Send(const std::string &message);
    bool TakeReconnected();
    std::vector<std::string> TakeInbound();

private:
    std::string BuildRegistration(EuroScopePlugIn::CController myself) const;

    WebSocketClient client_;
    std::string host_;
    bool discover_ = true;
    std::string pairingCode_;
    std::string registeredCallsign_;
    std::string registration_;
};
