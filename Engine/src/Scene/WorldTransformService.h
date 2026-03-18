#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Engine
{

    class SceneEntityIndex;
    class WorldTransformCache;

    class WorldTransformService
    {
    public:
        static glm::mat4 ComputeWorldTransform(entt::registry&         reg,
                                               entt::entity            entity,
                                               const SceneEntityIndex& index,
                                               WorldTransformCache*    cache = nullptr);

    private:
        static constexpr int kMaxDepth = 64;
        static glm::mat4     ComputeWorldTransformImpl(entt::registry&         reg,
                                                       entt::entity            entity,
                                                       const SceneEntityIndex& index,
                                                       WorldTransformCache*    cache,
                                                       int                     depth);
    };

} // namespace Engine
