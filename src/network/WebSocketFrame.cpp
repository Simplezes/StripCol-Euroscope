#include "pch.h"
#include "network/WebSocketFrame.h"

namespace WebSocketFrame
{
    namespace
    {
        constexpr unsigned char FIN_BIT = 0x80;
        constexpr unsigned char MASK_BIT = 0x80;
        constexpr unsigned char OPCODE_MASK = 0x0F;
        constexpr unsigned char LENGTH_MASK = 0x7F;
        constexpr unsigned char LENGTH_16_BIT_MARKER = 126;
        constexpr unsigned char LENGTH_64_BIT_MARKER = 127;
        constexpr size_t MAX_CONTROL_PAYLOAD = 125;
        constexpr size_t COMPACT_THRESHOLD = 64 * 1024;

        bool IsControl(unsigned char opcode)
        {
            return (opcode & 0x8) != 0;
        }
    }

    std::string Encode(Opcode opcode, const std::string &payload, uint32_t maskKey)
    {
        std::string frame;
        frame.reserve(payload.size() + 14);
        frame.push_back(static_cast<char>(FIN_BIT | static_cast<unsigned char>(opcode)));

        const uint64_t length = payload.size();
        if (length < LENGTH_16_BIT_MARKER)
        {
            frame.push_back(static_cast<char>(MASK_BIT | length));
        }
        else if (length <= 0xFFFF)
        {
            frame.push_back(static_cast<char>(MASK_BIT | LENGTH_16_BIT_MARKER));
            for (int shift = 8; shift >= 0; shift -= 8)
                frame.push_back(static_cast<char>((length >> shift) & 0xFF));
        }
        else
        {
            frame.push_back(static_cast<char>(MASK_BIT | LENGTH_64_BIT_MARKER));
            for (int shift = 56; shift >= 0; shift -= 8)
                frame.push_back(static_cast<char>((length >> shift) & 0xFF));
        }

        unsigned char key[4];
        for (int i = 0; i < 4; i++)
        {
            key[i] = static_cast<unsigned char>((maskKey >> (8 * i)) & 0xFF);
            frame.push_back(static_cast<char>(key[i]));
        }

        for (size_t i = 0; i < payload.size(); i++)
            frame.push_back(static_cast<char>(static_cast<unsigned char>(payload[i]) ^ key[i % 4]));

        return frame;
    }

    Reader::Reader(size_t maxMessageBytes) : maxMessageBytes_(maxMessageBytes)
    {
    }

    void Reader::Append(const char *data, size_t length)
    {
        buffer_.append(data, length);
    }

    void Reader::Reset()
    {
        buffer_.clear();
        offset_ = 0;
        fragments_.clear();
        inFragmentedMessage_ = false;
    }

    bool Reader::HasBytes(size_t count) const
    {
        return buffer_.size() - offset_ >= count;
    }

    void Reader::Compact()
    {
        if (offset_ == buffer_.size())
        {
            buffer_.clear();
            offset_ = 0;
        }
        else if (offset_ >= COMPACT_THRESHOLD)
        {
            buffer_.erase(0, offset_);
            offset_ = 0;
        }
    }

    Reader::Result Reader::Next(std::string &payload)
    {
        for (;;)
        {
            if (!HasBytes(2))
            {
                Compact();
                return Result::NeedMore;
            }

            const unsigned char *head = reinterpret_cast<const unsigned char *>(buffer_.data()) + offset_;
            const bool isFinal = (head[0] & FIN_BIT) != 0;
            const unsigned char opcode = head[0] & OPCODE_MASK;
            const bool isMasked = (head[1] & MASK_BIT) != 0;
            uint64_t length = head[1] & LENGTH_MASK;
            size_t headerSize = 2;

            if (length == LENGTH_16_BIT_MARKER)
            {
                headerSize += 2;
                if (!HasBytes(headerSize))
                    return Result::NeedMore;
                length = (static_cast<uint64_t>(head[2]) << 8) | head[3];
            }
            else if (length == LENGTH_64_BIT_MARKER)
            {
                headerSize += 8;
                if (!HasBytes(headerSize))
                    return Result::NeedMore;
                length = 0;
                for (int i = 0; i < 8; i++)
                    length = (length << 8) | head[2 + i];
            }

            if (length > maxMessageBytes_)
                return Result::Invalid;
            if (IsControl(opcode) && (!isFinal || length > MAX_CONTROL_PAYLOAD))
                return Result::Invalid;

            const size_t maskSize = isMasked ? 4 : 0;
            const size_t totalSize = headerSize + maskSize + static_cast<size_t>(length);
            if (!HasBytes(totalSize))
                return Result::NeedMore;

            const unsigned char *key = head + headerSize;
            const unsigned char *body = key + maskSize;
            std::string chunk(static_cast<size_t>(length), '\0');
            for (size_t i = 0; i < chunk.size(); i++)
                chunk[i] = static_cast<char>(isMasked ? (body[i] ^ key[i % 4]) : body[i]);

            offset_ += totalSize;

            switch (static_cast<Opcode>(opcode))
            {
            case Opcode::Close:
                payload = std::move(chunk);
                return Result::Close;
            case Opcode::Ping:
                payload = std::move(chunk);
                return Result::Ping;
            case Opcode::Pong:
                payload = std::move(chunk);
                return Result::Pong;
            case Opcode::Text:
            case Opcode::Binary:
                if (inFragmentedMessage_)
                    return Result::Invalid;
                if (isFinal)
                {
                    payload = std::move(chunk);
                    return Result::Message;
                }
                fragments_ = std::move(chunk);
                inFragmentedMessage_ = true;
                break;
            case Opcode::Continuation:
                if (!inFragmentedMessage_)
                    return Result::Invalid;
                fragments_ += chunk;
                if (fragments_.size() > maxMessageBytes_)
                    return Result::Invalid;
                if (isFinal)
                {
                    payload = std::move(fragments_);
                    fragments_.clear();
                    inFragmentedMessage_ = false;
                    return Result::Message;
                }
                break;
            default:
                return Result::Invalid;
            }
        }
    }
}
