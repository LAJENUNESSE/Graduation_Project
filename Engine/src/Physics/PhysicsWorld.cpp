#include "engpch.h"
#include "Physics/PhysicsWorld.h"
#include "Scene/Components.h"
#include "Core/Log.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace Engine
{

    void PhysicsWorld::Init(glm::vec3 gravity)
    {
        m_Gravity = gravity;
        m_Accumulator = 0.0f;
        m_Contacts.clear();
    }

    void PhysicsWorld::Step(float dt, entt::registry& reg)
    {
        m_Accumulator += dt;

        // 固定步长累加器模式，防止帧率波动影响物理
        while (m_Accumulator >= FIXED_DT)
        {
            Integrate(reg, FIXED_DT);
            DetectCollisions(reg);
            ResolveCollisions(reg);
            m_Accumulator -= FIXED_DT;
        }
    }

    // 半隐式欧拉积分 (Semi-implicit Euler)
    void PhysicsWorld::Integrate(entt::registry& reg, float dt)
    {
        auto view = reg.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view)
        {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rb = view.get<RigidBodyComponent>(entity);

            if (rb.Type != RigidBodyComponent::BodyType::Dynamic)
                continue;

            float invMass = (rb.Mass > 0.0f) ? 1.0f / rb.Mass : 0.0f;

            // 加速度 = 重力 * 重力缩放 + 外力 / 质量
            glm::vec3 acceleration = m_Gravity * rb.GravityScale + rb.Force * invMass;

            // 半隐式欧拉：先更新速度，再更新位置
            rb.LinearVelocity += acceleration * dt;
            transform.Translation += rb.LinearVelocity * dt;

            // 角速度积分（简化版：不考虑惯性张量变化）
            if (!rb.FixedRotation)
            {
                // 简化惯性张量为标量（球近似）：I = 2/5 * m * r^2，此处简化为 mass
                float invI = (rb.Mass > 0.0f) ? 1.0f / rb.Mass : 0.0f;
                rb.AngularVelocity += rb.Torque * invI * dt;
                transform.Rotation += rb.AngularVelocity * dt;
            }

            // 清除本帧外力
            rb.Force = {0, 0, 0};
            rb.Torque = {0, 0, 0};
        }
    }

    void PhysicsWorld::DetectCollisions(entt::registry& reg)
    {
        m_Contacts.clear();

        // 收集所有碰撞体
        struct ColliderInfo
        {
            entt::entity entity;
            glm::vec3 worldPos;
            enum Type { Box, Sphere } type;
            glm::vec3 halfExtents; // Box
            float radius;          // Sphere
        };

        std::vector<ColliderInfo> colliders;

        // 盒碰撞器
        {
            auto view = reg.view<TransformComponent, BoxColliderComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                auto& box = view.get<BoxColliderComponent>(entity);

                ColliderInfo ci;
                ci.entity = entity;
                ci.worldPos = transform.Translation + box.Offset;
                ci.type = ColliderInfo::Box;
                ci.halfExtents = box.HalfExtents * transform.Scale;
                ci.radius = 0;
                colliders.push_back(ci);
            }
        }

        // 球碰撞器
        {
            auto view = reg.view<TransformComponent, SphereColliderComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                auto& sphere = view.get<SphereColliderComponent>(entity);

                ColliderInfo ci;
                ci.entity = entity;
                ci.worldPos = transform.Translation + sphere.Offset;
                ci.type = ColliderInfo::Sphere;
                ci.halfExtents = {0, 0, 0};
                // 取 Scale 最大分量作为球半径缩放
                float maxScale = std::max({transform.Scale.x, transform.Scale.y, transform.Scale.z});
                ci.radius = sphere.Radius * maxScale;
                colliders.push_back(ci);
            }
        }

        // 两两检测（O(n^2)，场景量小够用）
        for (size_t i = 0; i < colliders.size(); i++)
        {
            for (size_t j = i + 1; j < colliders.size(); j++)
            {
                const auto& a = colliders[i];
                const auto& b = colliders[j];

                // 跳过两个都是 Static 的碰撞
                bool aIsStatic = true, bIsStatic = true;
                if (reg.all_of<RigidBodyComponent>(a.entity))
                    aIsStatic = (reg.get<RigidBodyComponent>(a.entity).Type == RigidBodyComponent::BodyType::Static);
                if (reg.all_of<RigidBodyComponent>(b.entity))
                    bIsStatic = (reg.get<RigidBodyComponent>(b.entity).Type == RigidBodyComponent::BodyType::Static);

                if (aIsStatic && bIsStatic)
                    continue;

                CollisionInfo info;
                info.entityA = a.entity;
                info.entityB = b.entity;

                bool collided = false;

                if (a.type == ColliderInfo::Sphere && b.type == ColliderInfo::Sphere)
                {
                    collided = SphereSphere(a.worldPos, a.radius, b.worldPos, b.radius, info);
                }
                else if (a.type == ColliderInfo::Box && b.type == ColliderInfo::Box)
                {
                    collided = AABBAABB(a.worldPos, a.halfExtents, b.worldPos, b.halfExtents, info);
                }
                else if (a.type == ColliderInfo::Sphere && b.type == ColliderInfo::Box)
                {
                    collided = SphereAABB(a.worldPos, a.radius, b.worldPos, b.halfExtents, info);
                    // SphereAABB 法线从球指向盒子外部，需要保证 A→B 方向
                }
                else if (a.type == ColliderInfo::Box && b.type == ColliderInfo::Sphere)
                {
                    collided = SphereAABB(b.worldPos, b.radius, a.worldPos, a.halfExtents, info);
                    // 交换 A/B，翻转法线
                    if (collided)
                    {
                        std::swap(info.entityA, info.entityB);
                        info.contactNormal = -info.contactNormal;
                    }
                }

                if (collided)
                    m_Contacts.push_back(info);
            }
        }
    }

    // ===== 碰撞检测算法 =====

    bool PhysicsWorld::SphereSphere(
        const glm::vec3& posA, float radiusA,
        const glm::vec3& posB, float radiusB,
        CollisionInfo& info)
    {
        glm::vec3 diff = posB - posA;
        float dist = glm::length(diff);
        float sumR = radiusA + radiusB;

        if (dist >= sumR || dist < 1e-6f)
            return false;

        info.contactNormal = diff / dist; // A→B
        info.penetrationDepth = sumR - dist;
        info.contactPoint = posA + info.contactNormal * radiusA;
        return true;
    }

    bool PhysicsWorld::AABBAABB(
        const glm::vec3& posA, const glm::vec3& halfA,
        const glm::vec3& posB, const glm::vec3& halfB,
        CollisionInfo& info)
    {
        glm::vec3 diff = posB - posA;

        // 6 轴分离检测
        float overlapX = (halfA.x + halfB.x) - std::abs(diff.x);
        if (overlapX <= 0) return false;

        float overlapY = (halfA.y + halfB.y) - std::abs(diff.y);
        if (overlapY <= 0) return false;

        float overlapZ = (halfA.z + halfB.z) - std::abs(diff.z);
        if (overlapZ <= 0) return false;

        // 选择穿透最小的轴作为碰撞法线
        if (overlapX <= overlapY && overlapX <= overlapZ)
        {
            info.contactNormal = (diff.x > 0) ? glm::vec3(1, 0, 0) : glm::vec3(-1, 0, 0);
            info.penetrationDepth = overlapX;
        }
        else if (overlapY <= overlapX && overlapY <= overlapZ)
        {
            info.contactNormal = (diff.y > 0) ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);
            info.penetrationDepth = overlapY;
        }
        else
        {
            info.contactNormal = (diff.z > 0) ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1);
            info.penetrationDepth = overlapZ;
        }

        info.contactPoint = (posA + posB) * 0.5f;
        return true;
    }

    bool PhysicsWorld::SphereAABB(
        const glm::vec3& spherePos, float sphereRadius,
        const glm::vec3& boxPos, const glm::vec3& boxHalf,
        CollisionInfo& info)
    {
        // 找 AABB 上距离球心最近的点
        glm::vec3 localSphere = spherePos - boxPos;
        glm::vec3 closest;
        closest.x = std::clamp(localSphere.x, -boxHalf.x, boxHalf.x);
        closest.y = std::clamp(localSphere.y, -boxHalf.y, boxHalf.y);
        closest.z = std::clamp(localSphere.z, -boxHalf.z, boxHalf.z);

        glm::vec3 diff = localSphere - closest;
        float dist = glm::length(diff);

        if (dist >= sphereRadius || dist < 1e-6f)
        {
            // 球心在盒内的情况
            if (dist < 1e-6f)
            {
                // 球心在 AABB 内部，找最近的面
                float dx = boxHalf.x - std::abs(localSphere.x);
                float dy = boxHalf.y - std::abs(localSphere.y);
                float dz = boxHalf.z - std::abs(localSphere.z);

                if (dx <= 0 || dy <= 0 || dz <= 0)
                    return false;

                if (dx <= dy && dx <= dz)
                {
                    info.contactNormal = (localSphere.x > 0) ? glm::vec3(1, 0, 0) : glm::vec3(-1, 0, 0);
                    info.penetrationDepth = dx + sphereRadius;
                }
                else if (dy <= dx && dy <= dz)
                {
                    info.contactNormal = (localSphere.y > 0) ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);
                    info.penetrationDepth = dy + sphereRadius;
                }
                else
                {
                    info.contactNormal = (localSphere.z > 0) ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1);
                    info.penetrationDepth = dz + sphereRadius;
                }
                info.contactPoint = boxPos + closest;
                return true;
            }
            return false;
        }

        info.contactNormal = diff / dist; // 从盒子指向球
        info.penetrationDepth = sphereRadius - dist;
        info.contactPoint = boxPos + closest;
        return true;
    }

    // ===== Phase 9c: 冲量碰撞响应（论文核心亮点）=====

    void PhysicsWorld::ResolveCollisions(entt::registry& reg)
    {
        constexpr float SLOP = 0.01f;       // 允许的微小穿透量（防抖动）
        constexpr float PERCENT = 0.4f;      // 每帧位置修正比例（渐进收敛）
        constexpr float EPSILON = 1e-6f;

        for (auto& contact : m_Contacts)
        {
            auto entityA = contact.entityA;
            auto entityB = contact.entityB;

            // 获取刚体组件（如果有）
            RigidBodyComponent* rbA = reg.all_of<RigidBodyComponent>(entityA)
                ? &reg.get<RigidBodyComponent>(entityA) : nullptr;
            RigidBodyComponent* rbB = reg.all_of<RigidBodyComponent>(entityB)
                ? &reg.get<RigidBodyComponent>(entityB) : nullptr;

            // 至少一个需要是 Dynamic
            bool aIsDynamic = rbA && rbA->Type == RigidBodyComponent::BodyType::Dynamic;
            bool bIsDynamic = rbB && rbB->Type == RigidBodyComponent::BodyType::Dynamic;
            if (!aIsDynamic && !bIsDynamic)
                continue;

            auto& transformA = reg.get<TransformComponent>(entityA);
            auto& transformB = reg.get<TransformComponent>(entityB);

            float massA = (rbA && aIsDynamic) ? rbA->Mass : 0.0f;
            float massB = (rbB && bIsDynamic) ? rbB->Mass : 0.0f;
            float invMassA = (massA > 0.0f) ? 1.0f / massA : 0.0f;
            float invMassB = (massB > 0.0f) ? 1.0f / massB : 0.0f;

            // 简化惯性张量（球近似）：I = 2/5 * m * r^2 ≈ m（简化）
            float invIA = (rbA && aIsDynamic && !rbA->FixedRotation) ? 1.0f / massA : 0.0f;
            float invIB = (rbB && bIsDynamic && !rbB->FixedRotation) ? 1.0f / massB : 0.0f;

            glm::vec3 n = contact.contactNormal;

            // 接触点相对于质心的向量
            glm::vec3 rA = contact.contactPoint - transformA.Translation;
            glm::vec3 rB = contact.contactPoint - transformB.Translation;

            // 接触点速度（线速度 + 角速度×r）
            glm::vec3 vA = (rbA ? rbA->LinearVelocity : glm::vec3(0))
                + (rbA ? glm::cross(rbA->AngularVelocity, rA) : glm::vec3(0));
            glm::vec3 vB = (rbB ? rbB->LinearVelocity : glm::vec3(0))
                + (rbB ? glm::cross(rbB->AngularVelocity, rB) : glm::vec3(0));

            // 相对速度
            glm::vec3 vRel = vB - vA;
            float vn = glm::dot(vRel, n);

            // 如果正在分离，跳过
            if (vn > 0.0f)
                continue;

            // 弹性系数：取两者较小值
            float e = std::min(rbA ? rbA->Restitution : 0.0f,
                               rbB ? rbB->Restitution : 0.0f);

            // ===== 带旋转的冲量大小 j =====
            // j = -(1 + e) * v_n / (1/mA + 1/mB + dot(IA⁻¹(rA×n)×rA, n) + dot(IB⁻¹(rB×n)×rB, n))
            glm::vec3 rAxN = glm::cross(rA, n);
            glm::vec3 rBxN = glm::cross(rB, n);

            float angularTermA = glm::dot(glm::cross(rAxN * invIA, rA), n);
            float angularTermB = glm::dot(glm::cross(rBxN * invIB, rB), n);

            float denominator = invMassA + invMassB + angularTermA + angularTermB;
            if (denominator < EPSILON)
                continue;

            float j = -(1.0f + e) * vn / denominator;

            // 施加法线冲量
            glm::vec3 impulse = j * n;

            if (rbA && aIsDynamic)
            {
                rbA->LinearVelocity -= impulse * invMassA;
                if (!rbA->FixedRotation)
                    rbA->AngularVelocity -= glm::cross(rA, impulse) * invIA;
            }
            if (rbB && bIsDynamic)
            {
                rbB->LinearVelocity += impulse * invMassB;
                if (!rbB->FixedRotation)
                    rbB->AngularVelocity += glm::cross(rB, impulse) * invIB;
            }

            // ===== 库仑摩擦 =====
            float friction = std::sqrt(
                (rbA ? rbA->Friction : 0.5f) * (rbB ? rbB->Friction : 0.5f));

            // 重新计算切向速度
            vA = (rbA ? rbA->LinearVelocity : glm::vec3(0))
                + (rbA ? glm::cross(rbA->AngularVelocity, rA) : glm::vec3(0));
            vB = (rbB ? rbB->LinearVelocity : glm::vec3(0))
                + (rbB ? glm::cross(rbB->AngularVelocity, rB) : glm::vec3(0));
            vRel = vB - vA;

            glm::vec3 vTangent = vRel - glm::dot(vRel, n) * n;
            float tangentLen = glm::length(vTangent);

            if (tangentLen > EPSILON)
            {
                glm::vec3 t = vTangent / tangentLen;

                // 切向冲量大小
                glm::vec3 rAxT = glm::cross(rA, t);
                glm::vec3 rBxT = glm::cross(rB, t);
                float angTermA_t = glm::dot(glm::cross(rAxT * invIA, rA), t);
                float angTermB_t = glm::dot(glm::cross(rBxT * invIB, rB), t);
                float denom_t = invMassA + invMassB + angTermA_t + angTermB_t;

                if (denom_t > EPSILON)
                {
                    float jt = -glm::dot(vRel, t) / denom_t;

                    glm::vec3 frictionImpulse;
                    if (std::abs(jt) < friction * j)
                    {
                        // 静摩擦
                        frictionImpulse = jt * t;
                    }
                    else
                    {
                        // 动摩擦
                        frictionImpulse = -friction * j * t;
                    }

                    if (rbA && aIsDynamic)
                    {
                        rbA->LinearVelocity -= frictionImpulse * invMassA;
                        if (!rbA->FixedRotation)
                            rbA->AngularVelocity -= glm::cross(rA, frictionImpulse) * invIA;
                    }
                    if (rbB && bIsDynamic)
                    {
                        rbB->LinearVelocity += frictionImpulse * invMassB;
                        if (!rbB->FixedRotation)
                            rbB->AngularVelocity += glm::cross(rB, frictionImpulse) * invIB;
                    }
                }
            }

            // ===== 位置修正（Baumgarte 稳定化）=====
            float correctionMag = std::max(contact.penetrationDepth - SLOP, 0.0f)
                / (invMassA + invMassB) * PERCENT;
            glm::vec3 correction = correctionMag * n;

            if (rbA && aIsDynamic)
                transformA.Translation -= correction * invMassA;
            if (rbB && bIsDynamic)
                transformB.Translation += correction * invMassB;
        }
    }

} // namespace Engine
