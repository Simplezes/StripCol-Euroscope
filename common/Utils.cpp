#include "pch.h"
#include "Utils.h"
#include "Secrets.h"
#include <random>
#include <windows.h>
#include <fstream>
#include <unordered_map>
#include <vector>

extern HMODULE g_hModule;

namespace Utils {

    void LogMessage(const std::string& message) {
        OutputDebugStringA(("[StripCol] " + message + "\n").c_str());
    }

    std::string GeneratePairingCode() {
        const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string code;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 35);

        for (int i = 0; i < 5; i++) {
            code += chars[dis(gen)];
        }

        return code;
    }

    std::string Base64Encode(const std::vector<unsigned char>& data) {
        static const char lookup[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string out;
        out.reserve(((data.size() + 2) / 3) * 4);

        int val = 0;
        int valb = -6;

        for (unsigned char c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out.push_back(lookup[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }

        if (valb > -6) out.push_back(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');

        return out;
    }

    std::string GenerateWebSocketKey() {
        std::vector<unsigned char> nonce(16);
        for (int i = 0; i < 16; i++) {
            nonce[i] = rand() % 256;
        }
        return Base64Encode(nonce);
    }

    static std::unordered_map<std::string, std::string> envVars;

    void LoadEnv(const std::string& filename) {
        std::string fullPath = filename;
        char dllPath[MAX_PATH];
        if (GetModuleFileNameA(g_hModule, dllPath, MAX_PATH) != 0) {
            std::string path(dllPath);
            std::string dir = path.substr(0, path.find_last_of("\\/"));
            fullPath = dir + "\\" + filename;
        }

        std::ifstream file(fullPath);
        if (!file.is_open()) return;

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                key.erase(0, key.find_first_not_of(" \t\r\n"));
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                value.erase(value.find_last_not_of(" \t\r\n") + 1);

                envVars[key] = value;
            }
        }
    }

    std::string GetEnv(const std::string& key) {
        if (envVars.count(key)) {
            return envVars[key];
        }
        
        if (key == "STRIPCOL_API_KEY") {
            return STRIPCOL_SECRET_API_KEY;
        }

        if (key == "STRIPCOL_BASE_URL") {
            return STRIPCOL_SECRET_BASE_URL;
        }

        return "";
    }
}
