#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <entt/entt.hpp>

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

    // 碰撞检测纯数学函数，从 PhysicsWorld 提取以便单元测试
    namespace CollisionMath
    {
        bool SphereSphere(
            const glm::vec3& posA, float radiusA, const glm::vec3& posB, float radiusB, CollisionInfo& info);

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

    } // namespace CollisionMath

} // namespace Engine
