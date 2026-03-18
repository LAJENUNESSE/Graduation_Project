#include "engpch.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/CollisionMath.h"
#include "Core/Log.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine
{
    namespace
    {
        glm::vec3 AbsVec3(const glm::vec3& value)
        {
            return {std::abs(value.x), std::abs(value.y), std::abs(value.z)};
        }

        glm::vec3 RotateScaledOffset(const glm::quat& rotation, const glm::vec3& offset, const glm::vec3& scale)
        {
            return rotation * (offset * scale);
        }

        glm::vec3 ComputeWorldAABBHalfExtents(const glm::vec3& localHalfExtents, const glm::quat& rotation)
        {
            const glm::mat3 basis = glm::mat3_cast(rotation);
            return {std::abs(basis[0][0]) * localHalfExtents.x + std::abs(basis[1][0]) * localHalfExtents.y +
                        std::abs(basis[2][0]) * localHalfExtents.z,
                    std::abs(basis[0][1]) * localHalfExtents.x + std::abs(basis[1][1]) * localHalfExtents.y +
                        std::abs(basis[2][1]) * localHalfExtents.z,
                    std::abs(basis[0][2]) * localHalfExtents.x + std::abs(basis[1][2]) * localHalfExtents.y +
                        std::abs(basis[2][2]) * localHalfExtents.z};
        }

        // 根据碰撞器形状计算世界空间逆惯性张量
        glm::mat3 ComputeWorldInverseInertiaTensor(entt::registry&           reg,
                                                   entt::entity              entity,
                                                   const TransformComponent& transform,
                                                   const RigidBodyComponent& rb)
        {
            if (rb.Mass <= 0.0f || rb.FixedRotation)
                return glm::mat3(0.0f);

            float     m = rb.Mass;
            glm::mat3 I_local(0.0f); // 局部惯性张量

            if (reg.all_of<BoxColliderComponent>(entity))
            {
                auto&     bc  = reg.get<BoxColliderComponent>(entity);
                glm::vec3 abs = AbsVec3(transform.Scale);
                glm::vec3 he  = bc.HalfExtents * abs;
                float     w2  = (2.0f * he.x) * (2.0f * he.x);
                float     h2  = (2.0f * he.y) * (2.0f * he.y);
                float     d2  = (2.0f * he.z) * (2.0f * he.z);
                I_local[0][0] = m / 12.0f * (h2 + d2);
                I_local[1][1] = m / 12.0f * (w2 + d2);
                I_local[2][2] = m / 12.0f * (w2 + h2);
            }
            else if (reg.all_of<SphereColliderComponent>(entity))
            {
                auto& sc = reg.get<SphereColliderComponent>(entity);
                float maxScale =
                    std::max({std::abs(transform.Scale.x), std::abs(transform.Scale.y), std::abs(transform.Scale.z)});
                float r        = sc.Radius * maxScale;
                float I_scalar = 0.4f * m * r * r; // 2/5 * m * r²
                I_local        = glm::mat3(I_scalar);
            }
            else
            {
                // 无碰撞器：球近似 r=1
                float I_scalar = 0.4f * m;
                I_local        = glm::mat3(I_scalar);
            }

            // 逆惯性张量（局部空间，对角矩阵直接取倒数）
            glm::mat3 I_local_inv(0.0f);
            for (int i = 0; i < 3; i++)
                I_local_inv[i][i] = (I_local[i][i] > 1e-8f) ? 1.0f / I_local[i][i] : 0.0f;

            // 转世界空间: R * I_local_inv * R^T
            glm::mat3 R = glm::mat3_cast(glm::quat(transform.Rotation));
            return R * I_local_inv * glm::transpose(R);
        }
    } // namespace

    void PhysicsWorld::Init(glm::vec3 gravity)
    {
        m_Gravity     = gravity;
        m_Accumulator = 0.0f;
        m_Contacts.clear();
    }

    void PhysicsWorld::Step(float dt, entt::registry& reg)
    {
        // 防止死亡螺旋：截断极端 dt
        if (dt > MAX_DT)
            dt = MAX_DT;

        m_Accumulator += dt;

        int steps = 0;
        // 固定步长累加器模式，防止帧率波动影响物理
        while (m_Accumulator >= FIXED_DT && steps < MAX_SUBSTEPS)
        {
            Integrate(reg, FIXED_DT);
            DetectCollisions(reg);
            // 多次迭代求解约束（类似 Bullet 的 sequential impulse solver）
            // 单次迭代在高速碰撞时穿透严重，多次迭代可渐进收敛
            for (int iter = 0; iter < SOLVER_ITERATIONS; ++iter)
            {
                ResolveCollisions(reg);
                // 重新检测以更新穿透深度（位置修正后碰撞状态变化）
                if (iter < SOLVER_ITERATIONS - 1)
                    DetectCollisions(reg);
            }
            m_Accumulator -= FIXED_DT;
            ++steps;
        }

        // 统一清零外力（在所有子步完成后，避免多子步时外力被提前清除）
        {
            auto clearView = reg.view<RigidBodyComponent>();
            for (auto e : clearView)
            {
                auto& rb  = clearView.get<RigidBodyComponent>(e);
                rb.Force  = {0, 0, 0};
                rb.Torque = {0, 0, 0};
            }
        }

        // 如果仍有剩余累积（超过最大子步数限制），丢弃避免无限累积
        if (m_Accumulator > FIXED_DT * 2)
            m_Accumulator = 0.0f;
    }

    // 半隐式欧拉积分 (Semi-implicit Euler)
    void PhysicsWorld::Integrate(entt::registry& reg, float dt)
    {
        auto view = reg.view<TransformComponent, RigidBodyComponent>();
        for (auto entity : view)
        {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rb        = view.get<RigidBodyComponent>(entity);

            if (rb.Type != RigidBodyComponent::BodyType::Dynamic)
                continue;

            float invMass = (rb.Mass > 0.0f) ? 1.0f / rb.Mass : 0.0f;

            // 加速度 = 重力 * 重力缩放 + 外力 / 质量
            glm::vec3 acceleration = m_Gravity * rb.GravityScale + rb.Force * invMass;

            // 半隐式欧拉：先更新速度，再更新位置
            rb.LinearVelocity += acceleration * dt;
            transform.Translation += rb.LinearVelocity * dt;

            // 角速度积分
            if (!rb.FixedRotation)
            {
                glm::mat3 invIWorld = ComputeWorldInverseInertiaTensor(reg, entity, transform, rb);
                rb.AngularVelocity += invIWorld * rb.Torque * dt;

                // 四元数积分避免万向节锁
                glm::quat q = glm::quat(transform.Rotation);
                glm::quat w(0.0f, rb.AngularVelocity.x, rb.AngularVelocity.y, rb.AngularVelocity.z);
                q += 0.5f * dt * w * q;
                q                  = glm::normalize(q);
                transform.Rotation = glm::eulerAngles(q);
            }
        }
    }

    void PhysicsWorld::DetectCollisions(entt::registry& reg)
    {
        m_Contacts.clear();

        // 收集所有碰撞体
        struct ColliderInfo
        {
            entt::entity entity;
            glm::vec3    worldPos;
            enum Type
            {
                Box,
                Sphere
            } type;
            glm::vec3 halfExtents;          // Box/OBB 本地半尺寸
            glm::quat rotation;             // Box 朝向
            glm::vec3 worldAABBHalfExtents; // 用于保守 Box-Box 检测
            float     radius;               // Sphere
        };

        std::vector<ColliderInfo> colliders;

        // 盒碰撞器
        {
            auto view = reg.view<TransformComponent, BoxColliderComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                auto& box       = view.get<BoxColliderComponent>(entity);

                const glm::quat rotation(transform.Rotation);
                const glm::vec3 absScale    = AbsVec3(transform.Scale);
                const glm::vec3 halfExtents = box.HalfExtents * absScale;

                ColliderInfo ci;
                ci.entity      = entity;
                ci.worldPos    = transform.Translation + RotateScaledOffset(rotation, box.Offset, transform.Scale);
                ci.type        = ColliderInfo::Box;
                ci.halfExtents = halfExtents;
                ci.rotation    = rotation;
                ci.worldAABBHalfExtents = ComputeWorldAABBHalfExtents(halfExtents, rotation);
                ci.radius               = 0.0f;
                colliders.push_back(ci);
            }
        }

        // 球碰撞器
        {
            auto view = reg.view<TransformComponent, SphereColliderComponent>();
            for (auto entity : view)
            {
                auto& transform = view.get<TransformComponent>(entity);
                auto& sphere    = view.get<SphereColliderComponent>(entity);

                const glm::quat rotation(transform.Rotation);
                const glm::vec3 absScale = AbsVec3(transform.Scale);

                ColliderInfo ci;
                ci.entity      = entity;
                ci.worldPos    = transform.Translation + RotateScaledOffset(rotation, sphere.Offset, transform.Scale);
                ci.type        = ColliderInfo::Sphere;
                ci.halfExtents = {0, 0, 0};
                ci.rotation    = rotation;
                ci.worldAABBHalfExtents = {0, 0, 0};
                // 取 Scale 最大分量作为球半径缩放
                float maxScale = std::max({absScale.x, absScale.y, absScale.z});
                ci.radius      = sphere.Radius * maxScale;
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
                    collided =
                        OBBOBB(a.worldPos, a.halfExtents, a.rotation, b.worldPos, b.halfExtents, b.rotation, info);
                }
                else if (a.type == ColliderInfo::Sphere && b.type == ColliderInfo::Box)
                {
                    collided = SphereOBB(a.worldPos, a.radius, b.worldPos, b.halfExtents, b.rotation, info);
                    // SphereOBB 返回盒子到球的法线，需要保持 A→B 方向
                }
                else if (a.type == ColliderInfo::Box && b.type == ColliderInfo::Sphere)
                {
                    collided = SphereOBB(b.worldPos, b.radius, a.worldPos, a.halfExtents, a.rotation, info);
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

    // ===== 碰撞检测算法（委托给 CollisionMath）=====

    bool PhysicsWorld::SphereSphere(
        const glm::vec3& posA, float radiusA, const glm::vec3& posB, float radiusB, CollisionInfo& info)
    {
        return CollisionMath::SphereSphere(posA, radiusA, posB, radiusB, info);
    }

    bool PhysicsWorld::AABBAABB(const glm::vec3& posA,
                                const glm::vec3& halfA,
                                const glm::vec3& posB,
                                const glm::vec3& halfB,
                                CollisionInfo&   info)
    {
        return CollisionMath::AABBAABB(posA, halfA, posB, halfB, info);
    }

    bool PhysicsWorld::OBBOBB(const glm::vec3& posA,
                              const glm::vec3& halfA,
                              const glm::quat& rotA,
                              const glm::vec3& posB,
                              const glm::vec3& halfB,
                              const glm::quat& rotB,
                              CollisionInfo&   info)
    {
        return CollisionMath::OBBOBB(posA, halfA, rotA, posB, halfB, rotB, info);
    }

    bool PhysicsWorld::SphereOBB(const glm::vec3& spherePos,
                                 float            sphereRadius,
                                 const glm::vec3& boxPos,
                                 const glm::vec3& boxHalf,
                                 const glm::quat& boxRotation,
                                 CollisionInfo&   info)
    {
        return CollisionMath::SphereOBB(spherePos, sphereRadius, boxPos, boxHalf, boxRotation, info);
    }
    // ===== Phase 9c: 冲量碰撞响应（论文核心亮点）=====

    void PhysicsWorld::ResolveCollisions(entt::registry& reg)
    {
        constexpr float SLOP           = 0.005f; // 允许的微小穿透量（防抖动）
        constexpr float BAUMGARTE      = 0.2f;   // 速度级 Baumgarte 偏置系数
        constexpr float EPSILON        = 1e-6f;
        constexpr float REST_THRESHOLD = 0.5f; // 低于此速度视为静止，取消弹性

        for (auto& contact : m_Contacts)
        {
            auto entityA = contact.entityA;
            auto entityB = contact.entityB;

            // 获取刚体组件（如果有）
            RigidBodyComponent* rbA =
                reg.all_of<RigidBodyComponent>(entityA) ? &reg.get<RigidBodyComponent>(entityA) : nullptr;
            RigidBodyComponent* rbB =
                reg.all_of<RigidBodyComponent>(entityB) ? &reg.get<RigidBodyComponent>(entityB) : nullptr;

            // 至少一个需要是 Dynamic
            bool aIsDynamic = rbA && rbA->Type == RigidBodyComponent::BodyType::Dynamic;
            bool bIsDynamic = rbB && rbB->Type == RigidBodyComponent::BodyType::Dynamic;
            if (!aIsDynamic && !bIsDynamic)
                continue;

            auto& transformA = reg.get<TransformComponent>(entityA);
            auto& transformB = reg.get<TransformComponent>(entityB);

            float massA    = (rbA && aIsDynamic) ? rbA->Mass : 0.0f;
            float massB    = (rbB && bIsDynamic) ? rbB->Mass : 0.0f;
            float invMassA = (massA > 0.0f) ? 1.0f / massA : 0.0f;
            float invMassB = (massB > 0.0f) ? 1.0f / massB : 0.0f;

            // 世界空间逆惯性张量
            glm::mat3 invIA = (rbA && aIsDynamic) ? ComputeWorldInverseInertiaTensor(reg, entityA, transformA, *rbA)
                                                  : glm::mat3(0.0f);
            glm::mat3 invIB = (rbB && bIsDynamic) ? ComputeWorldInverseInertiaTensor(reg, entityB, transformB, *rbB)
                                                  : glm::mat3(0.0f);

            glm::vec3 n = contact.contactNormal;

            // ===== 位置修正（直接全量修正，先于冲量）=====
            // 先修正位置，保证后续速度计算基于正确的几何状态
            float penetration = contact.penetrationDepth;
            if (penetration > SLOP)
            {
                float     correctionMag = (penetration - SLOP) / (invMassA + invMassB + EPSILON);
                glm::vec3 correction    = correctionMag * n;

                if (rbA && aIsDynamic)
                    transformA.Translation -= correction * invMassA;
                if (rbB && bIsDynamic)
                    transformB.Translation += correction * invMassB;
            }

            // 接触点相对于质心的向量
            glm::vec3 rA = contact.contactPoint - transformA.Translation;
            glm::vec3 rB = contact.contactPoint - transformB.Translation;

            // 接触点速度（线速度 + 角速度×r）
            glm::vec3 vA = (rbA ? rbA->LinearVelocity : glm::vec3(0)) +
                           (rbA ? glm::cross(rbA->AngularVelocity, rA) : glm::vec3(0));
            glm::vec3 vB = (rbB ? rbB->LinearVelocity : glm::vec3(0)) +
                           (rbB ? glm::cross(rbB->AngularVelocity, rB) : glm::vec3(0));

            // 相对速度
            glm::vec3 vRel = vB - vA;
            float     vn   = glm::dot(vRel, n);

            // 如果正在分离，跳过冲量（位置已修正）
            if (vn > 0.0f)
                continue;

            // 弹性系数：低速碰撞时置零，防止无限微弹跳导致逐步下沉
            float e = std::min(rbA ? rbA->Restitution : 0.0f, rbB ? rbB->Restitution : 0.0f);
            if (std::abs(vn) < REST_THRESHOLD)
                e = 0.0f;

            // ===== 速度级 Baumgarte 偏置 =====
            // 将穿透量转化为目标分离速度，注入冲量公式
            // 确保即使冲量不够也能通过速度修正推开物体
            float biasPenetration = 0.0f;
            if (penetration > SLOP)
                biasPenetration = (BAUMGARTE / FIXED_DT) * (penetration - SLOP);

            // ===== 带旋转的冲量大小 j =====
            // j = (-(1 + e) * v_n + bias) / (1/mA + 1/mB + angularTerms)
            glm::vec3 rAxN = glm::cross(rA, n);
            glm::vec3 rBxN = glm::cross(rB, n);

            float angularTermA = glm::dot(glm::cross(invIA * rAxN, rA), n);
            float angularTermB = glm::dot(glm::cross(invIB * rBxN, rB), n);

            float denominator = invMassA + invMassB + angularTermA + angularTermB;
            if (denominator < EPSILON)
                continue;

            float j = (-(1.0f + e) * vn + biasPenetration) / denominator;
            if (j < 0.0f)
                j = 0.0f; // 冲量只推开，不拉近

            // 施加法线冲量
            glm::vec3 impulse = j * n;

            if (rbA && aIsDynamic)
            {
                rbA->LinearVelocity -= impulse * invMassA;
                if (!rbA->FixedRotation)
                    rbA->AngularVelocity -= invIA * glm::cross(rA, impulse);
            }
            if (rbB && bIsDynamic)
            {
                rbB->LinearVelocity += impulse * invMassB;
                if (!rbB->FixedRotation)
                    rbB->AngularVelocity += invIB * glm::cross(rB, impulse);
            }

            // ===== 库仑摩擦 =====
            float friction = std::sqrt((rbA ? rbA->Friction : 0.5f) * (rbB ? rbB->Friction : 0.5f));

            // 重新计算切向速度
            vA = (rbA ? rbA->LinearVelocity : glm::vec3(0)) +
                 (rbA ? glm::cross(rbA->AngularVelocity, rA) : glm::vec3(0));
            vB = (rbB ? rbB->LinearVelocity : glm::vec3(0)) +
                 (rbB ? glm::cross(rbB->AngularVelocity, rB) : glm::vec3(0));
            vRel = vB - vA;

            glm::vec3 vTangent   = vRel - glm::dot(vRel, n) * n;
            float     tangentLen = glm::length(vTangent);

            if (tangentLen > EPSILON)
            {
                glm::vec3 t = vTangent / tangentLen;

                // 切向冲量大小
                glm::vec3 rAxT       = glm::cross(rA, t);
                glm::vec3 rBxT       = glm::cross(rB, t);
                float     angTermA_t = glm::dot(glm::cross(invIA * rAxT, rA), t);
                float     angTermB_t = glm::dot(glm::cross(invIB * rBxT, rB), t);
                float     denom_t    = invMassA + invMassB + angTermA_t + angTermB_t;

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
                            rbA->AngularVelocity -= invIA * glm::cross(rA, frictionImpulse);
                    }
                    if (rbB && bIsDynamic)
                    {
                        rbB->LinearVelocity += frictionImpulse * invMassB;
                        if (!rbB->FixedRotation)
                            rbB->AngularVelocity += invIB * glm::cross(rB, frictionImpulse);
                    }
                }
            }
        }
    }

} // namespace Engine
