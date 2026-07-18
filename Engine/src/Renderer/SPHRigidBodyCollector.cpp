#include "engpch.h"
#include "Renderer/SPHCommon.h"
#include "Scene/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cstdlib>

namespace Engine
{

    std::vector<GPURigidBodyData>
    CollectRigidBodies(entt::registry* registry, uint32_t maxRigidBodies, RigidBodyUploadFilter filter)
    {
        std::vector<GPURigidBodyData> bodies;
        if (!registry)
            return bodies;

        const bool requireRigidBody = (filter == RigidBodyUploadFilter::RequireRigidBodyComponent);
        bodies.reserve(maxRigidBodies);

        // 收集 box collider
        auto boxView = registry->view<TransformComponent, BoxColliderComponent>();
        for (auto entity : boxView)
        {
            if (bodies.size() >= maxRigidBodies)
                break;
            if (requireRigidBody && !registry->all_of<RigidBodyComponent>(entity))
                continue;

            auto&     tc       = boxView.get<TransformComponent>(entity);
            auto&     bc       = boxView.get<BoxColliderComponent>(entity);
            glm::quat rot      = glm::quat(tc.Rotation);
            glm::mat4 rotMat   = glm::toMat4(rot);
            glm::vec3 absScale = glm::vec3(std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z));

            GPURigidBodyData body{};
            body.posAndType  = glm::vec4(tc.Translation + rot * (bc.Offset * tc.Scale), 0.0f);
            body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            body.halfExtents = glm::vec4(bc.HalfExtents * absScale, 0.0f);
            if (registry->all_of<RigidBodyComponent>(entity))
            {
                auto& rb        = registry->get<RigidBodyComponent>(entity);
                body.linearVel  = glm::vec4(rb.LinearVelocity, 0.0f);
                body.angularVel = glm::vec4(rb.AngularVelocity, 0.0f);
            }
            bodies.push_back(body);
        }

        // 收集 sphere collider
        auto sphereView = registry->view<TransformComponent, SphereColliderComponent>();
        for (auto entity : sphereView)
        {
            if (bodies.size() >= maxRigidBodies)
                break;
            if (requireRigidBody && !registry->all_of<RigidBodyComponent>(entity))
                continue;

            auto&     tc     = sphereView.get<TransformComponent>(entity);
            auto&     sc     = sphereView.get<SphereColliderComponent>(entity);
            glm::quat rot    = glm::quat(tc.Rotation);
            glm::mat4 rotMat = glm::toMat4(rot);

            GPURigidBodyData body{};
            body.posAndType  = glm::vec4(tc.Translation + rot * (sc.Offset * tc.Scale), 1.0f);
            body.rotCol0     = glm::vec4(rotMat[0][0], rotMat[0][1], rotMat[0][2], 0.0f);
            body.rotCol1     = glm::vec4(rotMat[1][0], rotMat[1][1], rotMat[1][2], 0.0f);
            body.rotCol2     = glm::vec4(rotMat[2][0], rotMat[2][1], rotMat[2][2], 0.0f);
            float maxScale   = std::max({std::abs(tc.Scale.x), std::abs(tc.Scale.y), std::abs(tc.Scale.z)});
            body.halfExtents = glm::vec4(sc.Radius * maxScale, 0.0f, 0.0f, 0.0f);
            if (registry->all_of<RigidBodyComponent>(entity))
            {
                auto& rb        = registry->get<RigidBodyComponent>(entity);
                body.linearVel  = glm::vec4(rb.LinearVelocity, 0.0f);
                body.angularVel = glm::vec4(rb.AngularVelocity, 0.0f);
            }
            bodies.push_back(body);
        }

        return bodies;
    }

} // namespace Engine
