#include "pch.h"
#include "Utils.h"
#include <random>
#include <windows.h>
#include <vector>

extern HMODULE g_hModule;

namespace Utils
{

    void LogMessage(const std::string &message)
    {
        OutputDebugStringA(("[StripCol] " + message + "\n").c_str());
    }

    std::string GeneratePairingCode()
    {
        const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string code;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 35);

        for (int i = 0; i < 5; i++)
        {
            code += chars[dis(gen)];
        }

        return code;
    }

    std::string Base64Encode(const std::vector<unsigned char> &data)
    {
        static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);

        int val = 0;
        int valb = -6;

        for (unsigned char c : data)
        {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0)
            {
                out.push_back(lookup[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }

        if (valb > -6)
            out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4)
            out.push_back('=');

        return out;
    }

    std::string GenerateWebSocketKey()
    {
        std::vector<unsigned char> nonce(16);
        std::random_device rd;
        std::uniform_int_distribution<unsigned int> dist(0, 255);
        for (int i = 0; i < 16; i++)
        {
            nonce[i] = static_cast<unsigned char>(dist(rd));
        }
        return Base64Encode(nonce);
    }

}
