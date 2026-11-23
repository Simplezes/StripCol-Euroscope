#include "pch.h"
#include "flightplan/RouteEditor.h"
#include "common/Text.h"
#include <sstream>
#include <vector>

namespace RouteEditor
{
    namespace
    {
        constexpr const char *EDGE_SEPARATORS = " ,";

        std::vector<std::string> Tokenize(const std::string &route)
        {
            std::vector<std::string> tokens;
            std::istringstream stream(route);
            std::string token;
            while (stream >> token)
                tokens.push_back(token);
            return tokens;
        }

        std::string Join(const std::vector<std::string> &tokens, size_t first, size_t last)
        {
            std::string joined;
            for (size_t i = first; i < last; i++)
            {
                if (!joined.empty())
                    joined += ' ';
                joined += tokens[i];
            }

            size_t start = joined.find_first_not_of(EDGE_SEPARATORS);
            if (start == std::string::npos)
                return std::string();
            size_t end = joined.find_last_not_of(EDGE_SEPARATORS);
            return joined.substr(start, end - start + 1);
        }

        size_t ExistingProcedureTokens(const std::vector<std::string> &tokens, bool fromFront)
        {
            if (tokens.empty())
                return 0;

            const std::string &edge = fromFront ? tokens.front() : tokens.back();
            if (!IsProcedurePattern(edge))
                return 0;
            if (edge.find('/') != std::string::npos || tokens.size() < 2)
                return 1;

            const std::string &neighbour = fromFront ? tokens[1] : tokens[tokens.size() - 2];
            return IsProcedurePattern(neighbour) ? 2 : 1;
        }

        bool Amend(EuroScopePlugIn::CFlightPlan flightPlan, const std::string &route)
        {
            EuroScopePlugIn::CFlightPlanData data = flightPlan.GetFlightPlanData();
            return data.SetRoute(route.c_str()) && data.AmendFlightPlan();
        }
    }

    bool IsProcedurePattern(const std::string &segment)
    {
        bool hasDigit = segment.find_first_of("0123456789") != std::string::npos;
        bool hasLetter = segment.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ") != std::string::npos;
        return hasDigit && hasLetter;
    }

    std::string WithSid(const std::string &route, const std::string &sid, const std::string &runway)
    {
        std::vector<std::string> tokens = Tokenize(route);
        size_t remove = ExistingProcedureTokens(tokens, true);
        std::string rest = Join(tokens, remove, tokens.size());

        std::string procedure = sid + "/" + runway;
        return rest.empty() ? procedure : procedure + " " + rest;
    }

    std::string WithStar(const std::string &route, const std::string &star, const std::string &runway)
    {
        std::vector<std::string> tokens = Tokenize(route);
        size_t remove = ExistingProcedureTokens(tokens, false);
        std::string rest = Join(tokens, 0, tokens.size() - remove);

        std::string procedure = star + "/" + runway;
        return rest.empty() ? procedure : rest + " " + procedure;
    }

    bool ApplySid(EuroScopePlugIn::CFlightPlan flightPlan, const std::string &sid, const std::string &runway)
    {
        if (!flightPlan.IsValid() || flightPlan.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED)
            return false;

        std::string route = Text::FromCString(flightPlan.GetFlightPlanData().GetRoute());
        return Amend(flightPlan, WithSid(route, sid, runway));
    }

    bool ApplyStar(EuroScopePlugIn::CFlightPlan flightPlan, const std::string &star, const std::string &runway)
    {
        if (!flightPlan.IsValid() || flightPlan.GetState() != EuroScopePlugIn::FLIGHT_PLAN_STATE_ASSUMED)
            return false;

        std::string route = Text::FromCString(flightPlan.GetFlightPlanData().GetRoute());
        return Amend(flightPlan, WithStar(route, star, runway));
    }
}
