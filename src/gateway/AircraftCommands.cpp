#include "pch.h"
#include "gateway/AircraftCommands.h"
#include "common/Constants.h"
#include "common/Log.h"
#include "common/Text.h"
#include "flightplan/RouteEditor.h"
#include "gateway/JsonFields.h"
#include "gateway/Messages.h"

using namespace EuroScopePlugIn;
using namespace StripColConstants;

namespace AircraftCommands
{
    namespace
    {
        struct Target
        {
            CFlightPlan flightPlan;
            std::string callsign;
        };

        bool FindTarget(CommandContext &context, const nlohmann::json &message, Target &target)
        {
            target.callsign = JsonFields::String(message, "callsign");
            if (target.callsign.empty())
                return false;

            target.flightPlan = context.plugin.FlightPlanSelect(target.callsign.c_str());
            return target.flightPlan.IsValid();
        }

        void Reject(const char *command, const std::string &reason)
        {
            Log::Write(std::string("Ignored ") + command + ": " + reason);
        }
    }

    void SetClearedAltitude(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        auto altitude = JsonFields::Integer(message, "clearedAltitude");
        if (!altitude || !FindTarget(context, message, target))
            return Reject("set-cleared-alt", "missing flight or altitude");

        target.flightPlan.GetControllerAssignedData().SetClearedAltitude(*altitude);
        context.tracker.MarkDirty(target.callsign);
    }

    void SetFinalAltitude(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        auto altitude = JsonFields::Integer(message, "finalAltitude");
        if (!altitude || !FindTarget(context, message, target))
            return Reject("set-final-alt", "missing flight or altitude");

        target.flightPlan.GetControllerAssignedData().SetFinalAltitude(*altitude);
        context.tracker.MarkDirty(target.callsign);
    }

    void SetAssignedHeading(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        auto heading = JsonFields::Integer(message, "assignedHeading");
        if (!heading || *heading < 0 || *heading > MAX_HEADING || !FindTarget(context, message, target))
            return Reject("set-assigned-heading", "missing flight or invalid heading");

        target.flightPlan.GetControllerAssignedData().SetAssignedHeading(*heading);
        context.tracker.MarkDirty(target.callsign);
    }

    void SetAssignedSpeed(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        auto speed = JsonFields::Integer(message, "assignedSpeed");
        if (!speed || *speed < 0 || *speed > MAX_ASSIGNED_SPEED || !FindTarget(context, message, target))
            return Reject("set-assigned-speed", "missing flight or invalid speed");

        target.flightPlan.GetControllerAssignedData().SetAssignedSpeed(*speed);
        context.tracker.MarkDirty(target.callsign);
    }

    void SetAssignedMach(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        auto mach = JsonFields::Number(message, "assignedMach");
        if (!mach || *mach < 0 || *mach > MAX_ASSIGNED_MACH || !FindTarget(context, message, target))
            return Reject("set-assigned-mach", "missing flight or invalid mach");

        target.flightPlan.GetControllerAssignedData().SetAssignedMach(static_cast<int>(*mach * MACH_SCALE + 0.5));
        context.tracker.MarkDirty(target.callsign);
    }

    void SetSquawk(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        std::string squawk = JsonFields::String(message, "squawk");
        if (!Text::IsValidSquawk(squawk) || !FindTarget(context, message, target))
            return Reject("set-squawk", "missing flight or invalid squawk");

        target.flightPlan.GetControllerAssignedData().SetSquawk(squawk.c_str());
        context.tracker.MarkDirty(target.callsign);
    }

    void SetClearance(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        auto cleared = JsonFields::Boolean(message, "cleared");
        if (!cleared || !FindTarget(context, message, target))
            return Reject("set-clearance", "missing flight or state");

        const bool before = target.flightPlan.GetClearenceFlag();
        Log::Write("set-clearance " + target.callsign + ": requested=" + (*cleared ? "true" : "false") +
                   " current=" + (before ? "true" : "false"));
        if (before == *cleared)
            return;

        if (!context.screen)
            return Reject("set-clearance", "no EuroScope radar screen is open");

        context.screen->ToggleClearedFlag(context.plugin, target.flightPlan);
        const bool after = target.flightPlan.GetClearenceFlag();
        Log::Write("set-clearance " + target.callsign + ": after tag function=" + (after ? "true" : "false") +
                   (after == *cleared ? " (ok)" : " (EuroScope did not apply it)"));
        context.tracker.MarkDirty(target.callsign);
    }

