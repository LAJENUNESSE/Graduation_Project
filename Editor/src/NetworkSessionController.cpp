#include "engpch.h"
#include "NetworkSessionController.h"

#include "Core/Log.h"
#include "Core/Timestep.h"

#include <imgui.h>

#include <chrono>

namespace Engine
{

    void NetworkSessionController::OpenConnectDialog()
    {
        m_ShowConnectDialog = true;
        m_LastError.clear();
    }

    void NetworkSessionController::DrawDialog()
    {
        if (!m_ShowConnectDialog)
            return;

        ImGui::SetNextWindowSize(ImVec2(420, 0));
        if (ImGui::BeginPopupModal("连接服务器", &m_ShowConnectDialog, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("地址", m_HostBuffer, sizeof(m_HostBuffer));
            ImGui::InputText("端口", m_PortBuffer, sizeof(m_PortBuffer));

            if (!m_LastError.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_LastError.c_str());

            if (ImGui::Button("连接", ImVec2(120, 0)))
            {
                uint16_t port = static_cast<uint16_t>(std::atoi(m_PortBuffer));
                if (m_Client.Connect(m_HostBuffer, port))
                {
                    m_State     = NetSessionState::Connected;
                    m_LastError.clear();
                    SendHello();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    m_LastError = "连接失败：无法建立 TCP 连接";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("取消", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }
    }

    void NetworkSessionController::SendHello()
    {
        Net::ByteWriter writer;
        writer.U16(Net::kProtocolVersion);
        writer.Str("Editor");
        m_Client.Send(Net::MessageType::C2S_Hello, writer.Data());
    }

    void NetworkSessionController::SendPing()
    {
        Net::ByteWriter writer;
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        writer.F64(now);
        m_PingSentAt = now;
        m_Client.Send(Net::MessageType::C2S_Ping, writer.Data());
    }

    void NetworkSessionController::HandleMessage(const Net::Message& msg)
    {
        switch (msg.Type)
        {
        case Net::MessageType::S2C_Welcome:
        {
            Net::ByteReader reader(msg.Payload.data(), msg.Payload.size());
            uint16_t        playerIndex = 0;
            uint32_t        sceneSize   = 0;
            if (!reader.U16(playerIndex) || !reader.U32(sceneSize))
            {
                ENGINE_CORE_WARN("[NetSession] Malformed Welcome");
                return;
            }
            m_PlayerIndex = playerIndex;

            std::vector<uint8_t> sceneBytes;
            if (sceneSize > 0)
            {
                if (!reader.Bytes(sceneBytes, sceneSize))
                {
                    ENGINE_CORE_WARN("[NetSession] Welcome scene payload truncated");
                    return;
                }
                std::string yaml(sceneBytes.begin(), sceneBytes.end());
                if (m_SceneReceived)
                    m_SceneReceived(yaml, playerIndex);
            }
            ENGINE_CORE_INFO("[NetSession] Welcome: playerIndex={0} sceneBytes={1}", playerIndex, sceneSize);
            break;
        }
        case Net::MessageType::S2C_Reject:
        {
            Net::ByteReader reader(msg.Payload.data(), msg.Payload.size());
            std::string     reason;
            reader.Str(reason);
            m_LastError = "被服务器拒绝: " + reason;
            ENGINE_CORE_WARN("[NetSession] Rejected: {0}", reason);
            Disconnect();
            break;
        }
        case Net::MessageType::S2C_Pong:
        {
            Net::ByteReader reader(msg.Payload.data(), msg.Payload.size());
            double          clientTime = 0.0;
            if (reader.F64(clientTime))
            {
                double now =
                    std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                m_LastPingRtt = (now - clientTime) * 1000.0;
            }
            break;
        }
        default:
            break;
        }
    }

    void NetworkSessionController::PollNetwork()
    {
        m_Client.Poll(
            [this](const Net::ClientEvent& event)
            {
                if (event.Connected)
                    return;
                if (event.Disconnected)
                {
                    m_State     = NetSessionState::Disconnected;
                    m_LastError = "与服务器断开连接";
                    return;
                }
                HandleMessage(event.Msg);
            });

        // 心跳：1s 间隔
        if (m_State == NetSessionState::Connected)
        {
            m_PingTimer += ImGui::GetIO().DeltaTime;
            if (m_PingTimer >= 1.0f)
            {
                m_PingTimer = 0.0f;
                SendPing();
            }
        }
    }

    void NetworkSessionController::Disconnect()
    {
        m_Client.Disconnect();
        m_State = NetSessionState::Disconnected;
    }

    std::string NetworkSessionController::GetStatusText() const
    {
        switch (m_State)
        {
        case NetSessionState::Disconnected:
            return "未连接";
        case NetSessionState::Connecting:
            return "连接中...";
        case NetSessionState::Connected:
            return "已连接 (P" + std::to_string(m_PlayerIndex + 1) + ", RTT " +
                   std::to_string(static_cast<int>(m_LastPingRtt)) + "ms)";
        }
        return "未知";
    }

} // namespace Engine
