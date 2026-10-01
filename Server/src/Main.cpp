#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
#include "Core/CrashHandler.h"
#include "Core/Headless.h"
#include "Core/Log.h"
#include "Core/Timestep.h"
#include "Network/Protocol.h"
#include "Network/TcpServer.h"
#include "Renderer/EditorCamera.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"
#include "SessionManager.h"
#include "Network/SnapshotCodec.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>

namespace
{
    struct ServerOptions
    {
        std::string ScenePath       = "assets/scenes/network-demo.scene";
        double      DurationSeconds = 0.0; // 0 = 一直运行
        float       TickRate        = 60.0f;
        uint16_t    Port            = 30000;
    };

    ServerOptions ParseArgs(int argc, char** argv)
    {
        ServerOptions options;
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc)
                options.ScenePath = argv[++i];
            else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc)
                options.DurationSeconds = std::atof(argv[++i]);
            else if (std::strcmp(argv[i], "--tickrate") == 0 && i + 1 < argc)
                options.TickRate = static_cast<float>(std::atof(argv[++i]));
            else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
                options.Port = static_cast<uint16_t>(std::atoi(argv[++i]));
        }
        return options;
    }
} // namespace

int main(int argc, char** argv)
{
    using namespace Engine;

    Log::Init();
    CrashHandler::Install();

    if (!PathUtils::DiscoverProjectRootFromExecutable())
        ENGINE_CORE_WARN("[Server] Failed to discover project root from executable, fallback to exe directory");

    ENGINE_CORE_INFO("[Server] Project root: {0}", PathUtils::PathToUtf8String(PathUtils::GetProjectRoot()));

    ServerOptions options = ParseArgs(argc, argv);
    ENGINE_CORE_INFO("[Server] scene='{0}' duration={1}s tickRate={2}Hz port={3}", options.ScenePath,
                     options.DurationSeconds, options.TickRate, options.Port);

    // 无头模式：不创建窗口/GL 上下文，图形工厂走占位路径
    Headless::SetEnabled(true);

    AssetManager::Init();

    auto            scene = CreateRef<Scene>();
    SceneSerializer serializer(scene);
    if (!serializer.Deserialize(options.ScenePath))
    {
        ENGINE_CORE_ERROR("[Server] Failed to load scene '{0}'", options.ScenePath);
        AssetManager::Shutdown();
        return 1;
    }

    scene->OnRuntimeStart();
    ENGINE_CORE_INFO("[Server] Runtime started, stepping physics at {0}Hz", options.TickRate);

    // ── 网络：监听 + 握手/心跳（场景下发与快照在后续阶段接入） ──
    Net::TcpServer server;
    if (!server.Start(options.Port))
    {
        ENGINE_CORE_ERROR("[Server] Failed to start network on port {0}", options.Port);
        scene->OnRuntimeStop();
        AssetManager::Shutdown();
        return 1;
    }

    std::atomic<uint64_t> lastClientActivityMs{0};

    // 服务器权威场景：连接建立时全量下发
    std::string sceneYaml;
    {
        std::ifstream sceneFile(options.ScenePath, std::ios::binary);
        if (sceneFile)
        {
            std::ostringstream ss;
            ss << sceneFile.rdbuf();
            sceneYaml = ss.str();
        }
        ENGINE_CORE_INFO("[Server] Scene payload prepared: {0} bytes", sceneYaml.size());
    }

    // 玩家会话管理：Tag == PlayerN 的实体为各槽位可控实体
    SessionManager sessions;
    sessions.BindPlayerEntities(scene);

    auto handleServerEvent = [&server, &lastClientActivityMs, &sceneYaml, &sessions](const Net::ServerEvent& event)
    {
        if (event.Connected)
        {
            ENGINE_CORE_INFO("[Server] Client session {0} awaiting Hello", event.SessionId);
            return;
        }
        if (event.Disconnected)
        {
            sessions.ReleaseSlot(event.SessionId);
            ENGINE_CORE_INFO("[Server] Client session {0} disconnected", event.SessionId);
            return;
        }
        if (event.Msg.Type == Net::MessageType::C2S_Hello)
        {
            Net::ByteReader reader(event.Msg.Payload.data(), event.Msg.Payload.size());
            uint16_t        clientVersion = 0;
            std::string     clientName;
            reader.U16(clientVersion) && reader.Str(clientName);
            ENGINE_CORE_INFO("[Server] Hello from '{0}' (protocol v{1})", clientName, clientVersion);

            if (clientVersion != Net::kProtocolVersion)
            {
                Net::ByteWriter reject;
                reject.Str("protocol version mismatch");
                server.Send(event.SessionId, Net::MessageType::S2C_Reject, reject.Data());
                return;
            }

            uint16_t playerIndex = sessions.AssignSlot(event.SessionId);
            if (playerIndex == UINT16_MAX)
            {
                Net::ByteWriter reject;
                reject.Str("server full");
                server.Send(event.SessionId, Net::MessageType::S2C_Reject, reject.Data());
                return;
            }

            // Welcome: playerIndex(u16) + sceneYamlSize(u32) + yaml
            Net::ByteWriter welcome;
            welcome.U16(playerIndex);
            welcome.U32(static_cast<uint32_t>(sceneYaml.size()));
            welcome.Bytes(reinterpret_cast<const uint8_t*>(sceneYaml.data()), sceneYaml.size());
            server.Send(event.SessionId, Net::MessageType::S2C_Welcome, welcome.Data());
            ENGINE_CORE_INFO("[Server] Welcome sent to session {0}: player={1} scene={2} bytes", event.SessionId,
                             playerIndex, sceneYaml.size());
        }
        else if (event.Msg.Type == Net::MessageType::C2S_Input)
        {
            uint32_t seq  = 0;
            uint16_t bits = 0;
            if (Net::DecodeInput(event.Msg.Payload, seq, bits))
                sessions.ApplyInput(event.SessionId, bits, seq);
        }
        else if (event.Msg.Type == Net::MessageType::C2S_Ping)
        {
            Net::ByteReader reader(event.Msg.Payload.data(), event.Msg.Payload.size());
            double          clientTime = 0.0;
            if (reader.F64(clientTime))
            {
                Net::ByteWriter pong;
                pong.F64(clientTime);
                server.Send(event.SessionId, Net::MessageType::S2C_Pong, pong.Data());
                lastClientActivityMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count());
            }
        }
    };

    server.Poll(handleServerEvent);

    const auto      stepDuration        = std::chrono::duration<double>(1.0 / static_cast<double>(options.TickRate));
    const auto      startTime           = std::chrono::steady_clock::now();
    const float     stepSeconds         = 1.0f / options.TickRate;
    constexpr float kSnapshotRate       = 20.0f; // Hz
    float           snapshotAccumulator = 0.0f;

    uint64_t tick = 0;
    while (options.DurationSeconds <= 0.0 ||
           std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count() <
               options.DurationSeconds)
    {
        auto frameStart = std::chrono::steady_clock::now();

        server.Poll(handleServerEvent);

        // 输入 → 权威刚体速度注入（每 tick）
        //
        // 关键：必须保留 Bullet 已累加的 Y 轴（重力）分量。早期实现每 tick 把速度整体覆盖为
        // 输入速度，无输入时即 (0,0,0)，等于把上一 tick 的重力速度清零——物体因此以约
        // g*dt ≈ 0.16 m/s 匀速下沉而非自由落体（视觉上"缓慢飘落"）。
        // 现在分层接管：水平分量由输入直接控制（松键即停），Y 轴默认交还物理引擎，
        // 仅 Q/E 按下时由输入接管。
        const float moveSpeed = 5.0f;
        for (uint16_t i = 0; i < SessionManager::kMaxPlayers; ++i)
        {
            auto* slot = sessions.GetSlot(i);
            if (!slot || !slot->Occupied || slot->Entity == entt::null || !scene->GetRegistry().valid(slot->Entity))
                continue;

            // 读回当前速度；读取失败按零处理，不影响后续注入
            glm::vec3 currentVelocity{0.0f};
            scene->ReadEntityLinearVelocity(slot->Entity, currentVelocity);

            glm::vec3      velocity = currentVelocity;
            const uint16_t bits     = slot->InputBits;

            // 水平分量：输入直接接管，松键立即停止（演示场景不需要惯性滑行）
            velocity.x = 0.0f;
            velocity.z = 0.0f;
            if (bits & Net::kInputW)
                velocity.z += moveSpeed;
            if (bits & Net::kInputS)
                velocity.z -= moveSpeed;
            if (bits & Net::kInputA)
                velocity.x -= moveSpeed;
            if (bits & Net::kInputD)
                velocity.x += moveSpeed;

            // 垂直分量：默认保留重力累积；Q/E 按下时改由输入接管
            if (bits & Net::kInputQ)
                velocity.y = moveSpeed;
            else if (bits & Net::kInputE)
                velocity.y = -moveSpeed;

            scene->ApplyEntityLinearVelocity(slot->Entity, velocity);
        }

        EditorCamera dummyCamera;
        scene->OnUpdateRuntime(Timestep(stepSeconds), dummyCamera);

        // 20Hz 快照广播（Dynamic 刚体 Transform 全量）
        snapshotAccumulator += stepSeconds;
        if (snapshotAccumulator >= 1.0f / kSnapshotRate)
        {
            snapshotAccumulator = 0.0f;
            auto entities       = Net::CollectSnapshotEntities(scene);
            server.Broadcast(Net::MessageType::S2C_Snapshot,
                             Net::EncodeSnapshot(static_cast<uint32_t>(tick), entities));
        }

        ++tick;
        if (tick % static_cast<uint64_t>(options.TickRate) == 0)
        {
            for (auto [entity, id, tc, rb] :
                 scene->GetRegistry().view<IDComponent, TransformComponent, RigidBodyComponent>().each())
            {
                (void)entity;
                if (rb.Type != RigidBodyComponent::BodyType::Dynamic)
                    continue;
                ENGINE_CORE_INFO("[Server] tick={0} entity={1} pos=({2:.2f}, {3:.2f}, {4:.2f})", tick,
                                 static_cast<uint64_t>(id.ID), tc.Translation.x, tc.Translation.y, tc.Translation.z);
            }
        }

        std::this_thread::sleep_until(frameStart +
                                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(stepDuration));
    }

    server.Stop();
    scene->OnRuntimeStop();
    AssetManager::Shutdown();
    ENGINE_CORE_INFO("[Server] Shutdown after {0} ticks", tick);
    return 0;
}
