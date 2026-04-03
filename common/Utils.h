#pragma once

#include <string>
#include <vector>

namespace Utils {
    void LogMessage(const std::string& message);
    std::string GeneratePairingCode();
    std::string Base64Encode(const std::vector<unsigned char>& data);
    std::string GenerateWebSocketKey();
    void LoadEnv(const std::string& filepath);
    std::string GetEnv(const std::string& key);
}
