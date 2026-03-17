#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

// Bullet 前向声明
class btDiscreteDynamicsWorld;
class btBroadphaseInterface;
class btCollisionDispatcher;
class btDefaultCollisionConfiguration;
class btSequentialImpulseConstraintSolver;
class btRigidBody;
class btCollisionShape;
class btDefaultMotionState;
class btTriangleMesh;

namespace Engine
{

    class Scene;

    // 碰撞事件类型
    enum class CollisionEventType
    {
        Enter, // 新碰撞
        Stay,  // 持续碰撞
        Exit   // 碰撞结束
    };

    // 碰撞事件信息
    struct CollisionEvent
    {
        CollisionEventType Type = CollisionEventType::Enter;
        entt::entity       EntityA;
        entt::entity       EntityB;
        glm::vec3          ContactPoint;
        glm::vec3          ContactNormal; // A → B 方向
        float              Impulse;
        bool               IsTrigger = false; // 是否为触发器事件
    };

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
        // Kinematic 体：从 ECS Transform 同步到 Bullet
        void SyncFromECS(entt::registry& reg);

        // 获取本帧碰撞事件（Step 后有效），包含 Enter/Stay/Exit
        const std::vector<CollisionEvent>& GetCollisionEvents() const { return m_CollisionEvents; }

    private:
        btDiscreteDynamicsWorld*             m_DynamicsWorld   = nullptr;
        btBroadphaseInterface*               m_Broadphase      = nullptr;
        btCollisionDispatcher*               m_Dispatcher      = nullptr;
        btDefaultCollisionConfiguration*     m_CollisionConfig = nullptr;
        btSequentialImpulseConstraintSolver* m_Solver          = nullptr;

        struct BodyInfo
        {
            btRigidBody*          body         = nullptr;
            btCollisionShape*     shape        = nullptr;
            btDefaultMotionState* motionState  = nullptr;
            btTriangleMesh*       triangleMesh = nullptr; // MeshCollider 静态网格数据
            bool                  isTrigger    = false;
        };

        std::unordered_map<uint32_t, BodyInfo> m_Bodies; // entt::entity -> BodyInfo

        // 碰撞对跟踪（用于 Enter/Stay/Exit 判定）
        struct ContactPairHash
        {
            size_t operator()(const std::pair<uint32_t, uint32_t>& p) const
            {
                return std::hash<uint64_t>()((static_cast<uint64_t>(p.first) << 32) | p.second);
            }
        };

        // 碰撞对中存储的信息
        struct ContactPairInfo
        {
            glm::vec3 ContactPoint  = {0, 0, 0};
            glm::vec3 ContactNormal = {0, 0, 0};
            float     Impulse       = 0.0f;
            bool      IsTrigger     = false;
        };

        using ContactPairSet = std::unordered_map<std::pair<uint32_t, uint32_t>, ContactPairInfo, ContactPairHash>;
        ContactPairSet m_PreviousFrameContacts;
        ContactPairSet m_CurrentFrameContacts;

        // 本帧碰撞事件（含 Enter/Stay/Exit）
        std::vector<CollisionEvent> m_CollisionEvents;

        void DestroyBodies();
        void CollectCollisionEvents(entt::registry& reg);

        // 辅助：判断实体是否为触发器
        bool IsEntityTrigger(entt::registry& reg, entt::entity entity);
    };

} // namespace Engine
