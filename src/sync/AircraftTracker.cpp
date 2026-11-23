#include "pch.h"
#include "sync/AircraftTracker.h"
#include "common/Constants.h"
#include "flightplan/AircraftSerializer.h"
#include "gateway/Messages.h"
#include <ctime>

using namespace EuroScopePlugIn;

AircraftTracker::AircraftTracker(CPlugIn &plugin, GatewayLink &link, const ProcedureCatalog &catalog)
    : plugin_(plugin), link_(link), catalog_(catalog)
{
}

void AircraftTracker::MarkDirty(const std::string &callsign)
{
    if (assumed_.count(callsign))
        dirty_.insert(callsign);
}

void AircraftTracker::Resync()
{
    for (auto &entry : assumed_)
        entry.second.announced = false;
}

void AircraftTracker::Clear()
{
    assumed_.clear();
    transferring_.clear();
    dirty_.clear();
}

void AircraftTracker::Release(const std::string &callsign)
{
    auto found = assumed_.find(callsign);
    if (found == assumed_.end())
        return;

    if (link_.Send(Messages::Release(callsign)))
        assumed_.erase(found);
}

void AircraftTracker::Update(int tick)
{
    if (!link_.IsConnected())
    {
        dirty_.clear();
        return;
    }

    ++stamp_;
    now_ = std::time(nullptr);
    const bool refreshAll = tick % StripColConstants::AIRCRAFT_REFRESH_TICKS == 0;

    for (CFlightPlan flightPlan = plugin_.FlightPlanSelectFirst(); flightPlan.IsValid();
         flightPlan = plugin_.FlightPlanSelectNext(flightPlan))
    {
        const std::string callsign = flightPlan.GetCallsign();
        const int state = flightPlan.GetState();

        if (state == FLIGHT_PLAN_STATE_ASSUMED)
            ProcessAssumed(flightPlan, callsign, refreshAll || dirty_.count(callsign) > 0);
        else if (state == FLIGHT_PLAN_STATE_TRANSFER_TO_ME_INITIATED)
            ProcessTransfer(flightPlan, callsign);
    }

    SweepUnseen();
    dirty_.clear();
}

void AircraftTracker::ProcessAssumed(CFlightPlan &flightPlan, const std::string &callsign, bool refresh)
{
    auto found = assumed_.find(callsign);
    const bool isNew = found == assumed_.end();
    if (!isNew)
        found->second.seenStamp = stamp_;

    if (isNew || !found->second.announced)
    {
        std::string data = AircraftSerializer::Serialize(flightPlan, catalog_, now_);
        if (!link_.Send(Messages::Envelope("aircraft", data)))
            return;

        Tracked &tracked = assumed_[callsign];
        tracked.data = std::move(data);
        tracked.announced = true;
        tracked.seenStamp = stamp_;
        return;
    }

    if (!refresh)
        return;

    std::string data = AircraftSerializer::Serialize(flightPlan, catalog_, now_);
    if (data == found->second.data)
        return;

    if (link_.Send(Messages::Envelope("fpupdate", data)))
        found->second.data = std::move(data);
}

void AircraftTracker::ProcessTransfer(CFlightPlan &flightPlan, const std::string &callsign)
{
    auto [entry, inserted] = transferring_.try_emplace(callsign, stamp_);
    entry->second = stamp_;
    if (!inserted)
        return;

    std::string data = AircraftSerializer::Serialize(flightPlan, catalog_, now_);
    if (!link_.Send(Messages::Envelope("transfer", data)))
        transferring_.erase(entry);
}

void AircraftTracker::SweepUnseen()
{
    for (auto it = assumed_.begin(); it != assumed_.end();)
    {
        if (it->second.seenStamp == stamp_)
        {
            ++it;
            continue;
        }

        if (link_.Send(Messages::Release(it->first)))
            it = assumed_.erase(it);
        else
            ++it;
    }

    for (auto it = transferring_.begin(); it != transferring_.end();)
        it = it->second == stamp_ ? std::next(it) : transferring_.erase(it);
}
