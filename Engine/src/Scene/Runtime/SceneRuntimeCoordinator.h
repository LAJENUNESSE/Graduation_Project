#pragma once

#include "Core/Timestep.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <set>
#include <vector>

namespace Engine
{

    class Scene;
    class SceneEntityIndex;
    class ResourceLifecycleCoordinator;
    class SceneRenderer;
    class BulletPhysicsWorld;
    class LuaScriptEngine;
    struct CollisionEvent;

    class SceneRuntimeCoordinator
    {
    public:
        SceneRuntimeCoordinator(entt::registry&               reg,
                                SceneEntityIndex&             index,
                                ResourceLifecycleCoordinator& lifecycle,
                                Scene*                        scene);
        ~SceneRuntimeCoordinator();

        void OnRuntimeStart(SceneRenderer* renderer);
        void OnRuntimeStop(SceneRenderer* renderer);
        void OnUpdateRuntime(Timestep ts, SceneRenderer* renderer);

        // 供 Scene::DestroyEntity 调用
        void DestroyPhysicsBody(entt::entity entity);

        // 服务器权威：对运行中 Dynamic 刚体设置线速度（无物理世界/体不存在返回 false）
        bool ApplyEntityLinearVelocity(entt::entity entity, const glm::vec3& velocity);

        // 读取运行中刚体的当前线速度（无物理世界/体不存在返回 false）。
        // 用途：注入水平输入时保留 Bullet 累加的重力分量，避免整体覆盖导致重力失效。
        bool ReadEntityLinearVelocity(entt::entity entity, glm::vec3& out) const;

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

        std::unique_ptr<BulletPhysicsWorld> m_BulletPhysicsWorld;
        std::unique_ptr<LuaScriptEngine>    m_LuaScriptEngine;

        void ProcessCollisionParticleBursts(const std::vector<CollisionEvent>&       events,
                                            std::set<std::pair<uint32_t, uint32_t>>& processedPairs);
        void DispatchCollisionCallbacks(const std::vector<CollisionEvent>& events);
    };

} // namespace Engine
