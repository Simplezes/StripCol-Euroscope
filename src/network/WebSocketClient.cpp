#include "pch.h"
#include "network/WebSocketClient.h"
#include "network/GatewayDiscovery.h"
#include "common/Constants.h"
#include "common/Log.h"
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

using namespace StripColConstants;

namespace
{
    constexpr int RECEIVE_CHUNK_BYTES = 8192;
    constexpr size_t MAX_HANDSHAKE_BYTES = 8192;
    constexpr size_t NONCE_BYTES = 16;
    constexpr char HTTP_SWITCHING_PROTOCOLS[] = "HTTP/1.1 101";
    constexpr char HEADER_TERMINATOR[] = "\r\n\r\n";
    constexpr DWORD WAIT_SLICE_MS = 100;

    std::string Base64Encode(const std::string &data)
    {
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string encoded;
        encoded.reserve(((data.size() + 2) / 3) * 4);

        for (size_t i = 0; i < data.size(); i += 3)
        {
            unsigned chunk = static_cast<unsigned char>(data[i]) << 16;
            if (i + 1 < data.size())
                chunk |= static_cast<unsigned char>(data[i + 1]) << 8;
            if (i + 2 < data.size())
                chunk |= static_cast<unsigned char>(data[i + 2]);

            encoded.push_back(alphabet[(chunk >> 18) & 0x3F]);
            encoded.push_back(alphabet[(chunk >> 12) & 0x3F]);
            encoded.push_back(i + 1 < data.size() ? alphabet[(chunk >> 6) & 0x3F] : '=');
            encoded.push_back(i + 2 < data.size() ? alphabet[chunk & 0x3F] : '=');
        }
        return encoded;
    }

    std::string GenerateHandshakeKey()
    {
        std::random_device device;
        std::string nonce(NONCE_BYTES, '\0');
        for (char &byte : nonce)
            byte = static_cast<char>(device() & 0xFF);
        return Base64Encode(nonce);
    }
}

WebSocketClient::Link::Link(unsigned linkGeneration)
    : generation(linkGeneration), reader(MAX_MESSAGE_BYTES), lastInbound(Clock::now())
{
}

WebSocketClient::Link::~Link()
{
    Close();
}

void WebSocketClient::Link::Close()
{
    if (socket != INVALID_SOCKET)
    {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
    if (event != WSA_INVALID_EVENT)
    {
        WSACloseEvent(event);
        event = WSA_INVALID_EVENT;
    }
    reader.Reset();
}

WebSocketClient::WebSocketClient() : maskGenerator_(std::random_device{}())
{
    WSADATA data;
    winsockReady_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (!winsockReady_)
    {
        Log::Write("WSAStartup failed");
        return;
    }

    wake_ = WSACreateEvent();
    thread_ = std::thread(&WebSocketClient::ThreadMain, this);
}

WebSocketClient::~WebSocketClient()
{
    stopping_ = true;
    if (thread_.joinable())
    {
        SignalWake();
        thread_.join();
    }
    if (wake_ != WSA_INVALID_EVENT)
        WSACloseEvent(wake_);
    if (winsockReady_)
        WSACleanup();
}

void WebSocketClient::Connect(const std::string &host, const std::string &port, const std::string &greeting, bool discover)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_.host = host;
        target_.port = port;
        target_.greeting = greeting;
        target_.discover = discover;
        target_.active = true;
        target_.generation = ++generation_;
        outbound_.clear();
        inbound_.clear();
        connected_ = false;
        connectedEvent_ = false;
    }
    SignalWake();
}

void WebSocketClient::SetGreeting(const std::string &greeting)
{
    std::lock_guard<std::mutex> lock(mutex_);
    target_.greeting = greeting;
}

void WebSocketClient::Disconnect()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_.active = false;
        target_.generation = ++generation_;
        outbound_.clear();
        inbound_.clear();
        connected_ = false;
        connectedEvent_ = false;
    }
    SignalWake();
}

bool WebSocketClient::Send(std::string message)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_ || outbound_.size() >= MAX_QUEUED_MESSAGES)
            return false;
        outbound_.push_back(std::move(message));
    }
    SignalWake();
    return true;
}

bool WebSocketClient::TakeConnectedEvent()
{
    return connectedEvent_.exchange(false);
}

std::vector<std::string> WebSocketClient::TakeMessages()
{
    std::vector<std::string> messages;
    std::lock_guard<std::mutex> lock(mutex_);
    messages.swap(inbound_);
    return messages;
}

WebSocketClient::Target WebSocketClient::SnapshotTarget()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return target_;
}

bool WebSocketClient::Aborted(unsigned generation) const
{
    return stopping_ || generation_ != generation;
}

void WebSocketClient::SignalWake()
{
    if (wake_ != WSA_INVALID_EVENT)
        WSASetEvent(wake_);
}

void WebSocketClient::WaitWake(DWORD timeoutMs)
{
    WSAWaitForMultipleEvents(1, &wake_, FALSE, timeoutMs, FALSE);
    WSAResetEvent(wake_);
}

