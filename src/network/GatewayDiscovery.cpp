#include "pch.h"
#include "network/GatewayDiscovery.h"
#include "common/Constants.h"
#include "common/Log.h"
#include <iphlpapi.h>
#include <cstring>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")

namespace
{
    constexpr ULONG ADAPTER_BUFFER_BYTES = 16 * 1024;
    constexpr int ADAPTER_READ_ATTEMPTS = 3;

    struct Probe
    {
        SOCKET socket = INVALID_SOCKET;
        std::string adapterAddress;
    };

    bool ReadAdapters(std::vector<char> &buffer)
    {
        ULONG size = ADAPTER_BUFFER_BYTES;
        const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;

        for (int attempt = 0; attempt < ADAPTER_READ_ATTEMPTS; ++attempt)
        {
            buffer.resize(size);
            auto *first = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
            const ULONG result = GetAdaptersAddresses(AF_INET, flags, nullptr, first, &size);
            if (result == NO_ERROR)
                return true;
            if (result != ERROR_BUFFER_OVERFLOW)
                break;
        }
        return false;
    }

    bool BroadcastFor(const IP_ADAPTER_UNICAST_ADDRESS &unicast, in_addr &local, in_addr &broadcast)
    {
        const unsigned prefix = unicast.OnLinkPrefixLength;
        if (prefix == 0 || prefix >= 32)
            return false;

        const auto *address = reinterpret_cast<const sockaddr_in *>(unicast.Address.lpSockaddr);
        const unsigned long hostOrderAddress = ntohl(address->sin_addr.S_un.S_addr);
        const unsigned long mask = 0xFFFFFFFFul << (32 - prefix);

        local = address->sin_addr;
        broadcast.S_un.S_addr = htonl(hostOrderAddress | ~mask);
        return true;
    }

    bool OpenProbe(const in_addr &local, const in_addr &broadcast, Probe &probe)
    {
        SOCKET udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (udp == INVALID_SOCKET)
            return false;

        BOOL allowBroadcast = TRUE;
        setsockopt(udp, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&allowBroadcast), sizeof(allowBroadcast));

        sockaddr_in bindAddress{};
        bindAddress.sin_family = AF_INET;
        bindAddress.sin_addr = local;
        if (bind(udp, reinterpret_cast<sockaddr *>(&bindAddress), sizeof(bindAddress)) == SOCKET_ERROR)
        {
            closesocket(udp);
            return false;
        }

        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(StripColConstants::DISCOVERY_PORT);
        target.sin_addr = broadcast;

        const char *request = StripColConstants::DISCOVERY_REQUEST;
        sendto(udp, request, static_cast<int>(std::strlen(request)), 0, reinterpret_cast<sockaddr *>(&target), sizeof(target));

        probe.socket = udp;
        return true;
    }

    std::vector<Probe> OpenProbes()
    {
        std::vector<char> buffer;
        std::vector<Probe> probes;
        if (!ReadAdapters(buffer))
            return probes;

        auto *adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
        for (; adapter; adapter = adapter->Next)
        {
            if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
                continue;

            for (auto *unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
            {
                in_addr local{};
                in_addr broadcast{};
                Probe probe;
                if (BroadcastFor(*unicast, local, broadcast) && OpenProbe(local, broadcast, probe))
                    probes.push_back(probe);
            }
        }
        return probes;
    }

    std::string WaitForReply(const std::vector<Probe> &probes)
    {
        fd_set readable;
        FD_ZERO(&readable);
        for (const Probe &probe : probes)
            FD_SET(probe.socket, &readable);

        timeval timeout{};
        timeout.tv_sec = StripColConstants::DISCOVERY_WAIT_MS / 1000;
        timeout.tv_usec = (StripColConstants::DISCOVERY_WAIT_MS % 1000) * 1000;
        if (select(0, &readable, nullptr, nullptr, &timeout) <= 0)
            return std::string();

        const size_t expectedLength = std::strlen(StripColConstants::DISCOVERY_REPLY);
        for (const Probe &probe : probes)
        {
            if (!FD_ISSET(probe.socket, &readable))
                continue;

            char reply[64];
            sockaddr_in sender{};
            int senderLength = sizeof(sender);
            const int received = recvfrom(probe.socket, reply, sizeof(reply), 0, reinterpret_cast<sockaddr *>(&sender), &senderLength);
            if (received < static_cast<int>(expectedLength) || std::strncmp(reply, StripColConstants::DISCOVERY_REPLY, expectedLength) != 0)
                continue;

            char host[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, &sender.sin_addr, host, sizeof(host)))
                return host;
        }
        return std::string();
    }
}

namespace GatewayDiscovery
{
    std::string Find()
    {
        std::vector<Probe> probes = OpenProbes();
        if (probes.empty())
            return std::string();

        const std::string host = WaitForReply(probes);
        for (const Probe &probe : probes)
            closesocket(probe.socket);

        if (!host.empty())
            Log::Write("Discovered gateway at " + host);
        return host;
    }
}
