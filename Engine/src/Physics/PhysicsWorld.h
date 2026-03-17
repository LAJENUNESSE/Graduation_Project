#pragma once

#include "Scene/Entity.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Engine
{

    struct CollisionInfo
    {
        entt::entity entityA          = entt::null;
        entt::entity entityB          = entt::null;
        glm::vec3    contactPoint     = {0, 0, 0};
        glm::vec3    contactNormal    = {0, 0, 0}; // A→B 方向
        float        penetrationDepth = 0.0f;
    };

    class PhysicsWorld
    {
    public:
        void                              Init(glm::vec3 gravity = {0, -9.81f, 0});
        void                              Step(float dt, entt::registry& reg);
        const std::vector<CollisionInfo>& GetContacts() const { return m_Contacts; }

    private:
        void Integrate(entt::registry& reg, float dt);
        void DetectCollisions(entt::registry& reg);
        void ResolveCollisions(entt::registry& reg);

        // 碰撞检测辅助
        bool
        SphereSphere(const glm::vec3& posA, float radiusA, const glm::vec3& posB, float radiusB, CollisionInfo& info);

        bool AABBAABB(const glm::vec3& posA,
                      const glm::vec3& halfA,
                      const glm::vec3& posB,
                      const glm::vec3& halfB,
                      CollisionInfo&   info);

        bool OBBOBB(const glm::vec3& posA,
                    const glm::vec3& halfA,
                    const glm::quat& rotA,
                    const glm::vec3& posB,
                    const glm::vec3& halfB,
                    const glm::quat& rotB,
                    CollisionInfo&   info);

        bool SphereOBB(const glm::vec3& spherePos,
                       float            sphereRadius,
                       const glm::vec3& boxPos,
                       const glm::vec3& boxHalf,
                       const glm::quat& boxRotation,
                       CollisionInfo&   info);

        glm::vec3                  m_Gravity         = {0, -9.81f, 0};
        float                      m_Accumulator     = 0.0f;
        static constexpr float     FIXED_DT          = 1.0f / 60.0f;
        static constexpr float     MAX_DT            = 0.25f; // 单帧最大 dt 截断
        static constexpr int       MAX_SUBSTEPS      = 8;     // 单帧最大子步数
        static constexpr int       SOLVER_ITERATIONS = 6;     // 约束求解迭代次数
        std::vector<CollisionInfo> m_Contacts;
    };

} // namespace Engine
