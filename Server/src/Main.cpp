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

    auto handleServerEvent = [&server, &lastClientActivityMs, &sceneYaml](const Net::ServerEvent& event)
    {
        if (event.Connected)
        {
            ENGINE_CORE_INFO("[Server] Client session {0} awaiting Hello", event.SessionId);
            return;
        }
        if (event.Disconnected)
        {
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

            // Welcome: playerIndex(u16) + sceneYamlSize(u32) + yaml
            Net::ByteWriter welcome;
            welcome.U16(0); // assignedPlayerIndex（P3 改为按会话分配）
            welcome.U32(static_cast<uint32_t>(sceneYaml.size()));
            welcome.Bytes(reinterpret_cast<const uint8_t*>(sceneYaml.data()), sceneYaml.size());
            server.Send(event.SessionId, Net::MessageType::S2C_Welcome, welcome.Data());
            ENGINE_CORE_INFO("[Server] Welcome sent to session {0}: scene={1} bytes", event.SessionId,
                             sceneYaml.size());
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

    const auto  stepDuration = std::chrono::duration<double>(1.0 / static_cast<double>(options.TickRate));
    const auto  startTime    = std::chrono::steady_clock::now();
    const float stepSeconds  = 1.0f / options.TickRate;

    uint64_t tick = 0;
    while (options.DurationSeconds <= 0.0 ||
           std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count() <
               options.DurationSeconds)
    {
        auto frameStart = std::chrono::steady_clock::now();

        server.Poll(handleServerEvent);

        EditorCamera dummyCamera;
        scene->OnUpdateRuntime(Timestep(stepSeconds), dummyCamera);

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
