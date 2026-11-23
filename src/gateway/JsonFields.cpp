#include "pch.h"
#include "gateway/JsonFields.h"
#include <cmath>
#include <cstdlib>
#include <limits>

namespace JsonFields
{
    std::string Dump(const nlohmann::json &value)
    {
        return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string String(const nlohmann::json &message, const char *key)
    {
        auto field = message.find(key);
        if (field == message.end() || !field->is_string())
            return std::string();
        return field->get<std::string>();
    }

    std::optional<double> Number(const nlohmann::json &message, const char *key)
    {
        auto field = message.find(key);
        if (field == message.end())
            return std::nullopt;

        if (field->is_number())
        {
            double value = field->get<double>();
            return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
        }

        if (field->is_string())
        {
            const std::string text = field->get<std::string>();
            if (text.empty())
                return std::nullopt;

            char *end = nullptr;
            double value = std::strtod(text.c_str(), &end);
            if (*end != '\0' || !std::isfinite(value))
                return std::nullopt;
            return value;
        }

        return std::nullopt;
    }

    std::optional<int> Integer(const nlohmann::json &message, const char *key)
    {
        std::optional<double> value = Number(message, key);
        if (!value)
            return std::nullopt;

        double rounded = std::round(*value);
        if (rounded < std::numeric_limits<int>::min() || rounded > std::numeric_limits<int>::max())
            return std::nullopt;
        return static_cast<int>(rounded);
    }

    std::optional<bool> Boolean(const nlohmann::json &message, const char *key)
    {
        auto field = message.find(key);
        if (field == message.end() || !field->is_boolean())
            return std::nullopt;
        return field->get<bool>();
    }
}
