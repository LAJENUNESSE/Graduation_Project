#include "engpch.h"
#include "NetworkSessionController.h"

#include "Core/KeyCodes.h"
#include "Core/Log.h"
#include "Core/Input.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "Scene/SceneEntityIndex.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>

namespace Engine
{

    namespace
    {
        double NowSeconds()
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    } // namespace

    void NetworkSessionController::OpenConnectDialog()
    {
        // 只置请求位，不在这里调 ImGui::OpenPopup()。
        //
        // ImGui 的 BeginPopupModal 与 OpenPopup 必须在**同一个 ID 栈层级**成对出现：
        // OpenPopup("连接服务器") 记录的 ID 取决于调用点所在的窗口。菜单动作发生在
        // 主菜单栏的窗口上下文里，而弹窗要在 DrawDialog() 的调用点绘制，两者 ID
        // 不匹配时弹窗永远不会出现。所以这里记下意图，等 DrawDialog() 那一帧
        // 在正确的上下文里再调 OpenPopup。
        m_OpenPopupRequested = true;
        m_LastError.clear();
    }

    void NetworkSessionController::DrawDialog()
    {
        // 处理"打开弹窗"请求：必须与本帧的 BeginPopupModal 处于同一上下文
        if (m_OpenPopupRequested)
        {
            m_OpenPopupRequested = false;
            m_ShowConnectDialog  = true;
            ImGui::OpenPopup("连接服务器");
        }

        if (!m_ShowConnectDialog && !ImGui::IsPopupOpen("连接服务器"))
            return;

        ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
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
                    m_State = NetSessionState::Connected;
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
        writer.F64(NowSeconds());
        m_Client.Send(Net::MessageType::C2S_Ping, writer.Data());
    }

    void NetworkSessionController::SendInput(uint16_t bits)
    {
        m_Client.Send(Net::MessageType::C2S_Input, Net::EncodeInput(m_InputSeq++, bits));
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
                m_LastPingRtt = (NowSeconds() - clientTime) * 1000.0;
            break;
        }
        case Net::MessageType::S2C_Snapshot:
        {
            Net::DecodedSnapshot decoded;
            if (!Net::DecodeSnapshot(msg.Payload, decoded))
            {
                ENGINE_CORE_WARN("[NetSession] Malformed snapshot");
                break;
            }
            // 过期快照丢弃（乱序保护）
            if (!m_Snapshots.empty() && decoded.Tick <= m_Snapshots.back().Tick)
                break;

            RemoteSnapshot snap;
            snap.Tick           = decoded.Tick;
            snap.ReceiveTimeSec = NowSeconds();
            snap.Entities       = std::move(decoded.Entities);
            m_Snapshots.push_back(std::move(snap));

            // 缓冲上限 16 帧（0.8s @20Hz），防止积累
            while (m_Snapshots.size() > 16)
                m_Snapshots.pop_front();
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

        if (m_State != NetSessionState::Connected)
            return;

        // 心跳：1s 间隔
        m_PingTimer += ImGui::GetIO().DeltaTime;
        if (m_PingTimer >= 1.0f)
        {
            m_PingTimer = 0.0f;
            SendPing();
        }

        // 输入采样上行：50Hz 固定间隔（快照率 20Hz 的 2.5 倍，服务器端即到即用）
        m_InputTimer += ImGui::GetIO().DeltaTime;
        if (m_InputTimer >= 0.02f)
        {
            m_InputTimer = 0.0f;

            uint16_t bits = 0;
            if (Input::IsKeyPressed(KeyCode::W))
                bits |= Net::kInputW;
            if (Input::IsKeyPressed(KeyCode::S))
                bits |= Net::kInputS;
            if (Input::IsKeyPressed(KeyCode::A))
                bits |= Net::kInputA;
            if (Input::IsKeyPressed(KeyCode::D))
                bits |= Net::kInputD;
            if (Input::IsKeyPressed(KeyCode::Q))
                bits |= Net::kInputQ;
            if (Input::IsKeyPressed(KeyCode::E))
                bits |= Net::kInputE;

            SendInput(bits);
        }
    }

    bool NetworkSessionController::ApplyInterpolatedTransforms(Ref<Scene>& scene, double renderTimeSec)
    {
        // 插值延迟 = 2 个快照间隔（20Hz → 100ms）；渲染时刻 = 最新快照到达时间 - 延迟
        constexpr double kInterpDelaySec = 0.1;
        if (m_Snapshots.size() < 2)
            return false;

        const double target = m_Snapshots.back().ReceiveTimeSec - kInterpDelaySec;

        // 找到 target 前后的两帧快照
        size_t idx1 = 0;
        while (idx1 + 1 < m_Snapshots.size() && m_Snapshots[idx1 + 1].ReceiveTimeSec <= target)
            ++idx1;
        if (idx1 + 1 >= m_Snapshots.size())
            return false;

        const RemoteSnapshot& a    = m_Snapshots[idx1];
        const RemoteSnapshot& b    = m_Snapshots[idx1 + 1];
        const double          span = b.ReceiveTimeSec - a.ReceiveTimeSec;
        const float           alpha =
            span > 0.0 ? static_cast<float>(std::clamp((target - a.ReceiveTimeSec) / span, 0.0, 1.0)) : 0.0f;

        auto& registry = scene->GetRegistry();
        auto& index    = const_cast<SceneEntityIndex&>(scene->GetEntityIndex());
        bool  applied  = false;
        for (const auto& se : b.Entities)
        {
            entt::entity entity = index.Find(static_cast<uint64_t>(se.Uuid));
            if (entity == entt::null || !registry.valid(entity))
                continue;
            if (!registry.all_of<TransformComponent>(entity))
                continue;

            auto& tc = registry.get<TransformComponent>(entity);

            // 在 a 中找同实体做线性插值；找不到（新出现）直接取 b
            glm::vec3                  pos = se.Translation, rot = se.Rotation;
            const Net::SnapshotEntity* prev = nullptr;
            for (const auto& e : a.Entities)
            {
                if (e.Uuid == se.Uuid)
                {
                    prev = &e;
                    break;
                }
            }
            if (prev)
            {
                pos = glm::mix(prev->Translation, se.Translation, alpha);
                rot = glm::mix(prev->Rotation, se.Rotation, alpha);
            }

            tc.Translation = pos;
            tc.Rotation    = rot;
            applied        = true;
        }

        // 快照写完后强制世界变换缓存失效，本帧渲染重算
        if (applied)
            scene->GetTransformCache().Invalidate();
        return applied;
    }

    void NetworkSessionController::Disconnect()
    {
        m_Client.Disconnect();
        m_State = NetSessionState::Disconnected;
        m_Snapshots.clear();
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
