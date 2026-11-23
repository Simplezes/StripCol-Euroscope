#include "pch.h"
#include "network/WebSocketClient.h"
#include "common/Utils.h"
#include "common/Constants.h"
#include <ws2tcpip.h>
#include <sstream>
#include <random>

#pragma comment(lib, "ws2_32.lib")

WebSocketClient::WebSocketClient(const std::string &address, const std::string &port, MessageHandler onMessage)
    : gatewayAddress(address), gatewayPort(port), onMessageReceived(onMessage)
{
}

WebSocketClient::~WebSocketClient()
{
    Disconnect();
}

void WebSocketClient::Connect()
{
    if (running)
        return;

    running = true;
    wsThread = std::thread(&WebSocketClient::WebSocketThread, this);
}

void WebSocketClient::Disconnect()
{
    running = false;
    wsConnected = false;

    {
        std::lock_guard<std::mutex> lock(wsMutex);
        if (wsSocket != INVALID_SOCKET)
        {
            shutdown(wsSocket, SD_BOTH);
            closesocket(wsSocket);
            wsSocket = INVALID_SOCKET;
        }
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        while (!outgoingQueue.empty())
            outgoingQueue.pop();
    }
    queueCV.notify_all();

    if (wsThread.joinable())
    {
        wsThread.join();
    }
}

void WebSocketClient::WebSocketThread()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        Utils::LogMessage("WSAStartup failed");
        running = false;
        return;
    }

    auto lastPing = std::chrono::steady_clock::now();

    while (running)
    {
        if (!AttemptConnection())
        {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        SendRegistration();

        while (running && wsConnected)
        {
            ReceiveMessages();
            ProcessOutgoingQueue();

            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPing).count() >= StripColConstants::WEBSOCKET_PING_INTERVAL_SEC)
            {
                SendMessage("{\"type\":\"ping\"}");
                lastPing = now;
            }
        }

        wsConnected = false;
        {
            std::lock_guard<std::mutex> lock(wsMutex);
            if (wsSocket != INVALID_SOCKET)
            {
                closesocket(wsSocket);
                wsSocket = INVALID_SOCKET;
            }
        }

        if (running)
        {
            Utils::LogMessage("Connection lost, retrying...");
            m_recvBuffer.clear();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    WSACleanup();
}

bool WebSocketClient::AttemptConnection()
{
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    std::string addr = GetAddress();
    if (getaddrinfo(addr.c_str(), gatewayPort.c_str(), &hints, &res) != 0)
    {
        Utils::LogMessage("DNS resolution failed for " + addr);
        return false;
    }

    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET)
    {
        Utils::LogMessage("Socket creation failed");
        freeaddrinfo(res);
        return false;
    }

    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR)
    {
        Utils::LogMessage("Connect failed: " + std::to_string(WSAGetLastError()));
        freeaddrinfo(res);
        closesocket(sock);
        return false;
    }
    freeaddrinfo(res);

    std::string key = Utils::GenerateWebSocketKey();
    std::string handshake =
        "GET /api HTTP/1.1\r\n"
        "Host: " +
        addr + ":" + gatewayPort + "\r\n"
                                   "Upgrade: websocket\r\n"
                                   "Connection: Upgrade\r\n"
                                   "Origin: http://" +
        addr + "\r\n"
               "Sec-WebSocket-Key: " +
        key + "\r\n"
              "Sec-WebSocket-Version: 13\r\n\r\n";

    if (send(sock, handshake.c_str(), (int)handshake.size(), 0) == SOCKET_ERROR)
    {
        Utils::LogMessage("Handshake send failed");
        closesocket(sock);
        return false;
    }

    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    char buffer[2048];
    int bytesRead = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesRead <= 0)
    {
        closesocket(sock);
        return false;
    }

    buffer[bytesRead] = '\0';
    std::string response(buffer);
    if (response.find("101") == std::string::npos)
    {
        closesocket(sock);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(wsMutex);
        wsSocket = sock;
    }

    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));
    wsConnected = true;

    SendMessage("{\"type\":\"sync-request\"}");
    Utils::LogMessage("WebSocket connected successfully.");
    return true;
}

void WebSocketClient::ProcessOutgoingQueue()
{
    std::string msg;
    while (wsConnected && running)
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (outgoingQueue.empty())
                break;
            msg = outgoingQueue.front();
            outgoingQueue.pop();
        }
        SendWebSocketFrame(msg);
    }
}

void WebSocketClient::SendRegistration()
{
    if (!registrationMessage.empty())
    {
        SendMessage(registrationMessage);
    }
}

void WebSocketClient::SendMessage(const std::string &message)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        outgoingQueue.push(message);
    }
    queueCV.notify_all();
}

