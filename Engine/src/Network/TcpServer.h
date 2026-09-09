#pragma once

// 基于 asio 的多客户端 TCP 服务器（供 Server 进程使用）。
// asio io_context 跑在独立 IO 线程，回调经互斥队列转交主线程 Poll() 消费，
// 广播线程安全（发送队列 per-session + strand）。

#include "Core/Base.h"
#include "Network/Protocol.h"

#include <asio.hpp>

#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace Engine::Net
{

    struct ServerEvent
    {
        uint64_t     SessionId = 0;
        Message      Msg; // 仅收到的客户端消息；连接/断开用下面标志表达
        bool         Connected = false;
        bool         Disconnected = false;
    };

    class TcpServer
    {
    public:
        using EventCallback = std::function<void(const ServerEvent&)>;

        TcpServer() = default;
        ~TcpServer() { Stop(); }

        TcpServer(const TcpServer&) = delete;
        TcpServer& operator=(const TcpServer&) = delete;

        // 返回是否成功监听
        bool Start(uint16_t port);
        void Stop();

        // 主线程每帧调用，取走 IO 线程投递的事件
        void Poll(const EventCallback& callback);

        // 线程安全：向指定会话发送；sessionId 无效时静默丢弃
        void Send(uint64_t sessionId, MessageType type, const std::vector<uint8_t>& payload);

        // 线程安全：向所有会话广播（可选排除一个会话）
        void Broadcast(MessageType type, const std::vector<uint8_t>& payload, uint64_t excludeSession = 0);

        size_t SessionCount() const;

        uint16_t Port() const { return m_Port; }

    private:
        // 单 IO 线程模型：所有会话状态仅在 io_context.run() 线程读写，
        // 发送经 asio::post(m_IoContext) 排队，无需 strand。
        struct Session
        {
            uint64_t                                        Id = 0;
            std::shared_ptr<asio::ip::tcp::socket>          Socket;
            std::deque<std::vector<uint8_t>>                SendQueue;
            std::array<uint8_t, 8>                          HeaderBuffer{};
            std::vector<uint8_t>                            FrameBuffer; // 头之后的 payload 缓冲
        };
        using SessionPtr = std::shared_ptr<Session>;

        void DoAccept();
        void ReadHeader(SessionPtr session);
        void ReadBody(SessionPtr session);
        void DoSend(SessionPtr session);
        void PostEvent(const ServerEvent& event);
        void CloseSession(uint64_t sessionId);

        asio::io_context                             m_IoContext;
        std::unique_ptr<asio::ip::tcp::acceptor>     m_Acceptor;
        std::unique_ptr<std::thread>                 m_IoThread;
        std::set<std::shared_ptr<Session>>           m_Sessions; // IO 线程读写
        uint64_t                                     m_NextSessionId = 1;
        uint16_t                                     m_Port          = 0;

        std::mutex                m_EventMutex;
        std::deque<ServerEvent>   m_PendingEvents; // IO 线程生产，主线程消费
    };

} // namespace Engine::Net
