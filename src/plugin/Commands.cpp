#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Constants.h"
#include "common/Text.h"

bool StripCol::OnCompileCommand(const char *sCommandLine)
{
    const std::string line = Text::Trim(Text::FromCString(sCommandLine));
    const size_t split = line.find_first_of(" \t");
    const std::string command = Text::ToLower(line.substr(0, split));
    const std::string argument = split == std::string::npos ? std::string() : Text::Trim(line.substr(split));

    if (command == ".stripsquawk")
        RunToggleSquawkCommand();
    else if (command == ".striprestart")
        RunRestartCommand();
    else if (command == ".stripset")
        RunSetGatewayCommand(argument);
    else if (command == ".stripcode")
        RunPairingCodeCommand();
    else
        return false;

    return true;
}

void StripCol::RunToggleSquawkCommand()
{
    squawkAssignmentEnabled_ = !squawkAssignmentEnabled_;
    Notify("System", std::string("Squawk Assignment feature ") + (squawkAssignmentEnabled_ ? "enabled." : "disabled."));
}

void StripCol::RunRestartCommand()
{
    if (!ControllerMyself().IsValid())
    {
        Notify("System", "Connect as a controller before restarting the gateway connection.");
        return;
    }

    StartSession(true);
    Notify("System", "Gateway connection restarted.");
}

void StripCol::RunSetGatewayCommand(const std::string &argument)
{
    const std::string host = argument.empty() ? std::string(StripColConstants::DEFAULT_GATEWAY) : argument;
    if (!Text::IsValidHostname(host))
    {
        Notify("Gateway", "Invalid gateway address: " + host);
        return;
    }

    link_.SetHost(host);
    Notify("Gateway", "Gateway set to: " + host);
}

void StripCol::RunPairingCodeCommand()
{
    if (link_.PairingCode().empty())
        Notify("System", "No pairing code generated yet.");
    else
        Notify("System", "Current Pairing Code: " + link_.PairingCode());
}
