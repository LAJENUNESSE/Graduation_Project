#pragma once

#include "Scene/Entity.h"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <unordered_map>

namespace Engine
{

    class TransformHierarchyService
    {
    public:
        static glm::mat4 ComputeWorldTransform(entt::registry& reg, entt::entity entity);

        // Caches could be added here later to optimize performance
    };

} // namespace Engine
