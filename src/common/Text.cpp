#include "pch.h"
#include "common/Text.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <random>

namespace Text
{
    namespace
    {
        constexpr char PAIRING_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        constexpr int PAIRING_ALPHABET_SIZE = sizeof(PAIRING_ALPHABET) - 1;
        constexpr int PAIRING_CODE_LENGTH = 5;
        constexpr size_t MAX_HOSTNAME_LENGTH = 253;
        constexpr size_t SQUAWK_LENGTH = 4;
    }

    std::string FromCString(const char *value)
    {
        return value ? std::string(value) : std::string();
    }

    std::string Trim(const std::string &value)
    {
        const char *whitespace = " \t\r\n";
        size_t first = value.find_first_not_of(whitespace);
        if (first == std::string::npos)
            return std::string();
        size_t last = value.find_last_not_of(whitespace);
        return value.substr(first, last - first + 1);
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string FormatFrequency(double megahertz)
    {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%.3f", megahertz);
        return buffer;
    }

    std::string GeneratePairingCode()
    {
        std::random_device device;
        std::mt19937 generator(device());
        std::uniform_int_distribution<int> pick(0, PAIRING_ALPHABET_SIZE - 1);

        std::string code;
        for (int i = 0; i < PAIRING_CODE_LENGTH; i++)
            code.push_back(PAIRING_ALPHABET[pick(generator)]);
        return code;
    }

    bool IsValidHostname(const std::string &value)
    {
        if (value.empty() || value.size() > MAX_HOSTNAME_LENGTH)
            return false;

        return std::all_of(value.begin(), value.end(), [](unsigned char c)
                           { return std::isalnum(c) || c == '.' || c == '-'; });
    }

    bool IsValidSquawk(const std::string &value)
    {
        return value.size() == SQUAWK_LENGTH && std::all_of(value.begin(), value.end(), [](char c)
                                                            { return c >= '0' && c <= '7'; });
    }
}