uint32_t WebSocketClient::NextMaskKey()
{
    return maskGenerator_();
}

void WebSocketClient::ThreadMain()
{
    unsigned delayMs = RECONNECT_DELAY_MIN_MS;

    while (!stopping_)
    {
        Target target = SnapshotTarget();
        if (!target.active)
        {
            WaitWake(WSA_INFINITE);
            continue;
        }

        bool opened = RunConnection(target);
        if (stopping_)
            break;

        if (SnapshotTarget().generation != target.generation)
        {
            delayMs = RECONNECT_DELAY_MIN_MS;
            continue;
        }

        if (!opened && target.discover)
            SwitchToDiscoveredHost(target);

        WaitWake(delayMs);
        delayMs = opened ? RECONNECT_DELAY_MIN_MS : std::min<unsigned>(delayMs * 2, RECONNECT_DELAY_MAX_MS);
    }
}

void WebSocketClient::SwitchToDiscoveredHost(const Target &target)
{
    const std::string host = GatewayDiscovery::Find();
    if (host.empty() || host == target.host)
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (!Aborted(target.generation))
        target_.host = host;
}

bool WebSocketClient::RunConnection(const Target &target)
{
    Link link(target.generation);

    if (!OpenSocket(target, link) || !Handshake(target, link))
        return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (Aborted(target.generation))
            return false;
        outbound_.clear();
        outbound_.push_back(target_.greeting);
        connected_ = true;
        connectedEvent_ = true;
    }

    Log::Write("Connected to gateway at " + target.host);
    Pump(link);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = false;
        outbound_.clear();
    }
    Log::Write("Gateway connection closed");
    return true;
}

bool WebSocketClient::OpenSocket(const Target &target, Link &link)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *results = nullptr;
    if (getaddrinfo(target.host.c_str(), target.port.c_str(), &hints, &results) != 0)
    {
        Log::Write("Could not resolve " + target.host);
        return false;
    }

    bool connected = false;
    for (const addrinfo *address = results; address && !connected && !Aborted(link.generation); address = address->ai_next)
    {
        link.Close();
        connected = ConnectAddress(*address, link);
    }
    freeaddrinfo(results);

    if (!connected)
    {
        link.Close();
        return false;
    }

    BOOL noDelay = TRUE;
    setsockopt(link.socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
    return true;
}

