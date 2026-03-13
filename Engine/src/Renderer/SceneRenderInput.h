#pragma once

#include <entt/entt.hpp>

namespace Engine
{

    class SceneEntityIndex;

    struct SceneRenderInput
    {
        entt::registry* Registry = nullptr;
        const SceneEntityIndex* EntityIndex = nullptr;
        float DeltaTime = 0.0f;
    };

} // namespace Engine
