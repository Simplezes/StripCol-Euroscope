#pragma once

#include <string>
#include <vector>

namespace Messages
{
    std::string Envelope(const char *type, const std::string &dataJson);
    std::string Release(const std::string &callsign);
    std::string NearbyAircraft(const std::vector<std::string> &callsigns);
}
