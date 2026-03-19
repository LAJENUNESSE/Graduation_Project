#include "engpch.h"
#include "Physics/CollisionMath.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Engine
{
    namespace CollisionMath
    {

        bool
        SphereSphere(const glm::vec3& posA, float radiusA, const glm::vec3& posB, float radiusB, CollisionInfo& info)
        {
            glm::vec3 diff = posB - posA;
            float     dist = glm::length(diff);
            float     sumR = radiusA + radiusB;

            if (dist >= sumR)
                return false;

            if (dist < 1e-6f)
            {
                // 两球完全重合：使用默认分离轴
                info.contactNormal    = glm::vec3(0, 1, 0);
                info.penetrationDepth = sumR;
                info.contactPoint     = posA;
                return true;
            }

            info.contactNormal    = diff / dist; // A→B
            info.penetrationDepth = sumR - dist;
            info.contactPoint     = posA + info.contactNormal * radiusA;
            return true;
        }

        bool AABBAABB(const glm::vec3& posA,
                      const glm::vec3& halfA,
                      const glm::vec3& posB,
                      const glm::vec3& halfB,
                      CollisionInfo&   info)
        {
            glm::vec3 diff = posB - posA;

            float overlapX = (halfA.x + halfB.x) - std::abs(diff.x);
            if (overlapX <= 0)
                return false;

            float overlapY = (halfA.y + halfB.y) - std::abs(diff.y);
            if (overlapY <= 0)
                return false;

            float overlapZ = (halfA.z + halfB.z) - std::abs(diff.z);
            if (overlapZ <= 0)
                return false;

            int minAxis;
            if (overlapX <= overlapY && overlapX <= overlapZ)
            {
                info.contactNormal    = (diff.x > 0) ? glm::vec3(1, 0, 0) : glm::vec3(-1, 0, 0);
                info.penetrationDepth = overlapX;
                minAxis               = 0;
            }
            else if (overlapY <= overlapX && overlapY <= overlapZ)
            {
                info.contactNormal    = (diff.y > 0) ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);
                info.penetrationDepth = overlapY;
                minAxis               = 1;
            }
            else
            {
                info.contactNormal    = (diff.z > 0) ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1);
                info.penetrationDepth = overlapZ;
                minAxis               = 2;
            }

            // 接触点：沿最小穿透轴投影到 A 的接触面，其他轴取重叠中心
            float sign        = (diff[minAxis] > 0) ? 1.0f : -1.0f;
            info.contactPoint = posA;
            info.contactPoint[minAxis] += sign * halfA[minAxis];
            for (int i = 0; i < 3; i++)
            {
                if (i == minAxis)
                    continue;
                float lo             = std::max(posA[i] - halfA[i], posB[i] - halfB[i]);
                float hi             = std::min(posA[i] + halfA[i], posB[i] + halfB[i]);
                info.contactPoint[i] = (lo + hi) * 0.5f;
            }
            return true;
        }

        bool OBBOBB(const glm::vec3& posA,
                    const glm::vec3& halfA,
                    const glm::quat& rotA,
                    const glm::vec3& posB,
                    const glm::vec3& halfB,
                    const glm::quat& rotB,
                    CollisionInfo&   info)
        {
            const glm::mat3 mA = glm::mat3_cast(rotA);
            const glm::mat3 mB = glm::mat3_cast(rotB);

            const glm::vec3 axesA[3] = {mA[0], mA[1], mA[2]};
            const glm::vec3 axesB[3] = {mB[0], mB[1], mB[2]};

            const glm::vec3 d = posB - posA;

            float     minOverlap = std::numeric_limits<float>::max();
            glm::vec3 bestAxis(0.0f);

            auto testAxis = [&](const glm::vec3& axis) -> bool
            {
                float len = glm::length(axis);
                if (len < 1e-6f)
                    return true;

                glm::vec3 n = axis / len;

                float rA = halfA.x * std::abs(glm::dot(axesA[0], n)) + halfA.y * std::abs(glm::dot(axesA[1], n)) +
                           halfA.z * std::abs(glm::dot(axesA[2], n));

                float rB = halfB.x * std::abs(glm::dot(axesB[0], n)) + halfB.y * std::abs(glm::dot(axesB[1], n)) +
                           halfB.z * std::abs(glm::dot(axesB[2], n));

                float dist    = std::abs(glm::dot(d, n));
                float overlap = rA + rB - dist;

                if (overlap <= 0.0f)
                    return false;

                if (overlap < minOverlap)
                {
                    minOverlap = overlap;
                    bestAxis   = n;
                }
                return true;
            };

            for (int i = 0; i < 3; ++i)
                if (!testAxis(axesA[i]))
                    return false;

            for (int i = 0; i < 3; ++i)
                if (!testAxis(axesB[i]))
                    return false;

            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    if (!testAxis(glm::cross(axesA[i], axesB[j])))
                        return false;

            if (glm::dot(bestAxis, d) < 0.0f)
                bestAxis = -bestAxis;

            info.contactNormal    = bestAxis;
            info.penetrationDepth = minOverlap;
            // 接触点：沿分离轴方向，从两中心向接触面投影取中点
            float projA   = glm::dot(posA, bestAxis);
            float projB   = glm::dot(posB, bestAxis);
            float rA_proj = 0.0f;
            float rB_proj = 0.0f;
            for (int i = 0; i < 3; i++)
            {
                rA_proj += halfA[i] * std::abs(glm::dot(axesA[i], bestAxis));
                rB_proj += halfB[i] * std::abs(glm::dot(axesB[i], bestAxis));
            }
            // A 面在 bestAxis 方向的投影位置
            float faceA = projA + rA_proj;
            // B 面在 bestAxis 方向的投影位置
            float faceB       = projB - rB_proj;
            float contactProj = (faceA + faceB) * 0.5f;
            // 从 A 中心沿 bestAxis 偏移到接触面
            info.contactPoint = posA + bestAxis * (contactProj - projA);
            return true;
        }

        bool SphereOBB(const glm::vec3& spherePos,
                       float            sphereRadius,
                       const glm::vec3& boxPos,
                       const glm::vec3& boxHalf,
                       const glm::quat& boxRotation,
                       CollisionInfo&   info)
        {
            const glm::quat inverseRotation = glm::inverse(boxRotation);
            const glm::vec3 localSphere     = inverseRotation * (spherePos - boxPos);

            glm::vec3 closest;
            closest.x = std::clamp(localSphere.x, -boxHalf.x, boxHalf.x);
            closest.y = std::clamp(localSphere.y, -boxHalf.y, boxHalf.y);
            closest.z = std::clamp(localSphere.z, -boxHalf.z, boxHalf.z);

            glm::vec3 localDiff = localSphere - closest;
            float     distSq    = glm::dot(localDiff, localDiff);
            glm::vec3 localNormal(0.0f);

            if (distSq < 1e-6f)
            {
                float dx = boxHalf.x - std::abs(localSphere.x);
                float dy = boxHalf.y - std::abs(localSphere.y);
                float dz = boxHalf.z - std::abs(localSphere.z);

                if (dx <= 0.0f || dy <= 0.0f || dz <= 0.0f)
                    return false;

                if (dx <= dy && dx <= dz)
                {
                    localNormal           = (localSphere.x >= 0.0f) ? glm::vec3(1, 0, 0) : glm::vec3(-1, 0, 0);
                    closest               = {localNormal.x * boxHalf.x, localSphere.y, localSphere.z};
                    info.penetrationDepth = dx + sphereRadius;
                }
                else if (dy <= dx && dy <= dz)
                {
                    localNormal           = (localSphere.y >= 0.0f) ? glm::vec3(0, 1, 0) : glm::vec3(0, -1, 0);
                    closest               = {localSphere.x, localNormal.y * boxHalf.y, localSphere.z};
                    info.penetrationDepth = dy + sphereRadius;
                }
                else
                {
                    localNormal           = (localSphere.z >= 0.0f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 0, -1);
                    closest               = {localSphere.x, localSphere.y, localNormal.z * boxHalf.z};
                    info.penetrationDepth = dz + sphereRadius;
                }
            }
            else
            {
                float dist = std::sqrt(distSq);
                if (dist >= sphereRadius)
                    return false;

                localNormal           = localDiff / dist;
                info.penetrationDepth = sphereRadius - dist;
            }

            info.contactNormal = boxRotation * localNormal;
            info.contactPoint  = boxPos + boxRotation * closest;
            return true;
        }

    } // namespace CollisionMath

} // namespace Engine
