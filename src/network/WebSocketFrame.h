#pragma once

#include <cstdint>
#include <string>

namespace WebSocketFrame
{
    enum class Opcode : unsigned char
    {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA
    };

    std::string Encode(Opcode opcode, const std::string &payload, uint32_t maskKey);

    class Reader
    {
    public:
        enum class Result
        {
            NeedMore,
            Message,
            Ping,
            Pong,
            Close,
            Invalid
        };

        explicit Reader(size_t maxMessageBytes);

        void Append(const char *data, size_t length);
        Result Next(std::string &payload);
        void Reset();

    private:
        bool HasBytes(size_t count) const;
        void Compact();

        size_t maxMessageBytes_;
        std::string buffer_;
        size_t offset_ = 0;
        std::string fragments_;
        bool inFragmentedMessage_ = false;
    };
}
