#pragma once

// Server / Editor 共享的线上协议定义。
// 所有消息 = [uint32 LE 消息总长(含头)] [uint16 LE 类型] [uint16 LE 版本] [payload]
// 多字节字段一律小端逐字段显式写入（禁止裸结构体 memcpy，规避对齐/端序问题）。

#include "Core/Base.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Engine::Net
{

    inline constexpr uint16_t kProtocolVersion = 1;

    enum class MessageType : uint16_t
    {
        // 握手
        C2S_Hello = 1,    // payload: protocolVersion(u16) + 客户端名长度(u16) + utf8 名
        S2C_Welcome,      // payload: assignedPlayerIndex(u16) + 场景YAML长度(u32) + YAML 字节
        S2C_Reject,       // payload: 原因长度(u16) + utf8 原因

        // 会话
        C2S_Input = 10,   // payload: seq(u32) + 按键位掩码(u16)
        S2C_Snapshot,     // payload: tick(u32) + 实体数(u16) + N * [UUID(u64)+pos+rot+scale (12*3 f32 LE)]
        C2S_Ping = 20,    // payload: clientTimeSec(f64)
        S2C_Pong,         // payload: 原样回传 clientTimeSec(f64)
    };

    // C2S_Input 按键位掩码
    enum InputBits : uint16_t
    {
        kInputW = 1 << 0,
        kInputS = 1 << 1,
        kInputA = 1 << 2,
        kInputD = 1 << 3,
        kInputQ = 1 << 4,
        kInputE = 1 << 5,
        kInputSpace = 1 << 6,
        kInputShift = 1 << 7,
    };

    // ── 字节缓冲：显式小端逐字段写入/读取 ──────────────────────────

    class ByteWriter
    {
    public:
        void U8(uint8_t v) { m_Buffer.push_back(v); }
        void U16(uint16_t v)
        {
            m_Buffer.push_back(static_cast<uint8_t>(v & 0xFF));
            m_Buffer.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        }
        void U32(uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                m_Buffer.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
        void U64(uint64_t v)
        {
            for (int i = 0; i < 8; ++i)
                m_Buffer.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        }
        // IEEE-754 位模式，按整数逐字节写（float 在 x86/ARM 小端平台上位模式一致）
        void F32(float v)
        {
            uint32_t bits;
            static_assert(sizeof(bits) == sizeof(v));
            std::memcpy(&bits, &v, sizeof(bits));
            U32(bits);
        }
        void F64(double v)
        {
            uint64_t bits;
            static_assert(sizeof(bits) == sizeof(v));
            std::memcpy(&bits, &v, sizeof(bits));
            U64(bits);
        }
        void Str(const std::string& s)
        {
            U16(static_cast<uint16_t>(s.size()));
            m_Buffer.insert(m_Buffer.end(), s.begin(), s.end());
        }
        void Bytes(const uint8_t* data, size_t size) { m_Buffer.insert(m_Buffer.end(), data, data + size); }

        const std::vector<uint8_t>& Data() const { return m_Buffer; }
        size_t                      Size() const { return m_Buffer.size(); }

    private:
        std::vector<uint8_t> m_Buffer;
    };

    class ByteReader
    {
    public:
        explicit ByteReader(const uint8_t* data, size_t size) : m_Data(data), m_Size(size) {}

        bool U8(uint8_t& out)
        {
            if (m_Offset + 1 > m_Size)
                return false;
            out = m_Data[m_Offset++];
            return true;
        }
        bool U16(uint16_t& out)
        {
            if (m_Offset + 2 > m_Size)
                return false;
            out = static_cast<uint16_t>(m_Data[m_Offset]) | static_cast<uint16_t>(m_Data[m_Offset + 1]) << 8;
            m_Offset += 2;
            return true;
        }
        bool U32(uint32_t& out)
        {
            if (m_Offset + 4 > m_Size)
                return false;
            out = 0;
            for (int i = 0; i < 4; ++i)
                out |= static_cast<uint32_t>(m_Data[m_Offset + i]) << (8 * i);
            m_Offset += 4;
            return true;
        }
        bool U64(uint64_t& out)
        {
            if (m_Offset + 8 > m_Size)
                return false;
            out = 0;
            for (int i = 0; i < 8; ++i)
                out |= static_cast<uint64_t>(m_Data[m_Offset + i]) << (8 * i);
            m_Offset += 8;
            return true;
        }
        bool F32(float& out)
        {
            uint32_t bits;
            if (!U32(bits))
                return false;
            std::memcpy(&out, &bits, sizeof(out));
            return true;
        }
        bool F64(double& out)
        {
            uint64_t bits;
            if (!U64(bits))
                return false;
            std::memcpy(&out, &bits, sizeof(out));
            return true;
        }
        bool Str(std::string& out)
        {
            uint16_t len;
            if (!U16(len))
                return false;
            if (m_Offset + len > m_Size)
                return false;
            out.assign(reinterpret_cast<const char*>(m_Data + m_Offset), len);
            m_Offset += len;
            return true;
        }
        bool Bytes(std::vector<uint8_t>& out, size_t size)
        {
            if (m_Offset + size > m_Size)
                return false;
            out.assign(m_Data + m_Offset, m_Data + m_Offset + size);
            m_Offset += size;
            return true;
        }

        size_t Remaining() const { return m_Size - m_Offset; }

    private:
        const uint8_t* m_Data;
        size_t         m_Size;
        size_t         m_Offset = 0;
    };

    // ── 消息封装/解析 ────────────────────────────────────────────

    struct Message
    {
        MessageType          Type = MessageType::C2S_Hello;
        std::vector<uint8_t> Payload;
    };

    // payload 打包成完整帧（头部 + payload）
    inline std::vector<uint8_t> EncodeFrame(MessageType type, const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> frame;
        uint32_t             total = static_cast<uint32_t>(payload.size()) + 8; // len(4) + type(2) + version(2)
        auto                 push  = [&frame](uint32_t v, int bytes)
        {
            for (int i = 0; i < bytes; ++i)
                frame.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
        };
        push(total, 4);
        push(static_cast<uint32_t>(type), 2);
        push(kProtocolVersion, 2);
        frame.insert(frame.end(), payload.begin(), payload.end());
        return frame;
    }

    // 从字节流解析一帧。成功时返回 true 并输出 consumed；
    // 数据不足返回 false（帧首字段完整时可信任 consumed=0 语义，由调用方继续缓冲）。
    inline bool TryDecodeFrame(const uint8_t* data, size_t size, Message& out, size_t& consumed)
    {
        consumed = 0;
        if (size < 8)
            return false;

        auto readLE = [&data](size_t offset, int bytes) -> uint32_t
        {
            uint32_t v = 0;
            for (int i = 0; i < bytes; ++i)
                v |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
            return v;
        };

        uint32_t total = readLE(0, 4);
        if (total < 8)
            return false; // 非法帧长
        if (size < total)
            return false; // 帧未收全

        uint16_t type    = static_cast<uint16_t>(readLE(4, 2));
        uint16_t version = static_cast<uint16_t>(readLE(6, 2));
        if (version != kProtocolVersion)
            return false;

        out.Type      = static_cast<MessageType>(type);
        out.Payload.assign(data + 8, data + total);
        consumed = total;
        return true;
    }

} // namespace Engine::Net
