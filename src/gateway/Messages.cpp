#include "pch.h"
#include "gateway/Messages.h"
#include "gateway/JsonFields.h"
#include <nlohmann/json.hpp>

namespace Messages
{
    std::string Envelope(const char *type, const std::string &dataJson)
    {
        std::string message = "{\"type\":\"";
        message += type;
        message += "\",\"data\":";
        message += dataJson;
        message += "}";
        return message;
    }

    std::string Release(const std::string &callsign)
    {
        nlohmann::json message;
        message["type"] = "release";
        message["callsign"] = callsign;
        return JsonFields::Dump(message);
    }

    std::string Metar(const std::string &station, const std::string &metar)
    {
        nlohmann::json message;
        message["type"] = "metar";
        message["data"]["station"] = station;
        message["data"]["metar"] = metar;
        return JsonFields::Dump(message);
    }

    std::string SectorDataMessage(const SectorData &data)
    {
        nlohmann::json message;
        message["type"] = "sector-data";
        message["data"]["procedures"] = data.procedures;
        message["data"]["sectors"] = data.sectors;
        message["data"]["transfers"] = data.transfers;
        message["data"]["positions"] = data.positions;
        return JsonFields::Dump(message);
    }

    std::string NearbyAircraft(const std::vector<std::string> &callsigns)
    {
        nlohmann::json message;
        message["type"] = "nearby-aircraft";
        message["data"] = callsigns;
        return JsonFields::Dump(message);
    }
}
