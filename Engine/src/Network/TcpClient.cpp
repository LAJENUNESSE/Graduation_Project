#include "engpch.h"
#include "Network/TcpClient.h"

#include "Core/Log.h"

namespace Engine::Net
{

    bool TcpClient::Connect(const std::string& host, uint16_t port)
    {
        if (m_Connected.load())
            return true;

        try
        {
            asio::ip::tcp::resolver resolver(m_IoContext);
            auto endpoints = resolver.resolve(host, std::to_string(port));
            m_Socket       = std::make_unique<asio::ip::tcp::socket>(m_IoContext);
            asio::connect(*m_Socket, endpoints);
        }
        catch (const std::exception& e)
        {
            ENGINE_CORE_ERROR("[NetClient] Connect to {0}:{1} failed: {2}", host, port, e.what());
            m_Socket.reset();
            return false;
        }

        m_Connected.store(true);
        m_Thread = std::make_unique<std::thread>([this]() { IoThread(); });

        ClientEvent event;
        event.Connected = true;
        PostEvent(event);
        ENGINE_CORE_INFO("[NetClient] Connected to {0}:{1}", host, port);
        return true;
    }

    void TcpClient::Disconnect()
    {
        if (!m_Connected.exchange(false))
            return;

        std::error_code ignore;
        m_Socket->close(ignore);
        m_IoContext.stop();
        if (m_Thread)
        {
            m_Thread->join();
            m_Thread.reset();
        }
        m_Socket.reset();
        m_IoContext.restart();

        ClientEvent event;
        event.Disconnected = true;
        PostEvent(event);
        ENGINE_CORE_INFO("[NetClient] Disconnected");
    }

    void TcpClient::IoThread()
    {
        try
        {
            ReadHeader();
            m_IoContext.run();
        }
        catch (const std::exception& e)
        {
            ENGINE_CORE_ERROR("[NetClient] IO exception: {0}", e.what());
        }
        // run() 退出（出错/远端断开）时统一走 Disconnect 清理（幂等）
        if (m_Connected.exchange(false))
        {
            ClientEvent event;
            event.Disconnected = true;
            PostEvent(event);
            ENGINE_CORE_INFO("[NetClient] Connection lost");
        }
    }

    void TcpClient::ReadHeader()
    {
        auto self = this;
        asio::async_read(*m_Socket, asio::buffer(m_HeaderBuffer),
                         [self](std::error_code ec, size_t)
                         {
                             if (ec)
                             {
                                 if (ec != asio::error::operation_aborted)
                                     ENGINE_CORE_INFO("[NetClient] Read header end: {0}", ec.message());
                                 self->m_IoContext.stop();
                                 return;
                             }

                             uint32_t total = 0;
                             for (int i = 0; i < 4; ++i)
                                 total |= static_cast<uint32_t>(self->m_HeaderBuffer[i]) << (8 * i);

                             if (total < 8 || total > 64 * 1024 * 1024)
                             {
                                 ENGINE_CORE_WARN("[NetClient] Invalid frame size {0}", total);
                                 self->m_IoContext.stop();
                                 return;
                             }

                             self->m_FrameBuffer.assign(total - 8, 0);
                             self->ReadBody();
                         });
    }

    void TcpClient::ReadBody()
    {
        auto self = this;
        asio::async_read(*m_Socket, asio::buffer(m_FrameBuffer),
                         [self](std::error_code ec, size_t)
                         {
                             if (ec)
                             {
                                 if (ec != asio::error::operation_aborted)
                                     ENGINE_CORE_INFO("[NetClient] Read body end: {0}", ec.message());
                                 self->m_IoContext.stop();
                                 return;
                             }

                             Message msg;
                             size_t  consumed = 0;
                             // 组装完整帧（头 + body）后统一解码
                             std::vector<uint8_t> frame(self->m_HeaderBuffer.begin(), self->m_HeaderBuffer.end());
                             frame.insert(frame.end(), self->m_FrameBuffer.begin(), self->m_FrameBuffer.end());
                             if (TryDecodeFrame(frame.data(), frame.size(), msg, consumed) && consumed == frame.size())
                             {
                                 ClientEvent event;
                                 event.Msg = std::move(msg);
                                 self->PostEvent(event);
                             }
                             else
                             {
                                 ENGINE_CORE_WARN("[NetClient] Received invalid frame");
                             }

                             self->ReadHeader();
                         });
    }

    void TcpClient::DoSend()
    {
        // 仅 IO 线程调用；发送队首并串行衔接
        if (m_SendQueue.empty())
            return;

        auto payload = m_SendQueue.front();
        auto self    = this;
        asio::async_write(*m_Socket, asio::buffer(payload.data(), payload.size()),
                          [self](std::error_code ec, size_t)
                          {
                              if (ec)
                              {
                                  ENGINE_CORE_INFO("[NetClient] Send failed: {0}", ec.message());
                                  self->m_IoContext.stop();
                                  return;
                              }
                              std::lock_guard<std::mutex> lock(self->m_SendMutex);
                              self->m_SendQueue.pop_front();
                              if (!self->m_SendQueue.empty())
                                  self->DoSend();
                          });
    }

    void TcpClient::Send(MessageType type, const std::vector<uint8_t>& payload)
    {
        if (!m_Connected.load())
            return;

        bool kick = false;
        {
            std::lock_guard<std::mutex> lock(m_SendMutex);
            kick = m_SendQueue.empty();
            m_SendQueue.push_back(EncodeFrame(type, payload));
        }
        if (kick)
            asio::post(m_IoContext, [this]() { DoSend(); });
    }

    void TcpClient::PostEvent(const ClientEvent& event)
    {
        std::lock_guard<std::mutex> lock(m_EventMutex);
        m_PendingEvents.push_back(event);
    }

    void TcpClient::Poll(const EventCallback& callback)
    {
        std::deque<ClientEvent> events;
        {
            std::lock_guard<std::mutex> lock(m_EventMutex);
            events.swap(m_PendingEvents);
        }
        for (const auto& event : events)
            callback(event);
    }

} // namespace Engine::Net
