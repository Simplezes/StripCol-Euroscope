#pragma once

#include <string>
#include <vector>
#include "sector/SectorParser.h"

namespace Messages
{
    std::string Envelope(const char *type, const std::string &dataJson);
    std::string Release(const std::string &callsign);
    std::string SectorDataMessage(const SectorData &data);
    std::string Metar(const std::string &station, const std::string &metar);
    std::string NearbyAircraft(const std::vector<std::string> &callsigns);
}