void WebSocketClient::SendWebSocketFrame(const std::string &message)
{
    std::lock_guard<std::mutex> lock(wsMutex);
    if (wsSocket == INVALID_SOCKET || !wsConnected)
        return;

    size_t msgLen = message.size();
    std::vector<unsigned char> frame;
    frame.push_back(0x81);

    unsigned char maskKey[4];
    {
        thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<unsigned int> byteDist(0, 255);
        for (int i = 0; i < 4; i++)
            maskKey[i] = static_cast<unsigned char>(byteDist(rng));
    }

    if (msgLen < 126)
    {
        frame.push_back(0x80 | (unsigned char)msgLen);
    }
    else if (msgLen < 65536)
    {
        frame.push_back(0x80 | 126);
        frame.push_back((unsigned char)((msgLen >> 8) & 0xFF));
        frame.push_back((unsigned char)(msgLen & 0xFF));
    }
    else
    {
        Utils::LogMessage("Message too large to send (" + std::to_string(msgLen) + " bytes), dropping");
        return;
    }

    for (int i = 0; i < 4; i++)
        frame.push_back(maskKey[i]);
    for (size_t i = 0; i < msgLen; i++)
        frame.push_back((unsigned char)(message[i] ^ maskKey[i % 4]));

    if (send(wsSocket, (const char *)frame.data(), (int)frame.size(), 0) == SOCKET_ERROR)
    {
        Utils::LogMessage("Send failed: " + std::to_string(WSAGetLastError()));
        wsConnected = false;
    }
}

void WebSocketClient::ReceiveMessages()
{
    fd_set readfds;
    FD_ZERO(&readfds);

    {
        std::lock_guard<std::mutex> lock(wsMutex);
        if (wsSocket == INVALID_SOCKET)
            return;
        FD_SET(wsSocket, &readfds);
    }

    timeval timeout{0, 50000};
    if (select(0, &readfds, NULL, NULL, &timeout) <= 0)
        return;

    char buf[4096];
    int bytesRead;
    {
        std::lock_guard<std::mutex> lock(wsMutex);
        if (wsSocket == INVALID_SOCKET)
            return;
        bytesRead = recv(wsSocket, buf, sizeof(buf), 0);
    }

    if (bytesRead <= 0)
    {
        wsConnected = false;
        Utils::LogMessage("Connection closed by server");
        return;
    }

    m_recvBuffer.insert(m_recvBuffer.end(),
                        reinterpret_cast<unsigned char *>(buf),
                        reinterpret_cast<unsigned char *>(buf) + bytesRead);

    const size_t MAX_RECV_BUFFER = 1 * 1024 * 1024;
    if (m_recvBuffer.size() > MAX_RECV_BUFFER)
    {
        Utils::LogMessage("Receive buffer exceeded 1 MB, dropping connection");
        wsConnected = false;
        m_recvBuffer.clear();
        return;
    }

    while (m_recvBuffer.size() >= 2)
    {
        unsigned char opcode = m_recvBuffer[0] & 0x0F;
        bool masked = (m_recvBuffer[1] & 0x80) != 0;
        size_t lenByte = m_recvBuffer[1] & 0x7F;

        size_t extLenBytes = 0;
        if (lenByte == 126)
            extLenBytes = 2;
        else if (lenByte == 127)
            extLenBytes = 8;

        if (m_recvBuffer.size() < 2 + extLenBytes)
            break;

        size_t payloadLen = lenByte;
        if (lenByte == 126)
        {
            payloadLen = (static_cast<size_t>(m_recvBuffer[2]) << 8) | m_recvBuffer[3];
        }
        else if (lenByte == 127)
        {
            payloadLen = 0;
            for (int i = 0; i < 8; i++)
                payloadLen = (payloadLen << 8) | m_recvBuffer[2 + i];
        }

        size_t maskOffset = 2 + extLenBytes;
        size_t payloadStart = maskOffset + (masked ? 4 : 0);
        size_t totalLen = payloadStart + payloadLen;

        if (m_recvBuffer.size() < totalLen)
            break;

        if (opcode == 0x08)
        {
            wsConnected = false;
            Utils::LogMessage("Connection closed by server");
            m_recvBuffer.erase(m_recvBuffer.begin(), m_recvBuffer.begin() + totalLen);
            return;
        }
        else if (opcode == 0x09)
        {
            unsigned char pong[] = {0x8A, 0x80, 0x00, 0x00, 0x00, 0x00};
            std::lock_guard<std::mutex> lock(wsMutex);
            if (wsSocket != INVALID_SOCKET)
                send(wsSocket, reinterpret_cast<const char *>(pong), sizeof(pong), 0);
        }
        else if (opcode == 0x01 || opcode == 0x02)
        {
            std::string payload;
            payload.reserve(payloadLen);

            if (masked)
            {
                const unsigned char *key = &m_recvBuffer[maskOffset];
                for (size_t i = 0; i < payloadLen; i++)
                    payload.push_back(static_cast<char>(m_recvBuffer[payloadStart + i] ^ key[i % 4]));
            }
            else
            {
                payload.assign(reinterpret_cast<const char *>(&m_recvBuffer[payloadStart]), payloadLen);
            }

            if (onMessageReceived)
                onMessageReceived(payload);
        }

        m_recvBuffer.erase(m_recvBuffer.begin(), m_recvBuffer.begin() + totalLen);
    }
}

bool WebSocketClient::CheckAvailability(const std::string &address, const std::string &port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(address.c_str(), port.c_str(), &hints, &res) != 0)
    {
        WSACleanup();
        return false;
    }

    SOCKET testSocket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (testSocket == INVALID_SOCKET)
    {
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

    timeval timeout{0, 500000};
    int result = select(0, NULL, &writefds, NULL, &timeout);

    closesocket(testSocket);
    WSACleanup();
    return result > 0;
}
