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

void AircraftTracker::RecordPassedPoints(CFlightPlan &flightPlan, Tracked &tracked)
{
    CFlightPlanExtractedRoute route = flightPlan.GetExtractedRoute();
    const int index = route.GetPointsCalculatedIndex();
    if (index < 0)
        return;

    if (tracked.passedIndex >= 0 && index > tracked.passedIndex)
    {
        const std::string time = AircraftSerializer::FormatClock(now_);
        for (int i = tracked.passedIndex + 1; i <= index; i++)
        {
            const char *name = route.GetPointName(i);
            if (name && name[0])
                tracked.passedTimes.emplace(name, time);
        }
    }
    tracked.passedIndex = index;
}

void AircraftTracker::ProcessAssumed(CFlightPlan &flightPlan, const std::string &callsign, bool refresh)
{
    Tracked &tracked = assumed_[callsign];
    tracked.seenStamp = stamp_;

    const int passedBefore = tracked.passedIndex;
    RecordPassedPoints(flightPlan, tracked);
    const bool passedPoint = passedBefore >= 0 && tracked.passedIndex > passedBefore;

    if (!tracked.announced)
    {
        std::string data = AircraftSerializer::Serialize(flightPlan, catalog_, now_, tracked.passedTimes);
        if (!link_.Send(Messages::Envelope("aircraft", data)))
            return;

        tracked.data = std::move(data);
        tracked.announced = true;
        return;
    }

    if (!refresh && !passedPoint)
        return;

    std::string data = AircraftSerializer::Serialize(flightPlan, catalog_, now_, tracked.passedTimes);
    if (data == tracked.data)
        return;

    if (link_.Send(Messages::Envelope("fpupdate", data)))
        tracked.data = std::move(data);
}

void AircraftTracker::ProcessTransfer(CFlightPlan &flightPlan, const std::string &callsign)
{
    auto [entry, inserted] = transferring_.try_emplace(callsign, stamp_);
    entry->second = stamp_;
    if (!inserted)
        return;

    std::string data = AircraftSerializer::Serialize(flightPlan, catalog_, now_, {});
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
