#include "pch.h"
#include "plugin/StripCol.h"
#include "common/Constants.h"
#include "common/Log.h"
#include "common/ModulePath.h"
#include "common/Text.h"
#include "gateway/CommandContext.h"
#include "gateway/CommandDispatcher.h"
#include <cstring>

using namespace EuroScopePlugIn;

StripCol::StripCol()
    : CPlugIn(COMPATIBILITY_CODE,
              StripColConstants::PLUGIN_NAME,
              StripColConstants::PLUGIN_VERSION,
              StripColConstants::PLUGIN_AUTHOR,
              StripColConstants::PLUGIN_COPYRIGHT),
      tracker_(*this, link_, catalog_),
      atcList_(*this, link_)
{
    const std::string proceduresPath = ModulePath::Directory() + "\\" + StripColConstants::PROCEDURES_FILE;
    if (!catalog_.Load(proceduresPath))
        Log::Write("Could not load " + proceduresPath);

    RegisterTagItemType("Squawk Assignment", TAG_ITEM_SQUAWK);
    RegisterTagItemFunction("Assign Squawk", TAG_FUNC_ASSIGN_SQUAWK);
}

void StripCol::Notify(const char *category, const std::string &text)
{
    DisplayUserMessage("StripCol", category, text.c_str(), true, true, false, false, false);
}

void StripCol::StartSession(bool newPairingCode)
{
    CController myself = ControllerMyself();
    if (!myself.IsValid())
        return;

    const std::string previousCode = link_.PairingCode();
    link_.Open(myself, newPairingCode);
    tracker_.Clear();
    atcList_.Invalidate();

    if (link_.PairingCode() != previousCode)
        Notify("Pairing", "StripCol Pairing Code: " + link_.PairingCode());
}

void StripCol::StopSession()
{
    link_.Close();
    tracker_.Clear();
    atcList_.Invalidate();
}

void StripCol::OnControllerPositionUpdate(CController Controller)
{
    CController myself = ControllerMyself();
    if (!myself.IsValid() || std::strcmp(Controller.GetCallsign(), myself.GetCallsign()) != 0)
        return;

    if (link_.RegisteredCallsign() == myself.GetCallsign())
        link_.RefreshRegistration(myself);
    else
        StartSession(true);
}

void StripCol::OnControllerDisconnect(CController Controller)
{
    if (link_.IsOpen() && link_.RegisteredCallsign() == Controller.GetCallsign())
    {
        Log::Write("Own position disconnected, closing the gateway session");
        StopSession();
    }
}

void StripCol::OnFlightPlanFlightPlanDataUpdate(CFlightPlan FlightPlan)
{
    if (FlightPlan.IsValid())
        tracker_.MarkDirty(FlightPlan.GetCallsign());
}

void StripCol::OnFlightPlanControllerAssignedDataUpdate(CFlightPlan FlightPlan, int)
{
    if (FlightPlan.IsValid())
        tracker_.MarkDirty(FlightPlan.GetCallsign());
}

void StripCol::OnFlightPlanDisconnect(CFlightPlan FlightPlan)
{
    if (FlightPlan.IsValid())
        tracker_.Release(FlightPlan.GetCallsign());
}

void StripCol::OnTimer(int Counter)
{
    try
    {
        if (link_.TakeReconnected())
        {
            tracker_.Resync();
            atcList_.Invalidate();
        }

        CommandContext context{*this, link_, tracker_, atcList_};
        for (const std::string &message : link_.TakeInbound())
            CommandDispatcher::Dispatch(context, message);

        tracker_.Update(Counter);
        atcList_.Update(Counter);
    }
    catch (const std::exception &error)
    {
        Log::Write(std::string("Timer update failed: ") + error.what());
    }
}
