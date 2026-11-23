#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Utils.h"
#include <algorithm>

using namespace EuroScopePlugIn;

bool StripCol::OnCompileCommand(const char *sCommandLine)
{
    std::string cmd = sCommandLine;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if (cmd.find(".stripsquawk") == 0)
    {
        enableSquawkAssignment = !enableSquawkAssignment;
        std::string state = enableSquawkAssignment ? "enabled" : "disabled";
        DisplayUserMessage("StripCol", "System", ("Squawk Assignment feature " + state + ".").c_str(), true, true, false, false, false);
        return true;
    }

    if (cmd.find(".striprestart") == 0)
    {
        Utils::LogMessage("Manual restart triggered via command.");
        DisconnectFromGateway();
        {
            std::lock_guard<std::mutex> lock(codeMutex);
            pairingCode.clear();
            registeredCallsign.clear();
        }
        ConnectToGateway();
        DisplayUserMessage("StripCol", "System", "Gateway connection restarted.", true, true, false, false, false);
        return true;
    }

    if (cmd.find(".stripset") == 0)
    {
        std::string original = sCommandLine;
        size_t spacePos = original.find(' ');
        std::string newAddr = (spacePos == std::string::npos) ? "127.0.0.1" : original.substr(spacePos + 1);
        newAddr.erase(0, newAddr.find_first_not_of(" \t\r\n"));
        newAddr.erase(newAddr.find_last_not_of(" \t\r\n") + 1);
        if (newAddr.empty())
            newAddr = "127.0.0.1";

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

    if (cmd.find(".stripcode") == 0)
    {
        std::lock_guard<std::mutex> lock(codeMutex);
        if (!pairingCode.empty())
        {
            std::string msg = "Current Pairing Code: " + pairingCode;
            DisplayUserMessage("StripCol", "System", msg.c_str(), true, true, false, false, false);
        }
        else
        {
            DisplayUserMessage("StripCol", "System", "No pairing code generated yet.", true, true, false, false, false);
        }
        return true;
    }

    return false;
}
