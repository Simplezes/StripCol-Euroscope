#include "pch.h"
#include "HttpClient.h"
#include <vector>
#include <thread>
#include "../common/Utils.h"

std::string HttpClient::PerformRequest(const std::string& urlStr, const std::string& method, const std::string& body, const std::string& apiKey) {
    std::string responseBody = "";

    // Parse URL
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    
    int srcLen = static_cast<int>(urlStr.length());
    int wLen = MultiByteToWideChar(CP_UTF8, 0, urlStr.c_str(), srcLen, nullptr, 0);
    std::wstring wUrl(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, urlStr.c_str(), srcLen, &wUrl[0], wLen);
    
    wchar_t hostName[256] = { 0 };
    wchar_t urlPath[1024] = { 0 };

    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 1024;
    urlComp.dwSchemeLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        return "";
    }

    HINTERNET hSession = WinHttpOpen(L"StripCol Plugin/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);

    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::wstring wMethod(method.begin(), method.end());
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), urlPath,
        NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    // Add API Key Header
    if (!apiKey.empty()) {
        std::wstring header = L"X-API-Key: ";
        header += std::wstring(apiKey.begin(), apiKey.end());
        WinHttpAddRequestHeaders(hRequest, header.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    }
    
    // Add Content-Type if POST
    if (method == "POST") {
        std::wstring contentType = L"Content-Type: application/json";
        WinHttpAddRequestHeaders(hRequest, contentType.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    }

    BOOL bResults = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)(body.empty() ? WINHTTP_NO_REQUEST_DATA : body.c_str()),
        (DWORD)body.length(),
        (DWORD)body.length(), 0);

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;

        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) {
                break;
            }

            if (dwSize == 0) break;

            std::vector<char> buffer(dwSize + 1, 0);
            if (!WinHttpReadData(hRequest, (LPVOID)&buffer[0], dwSize, &dwDownloaded)) {
                break;
            }

            responseBody.append(&buffer[0], dwDownloaded);
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return responseBody;
}

std::string HttpClient::Get(const std::string& url, const std::string& apiKey) {
    return PerformRequest(url, "GET", "", apiKey);
}

std::string HttpClient::Post(const std::string& url, const std::string& body, const std::string& apiKey) {
    return PerformRequest(url, "POST", body, apiKey);
}

void HttpClient::GetAsync(const std::string& url, const std::string& apiKey, std::function<void(bool, const std::string&)> callback) {
    std::thread([url, apiKey, callback]() {
        std::string res = HttpClient::Get(url, apiKey);
        if (callback) {
            callback(!res.empty(), res);
        }
    }).detach();
}

void HttpClient::PostAsync(const std::string& url, const std::string& body, const std::string& apiKey, std::function<void(bool, const std::string&)> callback) {
    std::thread([url, body, apiKey, callback]() {
        std::string res = HttpClient::Post(url, body, apiKey);
        if (callback) {
            callback(!res.empty(), res);
        }
    }).detach();
}
