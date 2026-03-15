#pragma once

#include <entt/entt.hpp>

namespace Engine
{

    class SceneEntityIndex;
    class WorldTransformCache;

    struct SceneRenderInput
    {
        entt::registry* Registry = nullptr;
        const SceneEntityIndex* EntityIndex = nullptr;
        float DeltaTime = 0.0f;
        WorldTransformCache* TransformCache = nullptr;
    };

} // namespace Engine