    void SetDepartureTime(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        std::string departureTime = JsonFields::String(message, "Dtime");
        if (departureTime.empty() || !FindTarget(context, message, target))
            return Reject("set-departureTime", "missing flight or time");

        target.flightPlan.GetFlightPlanData().SetActualDepartureTime(departureTime.c_str());
        context.tracker.MarkDirty(target.callsign);
    }

    void SetDirectPoint(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        std::string pointName = JsonFields::String(message, "pointName");
        if (pointName.empty() || !FindTarget(context, message, target))
            return Reject("set-direct-point", "missing flight or point");

        CFlightPlanExtractedRoute route = target.flightPlan.GetExtractedRoute();
        for (int index = 0; index < route.GetPointsNumber(); index++)
        {
            const char *name = route.GetPointName(index);
            if (name && pointName == name)
            {
                target.flightPlan.GetControllerAssignedData().SetDirectToPointName(pointName.c_str());
                context.tracker.MarkDirty(target.callsign);
                return;
            }
        }
        Reject("set-direct-point", pointName + " is not on the route");
    }

    void SetSid(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        std::string sid = JsonFields::String(message, "sid");
        std::string runway = JsonFields::String(message, "runway");
        if (sid.empty() || runway.empty() || !FindTarget(context, message, target))
            return Reject("set-sid", "missing flight, sid or runway");

        if (!RouteEditor::ApplySid(target.flightPlan, sid, runway))
            return Reject("set-sid", "flight plan could not be amended");
        context.tracker.MarkDirty(target.callsign);
    }

    void SetStar(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        std::string star = JsonFields::String(message, "star");
        std::string runway = JsonFields::String(message, "runway");
        if (star.empty() || runway.empty() || !FindTarget(context, message, target))
            return Reject("set-star", "missing flight, star or runway");

        if (!RouteEditor::ApplyStar(target.flightPlan, star, runway))
            return Reject("set-star", "flight plan could not be amended");
        context.tracker.MarkDirty(target.callsign);
    }

    void AssumeAircraft(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        if (!FindTarget(context, message, target))
            return Reject("assume-aircraft", "unknown flight");

        if (target.flightPlan.GetState() != FLIGHT_PLAN_STATE_ASSUMED)
            target.flightPlan.StartTracking();
    }

    void AcceptHandoff(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        if (!FindTarget(context, message, target))
            return Reject("accept-handoff", "unknown flight");

        target.flightPlan.AcceptHandoff();
    }

    void RefuseHandoff(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        if (!FindTarget(context, message, target))
            return Reject("refuse-handoff", "unknown flight");

        target.flightPlan.RefuseHandoff();
    }

    void TransferToController(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        std::string controller = JsonFields::String(message, "targetATC");
        if (controller.empty() || !FindTarget(context, message, target))
            return Reject("ATC-transfer", "missing flight or controller");

        target.flightPlan.InitiateHandoff(controller.c_str());
        context.tracker.MarkDirty(target.callsign);
    }

    void EndTracking(CommandContext &context, const nlohmann::json &message)
    {
        Target target;
        if (!FindTarget(context, message, target))
            return Reject("end-tracking", "unknown flight");

        target.flightPlan.EndTracking();
        context.tracker.Release(target.callsign);
    }

    void ListNearbyAircraft(CommandContext &context, const nlohmann::json &)
    {
        CController myself = context.plugin.ControllerMyself();
        if (!myself.IsValid())
            return;

        const CPosition myPosition = myself.GetPosition();
        std::vector<std::string> callsigns;

        for (CFlightPlan flightPlan = context.plugin.FlightPlanSelectFirst(); flightPlan.IsValid();
             flightPlan = context.plugin.FlightPlanSelectNext(flightPlan))
        {
            const int state = flightPlan.GetState();
            if (state == FLIGHT_PLAN_STATE_ASSUMED || state == FLIGHT_PLAN_STATE_REDUNDANT)
                continue;

            CRadarTarget target = context.plugin.RadarTargetSelect(flightPlan.GetCallsign());
            if (!target.IsValid())
                continue;

            if (myPosition.DistanceTo(target.GetPosition().GetPosition()) <= NEARBY_AIRCRAFT_RANGE_NM)
                callsigns.push_back(flightPlan.GetCallsign());
        }

        context.link.Send(Messages::NearbyAircraft(callsigns));
    }

    void ResendState(CommandContext &context, const nlohmann::json &)
    {
        context.tracker.Resync();
        context.atcList.Invalidate();
    }
}
