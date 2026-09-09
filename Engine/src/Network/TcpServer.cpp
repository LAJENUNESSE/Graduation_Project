#include "engpch.h"
#include "Network/TcpServer.h"

#include "Core/Log.h"

namespace Engine::Net
{

    bool TcpServer::Start(uint16_t port)
    {
        try
        {
            auto endpoint = asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port);
            m_Acceptor    = std::make_unique<asio::ip::tcp::acceptor>(m_IoContext, endpoint);
            m_Port        = port;
        }
        catch (const std::exception& e)
        {
            ENGINE_CORE_ERROR("[NetServer] Listen failed on port {0}: {1}", port, e.what());
            return false;
        }

        m_IoThread = std::make_unique<std::thread>(
            [this]()
            {
                try
                {
                    m_IoContext.run();
                }
                catch (const std::exception& e)
                {
                    ENGINE_CORE_ERROR("[NetServer] IO thread exception: {0}", e.what());
                }
            });

        DoAccept();
        ENGINE_CORE_INFO("[NetServer] Listening on port {0}", port);
        return true;
    }

    void TcpServer::Stop()
    {
        if (!m_IoThread)
            return;

        m_IoContext.stop();
        m_IoThread->join();
        m_IoThread.reset();

        std::lock_guard<std::mutex> lock(m_EventMutex);
        m_PendingEvents.clear();
        ENGINE_CORE_INFO("[NetServer] Stopped");
    }

    void TcpServer::DoAccept()
    {
        if (!m_Acceptor)
            return;

        m_Acceptor->async_accept(
            [this](std::error_code ec, asio::ip::tcp::socket socket)
            {
                if (ec)
                {
                    if (ec != asio::error::operation_aborted)
                        ENGINE_CORE_WARN("[NetServer] Accept failed: {0}", ec.message());
                    return;
                }

                auto session    = std::make_shared<Session>();
                session->Id     = m_NextSessionId++;
                session->Socket = std::make_shared<asio::ip::tcp::socket>(std::move(socket));

                {
                    // m_Sessions 仅在 IO 线程访问（accept 回调与 io_context.run 同线程），无需加锁
                    m_Sessions.insert(session);
                }

                ServerEvent event;
                event.SessionId = session->Id;
                event.Connected = true;
                PostEvent(event);

                ENGINE_CORE_INFO("[NetServer] Session {0} connected from {1}", session->Id,
                                 session->Socket->remote_endpoint().address().to_string());

                ReadHeader(session);
                DoAccept();
            });
    }

    void TcpServer::ReadHeader(SessionPtr session)
    {
        asio::async_read(*session->Socket, asio::buffer(session->HeaderBuffer),
                         [this, session](std::error_code ec, size_t)
                         {
                             if (ec)
                             {
                                 if (ec != asio::error::operation_aborted)
                                     ENGINE_CORE_INFO("[NetServer] Session {0} read header end: {1}", session->Id,
                                                      ec.message());
                                 CloseSession(session->Id);
                                 return;
                             }

                             uint32_t total = 0;
                             for (int i = 0; i < 4; ++i)
                                 total |= static_cast<uint32_t>(session->HeaderBuffer[i]) << (8 * i);

                             // 帧长上限 64MB，防恶意/损坏头部
                             if (total < 8 || total > 64 * 1024 * 1024)
                             {
                                 ENGINE_CORE_WARN("[NetServer] Session {0} invalid frame size {1}", session->Id, total);
                                 CloseSession(session->Id);
                                 return;
                             }

                             session->FrameBuffer.assign(total - 8, 0);
                             ReadBody(session);
                         });
    }

    void TcpServer::ReadBody(SessionPtr session)
    {
        asio::async_read(*session->Socket, asio::buffer(session->FrameBuffer),
                         [this, session](std::error_code ec, size_t)
                         {
                             if (ec)
                             {
                                 if (ec != asio::error::operation_aborted)
                                     ENGINE_CORE_INFO("[NetServer] Session {0} read body end: {1}", session->Id,
                                                      ec.message());
                                 CloseSession(session->Id);
                                 return;
                             }

                             Message msg;
                             size_t  consumed = 0;
                             // 组装完整帧（头 + body）后统一解码
                             std::vector<uint8_t> frame(session->HeaderBuffer.begin(), session->HeaderBuffer.end());
                             frame.insert(frame.end(), session->FrameBuffer.begin(), session->FrameBuffer.end());
                             if (TryDecodeFrame(frame.data(), frame.size(), msg, consumed) && consumed == frame.size())
                             {
                                 ServerEvent event;
                                 event.SessionId = session->Id;
                                 event.Msg       = std::move(msg);
                                 PostEvent(event);
                             }
                             else
                             {
                                 ENGINE_CORE_WARN("[NetServer] Session {0} sent invalid frame", session->Id);
                             }

                             ReadHeader(session);
                         });
    }

    void TcpServer::DoSend(SessionPtr session)
    {
        // 仅 IO 线程调用：发送队首并串行衔接后续帧
        if (session->SendQueue.empty())
            return;

        auto payload = session->SendQueue.front();
        asio::async_write(
            *session->Socket,
            asio::buffer(payload.data(), payload.size()),
            [this, session](std::error_code ec, size_t)
            {
                if (ec)
                {
                    ENGINE_CORE_INFO("[NetServer] Session {0} send failed: {1}", session->Id, ec.message());
                    CloseSession(session->Id);
                    return;
                }
                session->SendQueue.pop_front();
                if (!session->SendQueue.empty())
                    DoSend(session);
            });
    }

    void TcpServer::Send(uint64_t sessionId, MessageType type, const std::vector<uint8_t>& payload)
    {
        // Send 可从主线程调用：post 到 io_context 保证与 accept/close 同线程操作 m_Sessions
        asio::post(m_IoContext,
                   [this, sessionId, type, payload]()
                   {
                       for (auto& session : m_Sessions)
                       {
                           if (session->Id != sessionId)
                               continue;
                           session->SendQueue.push_back(EncodeFrame(type, payload));
                           if (session->SendQueue.size() == 1)
                               DoSend(session);
                           break;
                       }
                   });
    }

    void TcpServer::Broadcast(MessageType type, const std::vector<uint8_t>& payload, uint64_t excludeSession)
    {
        asio::post(m_IoContext,
                   [this, type, payload, excludeSession]()
                   {
                       for (auto& session : m_Sessions)
                       {
                           if (session->Id == excludeSession)
                               continue;
                           session->SendQueue.push_back(EncodeFrame(type, payload));
                           if (session->SendQueue.size() == 1)
                               DoSend(session);
                       }
                   });
    }

    size_t TcpServer::SessionCount() const
    {
        return m_Sessions.size();
    }

    void TcpServer::CloseSession(uint64_t sessionId)
    {
        // 仅 IO 线程调用（async 回调内）
        for (auto it = m_Sessions.begin(); it != m_Sessions.end(); ++it)
        {
            if ((*it)->Id == sessionId)
            {
                std::error_code ignore;
                (*it)->Socket->close(ignore);
                m_Sessions.erase(it);

                ServerEvent event;
                event.SessionId    = sessionId;
                event.Disconnected = true;
                PostEvent(event);
                ENGINE_CORE_INFO("[NetServer] Session {0} closed", sessionId);
                return;
            }
        }
    }

    void TcpServer::PostEvent(const ServerEvent& event)
    {
        std::lock_guard<std::mutex> lock(m_EventMutex);
        m_PendingEvents.push_back(event);
    }

    void TcpServer::Poll(const EventCallback& callback)
    {
        std::deque<ServerEvent> events;
        {
            std::lock_guard<std::mutex> lock(m_EventMutex);
            events.swap(m_PendingEvents);
        }
        for (const auto& event : events)
            callback(event);
    }

} // namespace Engine::Net
