#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <functional>
#include <vector>
#include <winsock2.h>

class WebSocketClient {
public:
    using MessageHandler = std::function<void(const std::string&)>;

    WebSocketClient(const std::string& address, const std::string& port, MessageHandler onMessage);
    ~WebSocketClient();

    void Connect();
    void Disconnect();
    bool IsConnected() const { return wsConnected; }
    bool IsRunning() const { return running; }
    
    void SendMessage(const std::string& message);
    void SetRegistrationMessage(const std::string& message) { registrationMessage = message; }
    void UpdateAddress(const std::string& newAddress) { 
        std::lock_guard<std::mutex> lock(addressMutex);
        gatewayAddress = newAddress; 
    }

    static bool CheckAvailability(const std::string& address, const std::string& port);

private:
    void WebSocketThread();
    bool AttemptConnection();
    void SendRegistration();
    void ReceiveMessages();
    std::string GetAddress() {
        std::lock_guard<std::mutex> lock(addressMutex);
        return gatewayAddress;
    }

    std::string gatewayAddress;
    std::string gatewayPort;
    MessageHandler onMessageReceived;
    std::string registrationMessage;

    SOCKET wsSocket = INVALID_SOCKET;
    std::atomic<bool> wsConnected{ false };
    std::atomic<bool> running{ false };
    std::thread wsThread;
    mutable std::mutex wsMutex;
    std::mutex addressMutex;
};
