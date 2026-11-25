#pragma once

#include <string>
#include <nlohmann/json.hpp>

struct SectorData
{
    nlohmann::json procedures = nlohmann::json::object();
    nlohmann::json sectors = nlohmann::json::object();
    nlohmann::json transfers = nlohmann::json::array();
    nlohmann::json positions = nlohmann::json::array();

    bool HasProcedures() const { return !procedures.empty(); }
};

namespace SectorParser
{
    bool ParseFile(const std::string &path, SectorData &result);
}
