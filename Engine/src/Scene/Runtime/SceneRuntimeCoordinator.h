#pragma once

#include "Core/Timestep.h"

#include <entt/entt.hpp>
#include <memory>
#include <set>
#include <vector>

namespace Engine
{

    class Scene;
    class SceneEntityIndex;
    class ResourceLifecycleCoordinator;
    class SceneRenderer;
    class PhysicsWorld;
    class BulletPhysicsWorld;
    struct CollisionEvent;

    enum class PhysicsBackend;

    class SceneRuntimeCoordinator
    {
    public:
        SceneRuntimeCoordinator(entt::registry&               reg,
                                SceneEntityIndex&             index,
                                ResourceLifecycleCoordinator& lifecycle,
                                Scene*                        scene);
        ~SceneRuntimeCoordinator();

        void OnRuntimeStart(PhysicsBackend backend, SceneRenderer* renderer);
        void OnRuntimeStop(SceneRenderer* renderer);
        void OnUpdateRuntime(Timestep ts, PhysicsBackend backend, SceneRenderer* renderer);

        // 供 Scene::DestroyEntity 调用
        void DestroyPhysicsBody(entt::entity entity);

        // 不可拷贝/移动（持有引用成员）
        SceneRuntimeCoordinator(const SceneRuntimeCoordinator&)            = delete;
        SceneRuntimeCoordinator& operator=(const SceneRuntimeCoordinator&) = delete;
        SceneRuntimeCoordinator(SceneRuntimeCoordinator&&)                 = delete;
        SceneRuntimeCoordinator& operator=(SceneRuntimeCoordinator&&)      = delete;

    private:
        entt::registry&               m_Registry;
        SceneEntityIndex&             m_EntityIndex;
        ResourceLifecycleCoordinator& m_Lifecycle;
        Scene*                        m_Scene; // 非所有权，用于构造 Entity{handle, scene}

        std::unique_ptr<PhysicsWorld>       m_PhysicsWorld;
        std::unique_ptr<BulletPhysicsWorld> m_BulletPhysicsWorld;

        void ProcessCollisionParticleBursts(const std::vector<CollisionEvent>&       events,
                                            std::set<std::pair<uint32_t, uint32_t>>& processedPairs);
        void DispatchCollisionCallbacks(const std::vector<CollisionEvent>& events);
    };

} // namespace Engine
