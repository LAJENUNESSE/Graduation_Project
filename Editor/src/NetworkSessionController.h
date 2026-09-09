#pragma once

// Editor 联机会话控制器：连接对话框 UI + TcpClient 生命周期 + 服务器消息分发。
// EditorLayer 在 OnImGuiRender 调 DrawDialog/PollNetwork，OnUpdate 调 PollNetwork 均可。

#include "Core/Base.h"
#include "Network/Protocol.h"
#include "Network/TcpClient.h"

#include <functional>
#include <string>

namespace Engine
{

    class Scene;

    enum class NetSessionState
    {
        Disconnected,
        Connecting,
        Connected,
    };

    class NetworkSessionController
    {
    public:
        // 收到服务器场景 YAML 时回调（P2 接入场景重建）
        using SceneReceivedFn = std::function<void(const std::string& yaml, uint16_t playerIndex)>;

        void SetSceneReceivedHandler(SceneReceivedFn handler) { m_SceneReceived = std::move(handler); }

        // 弹出连接对话框（菜单动作触发）
        void OpenConnectDialog();
        // 每帧 UI：绘制连接对话框（若有）
        void DrawDialog();
        // 每帧逻辑：取网络事件、驱动心跳
        void PollNetwork();

        void Disconnect();

        NetSessionState GetState() const { return m_State; }
        uint16_t        GetAssignedPlayerIndex() const { return m_PlayerIndex; }
        uint16_t        GetAssignedUUIDLo() const { return m_PlayerIndex; } // 调试用

        std::string GetStatusText() const;

    private:
        void HandleMessage(const Net::Message& msg);
        void SendHello();
        void SendPing();

        Net::TcpClient    m_Client;
        NetSessionState   m_State       = NetSessionState::Disconnected;
        uint16_t          m_PlayerIndex = 0;

        // 连接对话框状态
        bool        m_ShowConnectDialog = false;
        char        m_HostBuffer[128]   = "127.0.0.1";
        char        m_PortBuffer[16]    = "30000";
        std::string m_LastError;

        // 心跳
        float m_PingTimer   = 0.0f;
        double m_LastPingRtt = 0.0;
        double m_PingSentAt  = 0.0;

        SceneReceivedFn m_SceneReceived;
    };

} // namespace Engine
