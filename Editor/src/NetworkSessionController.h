#pragma once

// Editor 联机会话控制器：连接对话框 UI + TcpClient 生命周期 + 快照缓冲/插值 + 输入采样上行。
// 收到 Welcome（含场景 YAML）后通过 SceneReceivedHandler 回调交给 EditorLayer 重建场景。

#include "Core/Base.h"
#include "Network/Protocol.h"
#include "Network/SnapshotCodec.h"
#include "Network/TcpClient.h"

#include <deque>
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

    // 一次服务器快照（tick + 各实体 Transform）
    struct RemoteSnapshot
    {
        uint32_t                         Tick           = 0;
        double                           ReceiveTimeSec = 0.0;
        std::vector<Net::SnapshotEntity> Entities;
    };

    class NetworkSessionController
    {
    public:
        // 收到服务器场景 YAML 时回调（EditorLayer 重建场景并进入联机 Play）
        using SceneReceivedFn = std::function<void(const std::string& yaml, uint16_t playerIndex)>;

        void SetSceneReceivedHandler(SceneReceivedFn handler) { m_SceneReceived = std::move(handler); }

        // 弹出连接对话框（菜单动作触发）
        void OpenConnectDialog();
        // 每帧 UI：绘制连接对话框（若有）
        void DrawDialog();
        // 每帧逻辑：取网络事件、驱动心跳、采样输入上行
        void PollNetwork();

        void Disconnect();

        NetSessionState GetState() const { return m_State; }
        uint16_t        GetAssignedPlayerIndex() const { return m_PlayerIndex; }
        void            SetPlayerIndex(uint16_t index) { m_PlayerIndex = index; }

        std::string GetStatusText() const;

        // 联机渲染：把当前插值时刻的快照状态写到场景实体 Transform（每帧调用，联机 Play 时）
        // 返回 false 表示暂无可用快照
        bool ApplyInterpolatedTransforms(Ref<Scene>& scene, double renderTimeSec);

    private:
        void HandleMessage(const Net::Message& msg);
        void SendHello();
        void SendPing();
        void SendInput(uint16_t bits);

        Net::TcpClient  m_Client;
        NetSessionState m_State       = NetSessionState::Disconnected;
        uint16_t        m_PlayerIndex = 0;

        // 连接对话框状态
        bool        m_ShowConnectDialog = false;
        char        m_HostBuffer[128]   = "127.0.0.1";
        char        m_PortBuffer[16]    = "30000";
        std::string m_LastError;

        // 心跳
        float  m_PingTimer   = 0.0f;
        double m_LastPingRtt = 0.0;

        // 输入上行
        uint32_t m_InputSeq   = 0;
        float    m_InputTimer = 0.0f;

        // 快照缓冲（服务器 20Hz → 缓冲 ~100ms 延迟插值）
        std::deque<RemoteSnapshot> m_Snapshots;

        SceneReceivedFn m_SceneReceived;
    };

} // namespace Engine
