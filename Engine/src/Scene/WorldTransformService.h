#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Engine
{

    class SceneEntityIndex;

    class WorldTransformService
    {
    public:
        static glm::mat4 ComputeWorldTransform(entt::registry& reg, entt::entity entity,
                                                const SceneEntityIndex& index);
    };

} // namespace Engine
