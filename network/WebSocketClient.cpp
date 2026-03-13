#include "pch.h"
#include "WebSocketClient.h"
#include "Utils.h"
#include <ws2tcpip.h>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

WebSocketClient::WebSocketClient(const std::string& address, const std::string& port, MessageHandler onMessage)
    : gatewayAddress(address), gatewayPort(port), onMessageReceived(onMessage) {
}

WebSocketClient::~WebSocketClient() {
    Disconnect();
}

void WebSocketClient::Connect() {
    if (running) return;

    running = true;
    wsThread = std::thread(&WebSocketClient::WebSocketThread, this);
}

void WebSocketClient::Disconnect() {
    running = false;
    wsConnected = false;

    {
        std::lock_guard<std::mutex> lock(wsMutex);
        if (wsSocket != INVALID_SOCKET) {
            shutdown(wsSocket, SD_BOTH);
            closesocket(wsSocket);
            wsSocket = INVALID_SOCKET;
        }
    }

    if (wsThread.joinable()) {
        wsThread.join();
    }
}

void WebSocketClient::WebSocketThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        Utils::LogMessage("WSAStartup failed");
        running = false;
        return;
    }

    auto lastPing = std::chrono::steady_clock::now();

    while (running) {
        if (!AttemptConnection()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        SendRegistration();

        while (running && wsConnected) {
            ReceiveMessages();

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPing).count() >= 30) {
                SendMessage("{\"type\":\"ping\"}");
                lastPing = now;
            }

            if (running && wsConnected) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        wsConnected = false;
        {
            std::lock_guard<std::mutex> lock(wsMutex);
            if (wsSocket != INVALID_SOCKET) {
                closesocket(wsSocket);
                wsSocket = INVALID_SOCKET;
            }
        }

        if (running) {
            Utils::LogMessage("Connection lost, retrying...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    WSACleanup();
}

bool WebSocketClient::AttemptConnection() {
    struct addrinfo hints {}, * res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string addr = GetAddress();
    if (getaddrinfo(addr.c_str(), gatewayPort.c_str(), &hints, &res) != 0) {
        Utils::LogMessage("DNS resolution failed for " + addr);
        return false;
    }

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) {
        Utils::LogMessage("Socket creation failed");
        freeaddrinfo(res);
        return false;
    }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        Utils::LogMessage("Connect failed: " + std::to_string(WSAGetLastError()));
        freeaddrinfo(res);
        closesocket(sock);
        return false;
    }
    freeaddrinfo(res);

    std::string key = Utils::GenerateWebSocketKey();
    std::string handshake =
        "GET /api HTTP/1.1\r\n"
        "Host: " + addr + ":" + gatewayPort + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Origin: http://" + addr + "\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    if (send(sock, handshake.c_str(), (int)handshake.size(), 0) == SOCKET_ERROR) {
        Utils::LogMessage("Handshake send failed");
        closesocket(sock);
        return false;
    }

    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    char buffer[2048];
    int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0) {
        closesocket(sock);
        return false;
    }

    buffer[bytesRead] = '\0';
    std::string response(buffer);
    if (response.find("101") == std::string::npos) {
        closesocket(sock);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(wsMutex);
        wsSocket = sock;
    }

    int flag = 1;
    setsockopt(wsSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int));
    wsConnected = true;

    SendMessage("{\"type\":\"ping\"}");
    Utils::LogMessage("WebSocket connected successfully.");
    return true;
}

void WebSocketClient::SendRegistration() {
    if (!registrationMessage.empty()) {
        SendMessage(registrationMessage);
    }
}

void WebSocketClient::SendMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(wsMutex);
    if (wsSocket == INVALID_SOCKET || !wsConnected) return;

    size_t msgLen = message.size();
    std::vector<unsigned char> frame;
    frame.push_back(0x81);

    unsigned char maskKey[4];
    for (int i = 0; i < 4; i++) maskKey[i] = rand() % 256;

    if (msgLen < 126) {
        frame.push_back(0x80 | (unsigned char)msgLen);
    } else if (msgLen < 65536) {
        frame.push_back(0x80 | 126);
        frame.push_back((msgLen >> 8) & 0xFF);
        frame.push_back(msgLen & 0xFF);
    } else {
        return;
    }

    for (int i = 0; i < 4; i++) frame.push_back(maskKey[i]);
    for (size_t i = 0; i < msgLen; i++) frame.push_back(message[i] ^ maskKey[i % 4]);

    if (send(wsSocket, (const char*)frame.data(), (int)frame.size(), 0) == SOCKET_ERROR) {
        Utils::LogMessage("Send failed: " + std::to_string(WSAGetLastError()));
        wsConnected = false;
    }
}

void WebSocketClient::ReceiveMessages() {
    char buffer[4096];
    fd_set readfds;
    FD_ZERO(&readfds);

    {
        std::lock_guard<std::mutex> lock(wsMutex);
        if (wsSocket == INVALID_SOCKET) return;
        FD_SET(wsSocket, &readfds);
    }

    timeval timeout{ 1, 0 };
    int activity = select(0, &readfds, NULL, NULL, &timeout);

    if (activity > 0) {
        int bytesRead;
        {
            std::lock_guard<std::mutex> lock(wsMutex);
            if (wsSocket == INVALID_SOCKET) return;
            bytesRead = recv(wsSocket, buffer, sizeof(buffer) - 1, 0);
        }

        if (bytesRead <= 0) {
            wsConnected = false;
            Utils::LogMessage("Connection closed by server");
            return;
        }

        if (bytesRead >= 2) {
            unsigned char opcode = buffer[0] & 0x0F;
            if (opcode == 0x08) {
                wsConnected = false;
                return;
            } else if (opcode == 0x01) {
                unsigned char payloadLen = buffer[1] & 0x7F;
                int payloadStart = 2;
                size_t actualLen = payloadLen;

                if (payloadLen == 126) {
                    actualLen = (static_cast<unsigned char>(buffer[2]) << 8) | static_cast<unsigned char>(buffer[3]);
                    payloadStart = 4;
                }

                if (payloadStart + actualLen <= static_cast<size_t>(bytesRead)) {
                    std::string message(buffer + payloadStart, actualLen);
                    if (onMessageReceived) {
                        onMessageReceived(message);
                    }
                }
            }
        }
    }
}

bool WebSocketClient::CheckAvailability(const std::string& address, const std::string& port) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

    struct addrinfo hints {}, * res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(address.c_str(), port.c_str(), &hints, &res) != 0) {
        WSACleanup();
        return false;
    }

    SOCKET testSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (testSocket == INVALID_SOCKET) {
        freeaddrinfo(res);
        WSACleanup();
        return false;
    }

    u_long mode = 1;
    ioctlsocket(testSocket, FIONBIO, &mode);
    connect(testSocket, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(testSocket, &writefds);

    timeval timeout{ 1, 0 };
    int result = select(0, NULL, &writefds, NULL, &timeout);

    closesocket(testSocket);
    WSACleanup();
    return result > 0;
}
