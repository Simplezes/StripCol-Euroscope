#pragma once

#include <string>
#include <functional>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

class HttpClient
{
public:
    static std::string Get(const std::string &url, const std::string &apiKey = "");
    static std::string Post(const std::string &url, const std::string &body, const std::string &apiKey = "");
    static void GetAsync(const std::string &url, const std::string &apiKey, std::function<void(bool, const std::string &)> callback);
    static void PostAsync(const std::string &url, const std::string &body, const std::string &apiKey, std::function<void(bool, const std::string &)> callback);

private:
    static std::string PerformRequest(const std::string &url, const std::string &method, const std::string &body, const std::string &apiKey);
};
