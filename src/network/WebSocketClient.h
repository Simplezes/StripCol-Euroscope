#pragma once

#include "network/WebSocketFrame.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

class WebSocketClient
{
public:
    WebSocketClient();
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient &) = delete;
    WebSocketClient &operator=(const WebSocketClient &) = delete;

    void Connect(const std::string &host, const std::string &port, const std::string &greeting, bool discover);
    void SetGreeting(const std::string &greeting);
    void Disconnect();

    bool Send(std::string message);
    bool IsConnected() const { return connected_; }
    bool TakeConnectedEvent();
    std::vector<std::string> TakeMessages();

private:
    using Clock = std::chrono::steady_clock;

    struct Target
    {
        std::string host;
        std::string port;
        std::string greeting;
        bool discover = false;
        bool active = false;
        unsigned generation = 0;
    };

    struct Link
    {
        explicit Link(unsigned linkGeneration);
        ~Link();
        Link(const Link &) = delete;
        Link &operator=(const Link &) = delete;

        void Close();

        SOCKET socket = INVALID_SOCKET;
        WSAEVENT event = WSA_INVALID_EVENT;
        unsigned generation;
        WebSocketFrame::Reader reader;
        Clock::time_point lastInbound;
    };

    enum class Wait
    {
        Signaled,
        TimedOut,
        Aborted
    };

    void ThreadMain();
    bool RunConnection(const Target &target);
    void SwitchToDiscoveredHost(const Target &target);
    bool OpenSocket(const Target &target, Link &link);
    bool ConnectAddress(const addrinfo &address, Link &link);
    bool Handshake(const Target &target, Link &link);
    void Pump(Link &link);

    Wait WaitSocket(Link &link, Clock::time_point deadline, WSANETWORKEVENTS &events);
    bool SendAll(Link &link, const std::string &bytes);
    bool FlushOutgoing(Link &link);
    bool ReadAvailable(Link &link);
    bool ProcessFrames(Link &link);
    bool SendControl(Link &link, WebSocketFrame::Opcode opcode, const std::string &payload);

    Target SnapshotTarget();
    bool Aborted(unsigned generation) const;
    void WaitWake(DWORD timeoutMs);
    void SignalWake();
    uint32_t NextMaskKey();

    std::mutex mutex_;
    Target target_;
    std::deque<std::string> outbound_;
    std::vector<std::string> inbound_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> connectedEvent_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<unsigned> generation_{0};

    bool winsockReady_ = false;
    WSAEVENT wake_ = WSA_INVALID_EVENT;
    std::mt19937 maskGenerator_;
    std::thread thread_;
};
