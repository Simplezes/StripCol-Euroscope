#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Constants.h"
#include "common/Log.h"
#include "common/ModulePath.h"
#include "common/Text.h"
#include "sector/SectorFileLocator.h"

bool StripCol::OnCompileCommand(const char *sCommandLine)
{
    const std::string line = Text::Trim(Text::FromCString(sCommandLine));
    const size_t split = line.find_first_of(" \t");
    const std::string command = Text::ToLower(line.substr(0, split));
    const std::string argument = split == std::string::npos ? std::string() : Text::Trim(line.substr(split));

    if (command == ".striprestart")
        RunRestartCommand();
    else if (command == ".stripset")
        RunSetGatewayCommand(argument);
    else if (command == ".stripsector")
        RunSectorFileCommand(argument);
    else if (command == ".stripcode")
        RunPairingCodeCommand();
    else if (command == ".stripdev")
        RunDebugConsoleCommand();
    else
        return false;

    return true;
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
    if (argument.empty())
    {
        link_.SetHost(StripColConstants::DEFAULT_GATEWAY, true);
        Notify("Gateway", "Gateway set to automatic discovery.");
        return;
    }

    const std::string host = argument;
    if (!Text::IsValidHostname(host))
    {
        Notify("Gateway", "Invalid gateway address: " + host);
        return;
    }

    link_.SetHost(host, false);
    Notify("Gateway", "Gateway set to: " + host);
}

void StripCol::RunSectorFileCommand(const std::string &argument)
{
    if (argument.empty())
    {
        Notify("Sector", sectorPath_.empty() ? "No sector extension file loaded." : "Sector file: " + sectorPath_);
        return;
    }

    if (!SectorFileLocator::SaveOverride(ModulePath::Directory(), argument))
    {
        Notify("Sector", "Could not save the sector file path.");
        return;
    }

    LoadSectorData();
    SendSectorData();
    Notify("Sector", sectorMessage_.empty() ? "Could not read procedures from: " + argument : "Loaded sector file: " + argument);
}

void StripCol::RunDebugConsoleCommand()
{
    Log::ToggleConsole();
    if (!Log::IsConsoleOpen())
    {
        Notify("System", "Debug console closed.");
        return;
    }

    Notify("System", "Debug console opened.");
    Log::Write("Debug console opened");
    Log::Write(std::string("Gateway connected: ") + (link_.IsConnected() ? "yes" : "no"));
    Log::Write("Radar screens registered: " + std::to_string(screens_.size()));
}

void StripCol::RunPairingCodeCommand()
{
    if (link_.PairingCode().empty())
        Notify("System", "No pairing code generated yet.");
    else
        Notify("System", "Current Pairing Code: " + link_.PairingCode());
}
