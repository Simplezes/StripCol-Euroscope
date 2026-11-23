#pragma once

#include <string>
#include <nlohmann/json.hpp>

struct SectorData
{
    nlohmann::json procedures = nlohmann::json::object();
    nlohmann::json sectors = nlohmann::json::object();

    bool HasProcedures() const { return !procedures.empty(); }
};

namespace SectorParser
{
    bool ParseFile(const std::string &path, SectorData &result);
}
