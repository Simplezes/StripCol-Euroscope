#include "pch.h"
#include "flightplan/ProcedureCatalog.h"
#include "common/Text.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    const char *TypeKey(ProcedureType type)
    {
        return type == ProcedureType::Sid ? "SID" : "STAR";
    }

    std::vector<std::string> ReadWaypoints(const json &list)
    {
        std::vector<std::string> waypoints;
        for (const json &entry : list)
        {
            if (entry.is_string())
                waypoints.push_back(entry.get<std::string>());
        }
        return waypoints;
    }
}

std::string ProcedureCatalog::MakeKey(const std::string &airport,
                                      ProcedureType type,
                                      const std::string &runway,
                                      const std::string &name)
{
    return airport + '|' + TypeKey(type) + '|' + runway + '|' + Text::ToLower(name);
}

bool ProcedureCatalog::Load(const std::string &path)
{
    procedures_.clear();

    std::ifstream file(path);
    if (!file.is_open())
        return false;

    json root = json::parse(file, nullptr, false);
    if (!root.is_object())
        return false;

    for (const auto &[airport, byType] : root.items())
    {
        if (!byType.is_object())
            continue;

        for (ProcedureType type : {ProcedureType::Sid, ProcedureType::Star})
        {
            auto typeEntry = byType.find(TypeKey(type));
            if (typeEntry == byType.end() || !typeEntry->is_object())
                continue;

            for (const auto &[runway, byName] : typeEntry->items())
            {
                if (!byName.is_object())
                    continue;

                for (const auto &[name, list] : byName.items())
                {
                    if (list.is_array())
                        procedures_.emplace(MakeKey(airport, type, runway, name), ReadWaypoints(list));
                }
            }
        }
    }
    return true;
}

const std::vector<std::string> &ProcedureCatalog::Waypoints(const std::string &airport,
                                                            ProcedureType type,
                                                            const std::string &runway,
                                                            const std::string &name) const
{
    static const std::vector<std::string> none;

    if (airport.empty() || runway.empty() || name.empty())
        return none;

    auto found = procedures_.find(MakeKey(airport, type, runway, name));
    return found == procedures_.end() ? none : found->second;
}