bool WebSocketClient::ConnectAddress(const addrinfo &address, Link &link)
{
    link.socket = socket(address.ai_family, address.ai_socktype, address.ai_protocol);
    if (link.socket == INVALID_SOCKET)
        return false;

    link.event = WSACreateEvent();
    if (WSAEventSelect(link.socket, link.event, FD_CONNECT | FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR)
        return false;

    int result = connect(link.socket, address.ai_addr, static_cast<int>(address.ai_addrlen));
    if (result == 0)
        return true;
    if (WSAGetLastError() != WSAEWOULDBLOCK)
        return false;

    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(CONNECT_TIMEOUT_MS);
    for (;;)
    {
        WSANETWORKEVENTS events{};
        if (WaitSocket(link, deadline, events) != Wait::Signaled)
            return false;
        if (events.lNetworkEvents & FD_CONNECT)
            return events.iErrorCode[FD_CONNECT_BIT] == 0;
    }
}

bool WebSocketClient::Handshake(const Target &target, Link &link)
{
    const std::string request =
        "GET /api HTTP/1.1\r\n"
        "Host: " + target.host + ":" + target.port + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Origin: http://" + target.host + "\r\n"
        "Sec-WebSocket-Key: " + GenerateHandshakeKey() + "\r\n"
        "Sec-WebSocket-Version: 13\r\n\r\n";

    if (!SendAll(link, request))
        return false;

    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(HANDSHAKE_TIMEOUT_MS);
    std::string response;
    char chunk[RECEIVE_CHUNK_BYTES];

    size_t headerEnd;
    while ((headerEnd = response.find(HEADER_TERMINATOR)) == std::string::npos)
    {
        if (response.size() > MAX_HANDSHAKE_BYTES)
            return false;

        int received = recv(link.socket, chunk, sizeof(chunk), 0);
        if (received > 0)
        {
            response.append(chunk, received);
            continue;
        }
        if (received == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
            return false;

        WSANETWORKEVENTS events{};
        if (WaitSocket(link, deadline, events) != Wait::Signaled)
            return false;
    }

    if (response.compare(0, sizeof(HTTP_SWITCHING_PROTOCOLS) - 1, HTTP_SWITCHING_PROTOCOLS) != 0)
    {
        Log::Write("Gateway refused the WebSocket upgrade");
        return false;
    }

    const size_t bodyStart = headerEnd + sizeof(HEADER_TERMINATOR) - 1;
    link.reader.Append(response.data() + bodyStart, response.size() - bodyStart);
    return true;
}

void WebSocketClient::Pump(Link &link)
{
    const auto pingInterval = std::chrono::seconds(PING_INTERVAL_SECONDS);
    const auto silenceLimit = std::chrono::seconds(INBOUND_SILENCE_LIMIT_SECONDS);
    Clock::time_point nextPing = Clock::now() + pingInterval;
    link.lastInbound = Clock::now();

    while (!Aborted(link.generation))
    {
        if (!ProcessFrames(link) || !FlushOutgoing(link) || !ReadAvailable(link))
            return;

        Clock::time_point now = Clock::now();
        if (now - link.lastInbound > silenceLimit)
        {
            Log::Write("Gateway stopped responding");
            return;
        }
        if (now >= nextPing)
        {
            if (!SendControl(link, WebSocketFrame::Opcode::Ping, std::string()))
                return;
            nextPing = now + pingInterval;
        }

        auto untilPing = std::chrono::duration_cast<std::chrono::milliseconds>(nextPing - Clock::now());
        DWORD timeoutMs = static_cast<DWORD>(std::max<long long>(untilPing.count(), 0));

        WSAEVENT events[2] = {link.event, wake_};
        DWORD signaled = WSAWaitForMultipleEvents(2, events, FALSE, timeoutMs, FALSE);
        if (signaled == WSA_WAIT_FAILED)
            return;
        if (signaled == WSA_WAIT_EVENT_0)
        {
            WSANETWORKEVENTS ignored{};
            WSAEnumNetworkEvents(link.socket, link.event, &ignored);
        }
        else if (signaled == WSA_WAIT_EVENT_0 + 1)
        {
            WSAResetEvent(wake_);
        }
    }
}

WebSocketClient::Wait WebSocketClient::WaitSocket(Link &link, Clock::time_point deadline, WSANETWORKEVENTS &events)
{
    for (;;)
    {
        if (Aborted(link.generation))
            return Wait::Aborted;

        Clock::time_point now = Clock::now();
        if (now >= deadline)
            return Wait::TimedOut;

        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        DWORD sliceMs = static_cast<DWORD>(std::min<long long>(remaining, WAIT_SLICE_MS));

        DWORD signaled = WSAWaitForMultipleEvents(1, &link.event, FALSE, sliceMs, FALSE);
        if (signaled == WSA_WAIT_FAILED)
            return Wait::Aborted;
        if (signaled == WSA_WAIT_EVENT_0)
        {
            WSAEnumNetworkEvents(link.socket, link.event, &events);
            return Wait::Signaled;
        }
    }
}

bool WebSocketClient::SendAll(Link &link, const std::string &bytes)
{
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(SEND_TIMEOUT_MS);
    size_t sent = 0;

    while (sent < bytes.size())
    {
        int written = send(link.socket, bytes.data() + sent, static_cast<int>(bytes.size() - sent), 0);
        if (written > 0)
        {
            sent += written;
            continue;
        }

        if (WSAGetLastError() != WSAEWOULDBLOCK)
        {
            Log::Write("Send failed: " + std::to_string(WSAGetLastError()));
            return false;
        }

        WSANETWORKEVENTS events{};
        if (WaitSocket(link, deadline, events) != Wait::Signaled)
            return false;
        if (events.lNetworkEvents & FD_CLOSE)
            return false;
    }
    return true;
}

bool WebSocketClient::SendControl(Link &link, WebSocketFrame::Opcode opcode, const std::string &payload)
{
    return SendAll(link, WebSocketFrame::Encode(opcode, payload, NextMaskKey()));
}

bool WebSocketClient::FlushOutgoing(Link &link)
{
    std::deque<std::string> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending.swap(outbound_);
    }
    if (pending.empty())
        return true;

    std::string bytes;
    for (const std::string &message : pending)
        bytes += WebSocketFrame::Encode(WebSocketFrame::Opcode::Text, message, NextMaskKey());
    return SendAll(link, bytes);
}

bool WebSocketClient::ReadAvailable(Link &link)
{
    char chunk[RECEIVE_CHUNK_BYTES];
    for (;;)
    {
        int received = recv(link.socket, chunk, sizeof(chunk), 0);
        if (received > 0)
        {
            link.reader.Append(chunk, received);
            link.lastInbound = Clock::now();
            if (!ProcessFrames(link))
                return false;
            continue;
        }
        if (received == 0)
            return false;
        return WSAGetLastError() == WSAEWOULDBLOCK;
    }
}

bool WebSocketClient::ProcessFrames(Link &link)
{
    using Result = WebSocketFrame::Reader::Result;

    std::string payload;
    for (;;)
    {
        switch (link.reader.Next(payload))
        {
        case Result::NeedMore:
            return true;
        case Result::Message:
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (inbound_.size() < MAX_QUEUED_MESSAGES)
                inbound_.push_back(std::move(payload));
            break;
        }
        case Result::Ping:
            if (!SendControl(link, WebSocketFrame::Opcode::Pong, payload))
                return false;
            break;
        case Result::Pong:
            break;
        case Result::Close:
            return false;
        case Result::Invalid:
            Log::Write("Invalid WebSocket frame from gateway");
            return false;
        }
    }
}
