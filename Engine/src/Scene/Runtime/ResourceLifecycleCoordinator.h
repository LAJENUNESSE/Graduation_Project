#pragma once

#include <entt/entt.hpp>
#include <functional>
#include <vector>

namespace Engine
{

    // 统一的运行时资源生命周期管理
    // 回调在 Scene 的关键生命周期点被调用，确保资源正确释放
    class ResourceLifecycleCoordinator
    {
    public:
        using EntityCleanupFn = std::function<void(entt::registry&, entt::entity)>;
        using RuntimeStopCleanupFn = std::function<void(entt::registry&)>;
        using SceneDestroyCleanupFn = std::function<void()>;

        // 注册清理回调
        void RegisterEntityCleanup(EntityCleanupFn fn);
        void RegisterRuntimeStopCleanup(RuntimeStopCleanupFn fn);
        void RegisterSceneDestroyCleanup(SceneDestroyCleanupFn fn);

        // 由 Scene 在对应时机调用
        void PreDestroyEntity(entt::registry& reg, entt::entity entity);
        void CleanupRuntimeStop(entt::registry& reg);
        void CleanupSceneDestroy();

        // 清空所有注册的回调（Scene::Copy 的新场景需要重新注册）
        void ClearAll();

    private:
        std::vector<EntityCleanupFn> m_EntityCleanups;
        std::vector<RuntimeStopCleanupFn> m_RuntimeStopCleanups;
        std::vector<SceneDestroyCleanupFn> m_SceneDestroyCleanups;
    };

} // namespace Engine
