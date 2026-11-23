#pragma once

#include <string>

namespace Text
{
    std::string FromCString(const char *value);
    std::string Trim(const std::string &value);
    std::string ToLower(std::string value);
    std::string FormatFrequency(double megahertz);
    std::string GeneratePairingCode();
    bool IsValidHostname(const std::string &value);
    bool IsValidSquawk(const std::string &value);
}
