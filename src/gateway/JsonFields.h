#pragma once

#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace JsonFields
{
    std::string Dump(const nlohmann::json &value);
    std::string String(const nlohmann::json &message, const char *key);
    std::optional<double> Number(const nlohmann::json &message, const char *key);
    std::optional<int> Integer(const nlohmann::json &message, const char *key);
}
