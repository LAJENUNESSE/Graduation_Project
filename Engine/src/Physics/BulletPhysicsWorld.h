#pragma once

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <vector>
#include <unordered_map>

// Bullet 前向声明
class btDiscreteDynamicsWorld;
class btBroadphaseInterface;
class btCollisionDispatcher;
class btDefaultCollisionConfiguration;
class btSequentialImpulseConstraintSolver;
class btRigidBody;
class btCollisionShape;
class btDefaultMotionState;

namespace Engine
{

    class BulletPhysicsWorld
    {
    public:
        BulletPhysicsWorld();
        ~BulletPhysicsWorld();

        void Init(glm::vec3 gravity = {0, -9.81f, 0});
        void Shutdown();
        void Step(float dt, entt::registry& reg);

        // 从 ECS 组件创建 Bullet 刚体
        void CreateBodies(entt::registry& reg);
        // 销毁单个实体的 Bullet 刚体（运行时删除实体时调用）
        void DestroyBody(entt::entity entity);
        // 将 Bullet 状态同步回 ECS
        void SyncToECS(entt::registry& reg);

    private:
        btDiscreteDynamicsWorld* m_DynamicsWorld = nullptr;
        btBroadphaseInterface* m_Broadphase = nullptr;
        btCollisionDispatcher* m_Dispatcher = nullptr;
        btDefaultCollisionConfiguration* m_CollisionConfig = nullptr;
        btSequentialImpulseConstraintSolver* m_Solver = nullptr;

        struct BodyInfo
        {
            btRigidBody* body = nullptr;
            btCollisionShape* shape = nullptr;
            btDefaultMotionState* motionState = nullptr;
        };

        std::unordered_map<uint32_t, BodyInfo> m_Bodies; // entt::entity -> BodyInfo

        void DestroyBodies();
    };

} // namespace Engine
