#pragma once

#include "EuroScopePlugIn.h"
#include "flightplan/AircraftSerializer.h"
#include "flightplan/ProcedureCatalog.h"
#include "gateway/GatewayLink.h"
#include <string>
#include <unordered_map>
#include <unordered_set>

class AircraftTracker
{
public:
    AircraftTracker(EuroScopePlugIn::CPlugIn &plugin, GatewayLink &link, const ProcedureCatalog &catalog);

    void Update(int tick);
    void MarkDirty(const std::string &callsign);
    void Release(const std::string &callsign);
    void Resync();
    void Clear();

private:
    struct Tracked
    {
        std::string data;
        bool announced = false;
        unsigned seenStamp = 0;
        int passedIndex = -1;
        AircraftSerializer::PassedTimes passedTimes;
    };

    void RecordPassedPoints(EuroScopePlugIn::CFlightPlan &flightPlan, Tracked &tracked);

    void ProcessAssumed(EuroScopePlugIn::CFlightPlan &flightPlan, const std::string &callsign, bool refresh);
    void ProcessTransfer(EuroScopePlugIn::CFlightPlan &flightPlan, const std::string &callsign);
    void SweepUnseen();

    EuroScopePlugIn::CPlugIn &plugin_;
    GatewayLink &link_;
    const ProcedureCatalog &catalog_;

    std::unordered_map<std::string, Tracked> assumed_;
    std::unordered_map<std::string, unsigned> transferring_;
    std::unordered_set<std::string> dirty_;
    unsigned stamp_ = 0;
    std::time_t now_ = 0;
};
