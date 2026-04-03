#pragma once

#include <string>
#include <functional>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

class HttpClient {
public:
    // Performs a synchronous GET request
    // Returns HTTP response body string, or empty string if failed.
    static std::string Get(const std::string& url, const std::string& apiKey = "");

    // Performs a synchronous POST request
    // Returns HTTP response body string, or empty string if failed.
    static std::string Post(const std::string& url, const std::string& body, const std::string& apiKey = "");

    // Performs an asynchronous GET request
    static void GetAsync(const std::string& url, const std::string& apiKey, std::function<void(bool, const std::string&)> callback);

    // Performs an asynchronous POST request
    static void PostAsync(const std::string& url, const std::string& body, const std::string& apiKey, std::function<void(bool, const std::string&)> callback);

private:
    static std::string PerformRequest(const std::string& url, const std::string& method, const std::string& body, const std::string& apiKey);
};
