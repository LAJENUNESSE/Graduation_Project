#include "engpch.h"
#include "Scene/Runtime/ResourceLifecycleCoordinator.h"

namespace Engine
{

    void ResourceLifecycleCoordinator::RegisterEntityCleanup(EntityCleanupFn fn)
    {
        m_EntityCleanups.push_back(std::move(fn));
    }

    void ResourceLifecycleCoordinator::RegisterRuntimeStopCleanup(RuntimeStopCleanupFn fn)
    {
        m_RuntimeStopCleanups.push_back(std::move(fn));
    }

    void ResourceLifecycleCoordinator::RegisterSceneDestroyCleanup(SceneDestroyCleanupFn fn)
    {
        m_SceneDestroyCleanups.push_back(std::move(fn));
    }

    void ResourceLifecycleCoordinator::PreDestroyEntity(entt::registry& reg, entt::entity entity)
    {
        for (auto& fn : m_EntityCleanups)
            fn(reg, entity);
    }

    void ResourceLifecycleCoordinator::CleanupRuntimeStop(entt::registry& reg)
    {
        for (auto& fn : m_RuntimeStopCleanups)
            fn(reg);
    }

    void ResourceLifecycleCoordinator::CleanupSceneDestroy()
    {
        for (auto& fn : m_SceneDestroyCleanups)
            fn();
    }

    void ResourceLifecycleCoordinator::ClearAll()
    {
        m_EntityCleanups.clear();
        m_RuntimeStopCleanups.clear();
        m_SceneDestroyCleanups.clear();
    }

} // namespace Engine
