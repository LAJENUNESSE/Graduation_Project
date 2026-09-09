#pragma once

// Editor 侧 TCP 客户端：连接 Server，收发协议帧。
// asio IO 线程 + 互斥队列，主线程 Poll() 消费；Send 线程安全。

#include "Core/Base.h"
#include "Network/Protocol.h"

#include <asio.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace Engine::Net
{

    struct ClientEvent
    {
        Message Msg;
        bool    Connected    = false; // TCP 连上
        bool    Disconnected = false;
    };

    class TcpClient
    {
    public:
        using EventCallback = std::function<void(const ClientEvent&)>;

        TcpClient() = default;
        ~TcpClient() { Disconnect(); }

        TcpClient(const TcpClient&) = delete;
        TcpClient& operator=(const TcpClient&) = delete;

        // 阻塞式连接（局域网内毫秒级，可在菜单动作中直接调用）
        bool Connect(const std::string& host, uint16_t port);
        void Disconnect();

        void Poll(const EventCallback& callback);
        void Send(MessageType type, const std::vector<uint8_t>& payload);

        bool IsConnected() const { return m_Connected.load(); }

    private:
        void IoThread();
        void ReadHeader();
        void ReadBody();
        void DoSend();
        void PostEvent(const ClientEvent& event);

        asio::io_context                       m_IoContext;
        std::unique_ptr<asio::ip::tcp::socket> m_Socket;
        std::unique_ptr<std::thread>           m_Thread;
        std::atomic<bool>                      m_Connected{false};

        std::array<uint8_t, 8> m_HeaderBuffer{};
        std::vector<uint8_t>   m_FrameBuffer;

        std::mutex              m_SendMutex;
        std::deque<std::vector<uint8_t>> m_SendQueue;

        std::mutex              m_EventMutex;
        std::deque<ClientEvent> m_PendingEvents;
    };

} // namespace Engine::Net
