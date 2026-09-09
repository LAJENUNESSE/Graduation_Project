#include "Asset/AssetManager.h"
#include "Asset/PathUtils.h"
#include "Core/CrashHandler.h"
#include "Core/Headless.h"
#include "Core/Log.h"
#include "Core/Timestep.h"
#include "Renderer/EditorCamera.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/SceneSerializer.h"

#include <chrono>
#include <cstring>
#include <thread>

namespace
{
    struct ServerOptions
    {
        std::string ScenePath = "assets/scenes/network-demo.scene";
        double      DurationSeconds = 10.0; // 0 = 一直运行
        float       TickRate = 60.0f;
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
    ENGINE_CORE_INFO("[Server] scene='{0}' duration={1}s tickRate={2}Hz", options.ScenePath, options.DurationSeconds,
                     options.TickRate);

    // 无头模式：不创建窗口/GL 上下文，图形工厂走占位路径
    Headless::SetEnabled(true);

    AssetManager::Init();

    auto scene = CreateRef<Scene>();
    SceneSerializer serializer(scene);
    if (!serializer.Deserialize(options.ScenePath))
    {
        ENGINE_CORE_ERROR("[Server] Failed to load scene '{0}'", options.ScenePath);
        AssetManager::Shutdown();
        return 1;
    }

    scene->OnRuntimeStart();
    ENGINE_CORE_INFO("[Server] Runtime started, stepping physics at {0}Hz", options.TickRate);

    const auto  stepDuration = std::chrono::duration<double>(1.0 / static_cast<double>(options.TickRate));
    const auto  startTime    = std::chrono::steady_clock::now();
    const float stepSeconds  = 1.0f / options.TickRate;

    uint64_t tick = 0;
    while (options.DurationSeconds <= 0.0 ||
           std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count() <
               options.DurationSeconds)
    {
        auto frameStart = std::chrono::steady_clock::now();

        EditorCamera dummyCamera;
        scene->OnUpdateRuntime(Timestep(stepSeconds), dummyCamera);

        ++tick;
        if (tick % static_cast<uint64_t>(options.TickRate) == 0)
        {
            // 每秒打印一次动态刚体位置，便于无显示器环境验证仿真
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

        std::this_thread::sleep_until(frameStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                        stepDuration));
    }

    scene->OnRuntimeStop();
    AssetManager::Shutdown();
    ENGINE_CORE_INFO("[Server] Shutdown after {0} ticks", tick);
    return 0;
}
