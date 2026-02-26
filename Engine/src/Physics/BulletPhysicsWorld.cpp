#include "engpch.h"
#include "Physics/BulletPhysicsWorld.h"
#include "Scene/Components.h"
#include "Core/Log.h"

#include <btBulletDynamicsCommon.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine
{

    // GLM ↔ Bullet 转换辅助
    static btVector3 ToBt(const glm::vec3& v)
    {
        return btVector3(v.x, v.y, v.z);
    }

    static glm::vec3 ToGlm(const btVector3& v)
    {
        return glm::vec3(v.getX(), v.getY(), v.getZ());
    }

    static btQuaternion EulerToBtQuat(const glm::vec3& euler)
    {
        glm::quat q(euler);
        return btQuaternion(q.x, q.y, q.z, q.w);
    }

    static glm::vec3 BtQuatToEuler(const btQuaternion& q)
    {
        glm::quat gq(q.getW(), q.getX(), q.getY(), q.getZ());
        return glm::eulerAngles(gq);
    }

    BulletPhysicsWorld::BulletPhysicsWorld()
    {
    }

    BulletPhysicsWorld::~BulletPhysicsWorld()
    {
        Shutdown();
    }

    void BulletPhysicsWorld::Init(glm::vec3 gravity)
    {
        m_CollisionConfig = new btDefaultCollisionConfiguration();
        m_Dispatcher = new btCollisionDispatcher(m_CollisionConfig);
        m_Broadphase = new btDbvtBroadphase();
        m_Solver = new btSequentialImpulseConstraintSolver();
        m_DynamicsWorld = new btDiscreteDynamicsWorld(m_Dispatcher, m_Broadphase, m_Solver, m_CollisionConfig);
        m_DynamicsWorld->setGravity(ToBt(gravity));
    }

    void BulletPhysicsWorld::Shutdown()
    {
        DestroyBodies();

        delete m_DynamicsWorld;
        m_DynamicsWorld = nullptr;
        delete m_Solver;
        m_Solver = nullptr;
        delete m_Broadphase;
        m_Broadphase = nullptr;
        delete m_Dispatcher;
        m_Dispatcher = nullptr;
        delete m_CollisionConfig;
        m_CollisionConfig = nullptr;
    }

    void BulletPhysicsWorld::CreateBodies(entt::registry& reg)
    {
        if (!m_DynamicsWorld)
            return;

        auto view = reg.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view)
        {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rb = view.get<RigidBodyComponent>(entity);

            uint32_t entityId = static_cast<uint32_t>(entity);

            // 确定碰撞形状
            btCollisionShape* shape = nullptr;

            if (reg.all_of<SphereColliderComponent>(entity))
            {
                auto& sphere = reg.get<SphereColliderComponent>(entity);
                float maxScale = std::max({transform.Scale.x, transform.Scale.y, transform.Scale.z});
                shape = new btSphereShape(sphere.Radius * maxScale);
            }
            else if (reg.all_of<BoxColliderComponent>(entity))
            {
                auto& box = reg.get<BoxColliderComponent>(entity);
                shape = new btBoxShape(ToBt(box.HalfExtents * transform.Scale));
            }
            else
            {
                // 没有碰撞器，使用默认的单位盒
                shape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
            }

            // 计算质量和惯性
            float mass = 0.0f;
            if (rb.Type == RigidBodyComponent::BodyType::Dynamic)
                mass = rb.Mass;

            btVector3 localInertia(0, 0, 0);
            if (mass > 0.0f)
                shape->calculateLocalInertia(mass, localInertia);

            // Motion State（桥接 Transform ↔ Bullet）
            btTransform startTransform;
            startTransform.setIdentity();
            startTransform.setOrigin(ToBt(transform.Translation));
            startTransform.setRotation(EulerToBtQuat(transform.Rotation));

            auto* motionState = new btDefaultMotionState(startTransform);

            btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motionState, shape, localInertia);
            rbInfo.m_restitution = rb.Restitution;
            rbInfo.m_friction = rb.Friction;

            auto* body = new btRigidBody(rbInfo);

            // 存储 entity ID 到 Bullet userIndex（int 范围足够，entt::entity 为 uint32_t）
            static_assert(sizeof(entt::entity) <= sizeof(int), "entt::entity exceeds int range");
            body->setUserIndex(static_cast<int>(entityId));

            // Kinematic 设置
            if (rb.Type == RigidBodyComponent::BodyType::Kinematic)
            {
                body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                body->setActivationState(DISABLE_DEACTIVATION);
            }

            // 固定旋转
            if (rb.FixedRotation)
            {
                body->setAngularFactor(btVector3(0, 0, 0));
            }

            // 重力缩放
            body->setGravity(m_DynamicsWorld->getGravity() * rb.GravityScale);

            m_DynamicsWorld->addRigidBody(body);

            BodyInfo info;
            info.body = body;
            info.shape = shape;
            info.motionState = motionState;
            m_Bodies[entityId] = info;
        }
    }

    void BulletPhysicsWorld::Step(float dt, entt::registry& reg)
    {
        if (!m_DynamicsWorld)
            return;

        SyncFromECS(reg);  // Kinematic: ECS → Bullet（stepSimulation 前）
        m_DynamicsWorld->stepSimulation(dt, 10, 1.0f / 60.0f);
        SyncToECS(reg);
        CollectCollisionEvents(reg);
    }

    void BulletPhysicsWorld::CollectCollisionEvents(entt::registry& reg)
    {
        m_CollisionEvents.clear();

        if (!m_Dispatcher)
            return;

        int numManifolds = m_Dispatcher->getNumManifolds();
        for (int i = 0; i < numManifolds; i++)
        {
            btPersistentManifold* manifold = m_Dispatcher->getManifoldByIndexInternal(i);
            const btCollisionObject* objA = manifold->getBody0();
            const btCollisionObject* objB = manifold->getBody1();

            int numContacts = manifold->getNumContacts();
            for (int j = 0; j < numContacts; j++)
            {
                btManifoldPoint& pt = manifold->getContactPoint(j);

                // 只处理本帧新生碰撞（impulse > 0）
                float impulse = pt.getAppliedImpulse();
                if (impulse <= 0.0f)
                    continue;

                CollisionEvent event;
                event.EntityA = static_cast<entt::entity>(objA->getUserIndex());
                event.EntityB = static_cast<entt::entity>(objB->getUserIndex());
                event.ContactPoint = ToGlm(pt.getPositionWorldOnB());
                event.ContactNormal = ToGlm(pt.m_normalWorldOnB);
                event.Impulse = impulse;

                m_CollisionEvents.push_back(event);
            }
        }
    }

    void BulletPhysicsWorld::SyncFromECS(entt::registry& reg)
    {
        for (auto& [entityId, info] : m_Bodies)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (!reg.valid(entity))
                continue;

            if (!reg.all_of<TransformComponent, RigidBodyComponent>(entity))
                continue;

            auto& rb = reg.get<RigidBodyComponent>(entity);
            if (rb.Type != RigidBodyComponent::BodyType::Kinematic)
                continue;

            auto& transform = reg.get<TransformComponent>(entity);
            btTransform btTrans;
            btTrans.setIdentity();
            btTrans.setOrigin(ToBt(transform.Translation));
            btTrans.setRotation(EulerToBtQuat(transform.Rotation));
            info.motionState->setWorldTransform(btTrans);
        }
    }

    void BulletPhysicsWorld::SyncToECS(entt::registry& reg)
    {
        for (auto& [entityId, info] : m_Bodies)
        {
            auto entity = static_cast<entt::entity>(entityId);
            if (!reg.valid(entity))
                continue;

            if (!reg.all_of<TransformComponent, RigidBodyComponent>(entity))
                continue;

            auto& rb = reg.get<RigidBodyComponent>(entity);
            if (rb.Type != RigidBodyComponent::BodyType::Dynamic)
                continue;

            auto& transform = reg.get<TransformComponent>(entity);

            btTransform btTrans;
            info.motionState->getWorldTransform(btTrans);

            transform.Translation = ToGlm(btTrans.getOrigin());
            if (!rb.FixedRotation)
                transform.Rotation = BtQuatToEuler(btTrans.getRotation());

            // 同步速度到 ECS 组件
            rb.LinearVelocity = ToGlm(info.body->getLinearVelocity());
            rb.AngularVelocity = ToGlm(info.body->getAngularVelocity());
        }
    }

    void BulletPhysicsWorld::DestroyBody(entt::entity entity)
    {
        if (!m_DynamicsWorld)
            return;

        uint32_t entityId = static_cast<uint32_t>(entity);
        auto it = m_Bodies.find(entityId);
        if (it == m_Bodies.end())
            return;

        auto& info = it->second;
        if (info.body)
        {
            m_DynamicsWorld->removeRigidBody(info.body);
            delete info.body;
        }
        delete info.shape;
        delete info.motionState;
        m_Bodies.erase(it);
    }

    void BulletPhysicsWorld::DestroyBodies()
    {
        if (!m_DynamicsWorld)
            return;

        for (auto& [id, info] : m_Bodies)
        {
            if (info.body)
            {
                m_DynamicsWorld->removeRigidBody(info.body);
                delete info.body;
            }
            delete info.shape;
            delete info.motionState;
        }
        m_Bodies.clear();
    }

} // namespace Engine
