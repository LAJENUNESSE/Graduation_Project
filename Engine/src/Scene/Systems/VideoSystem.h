#pragma once

#include <entt/entt.hpp>

namespace Engine
{

    class VideoSystem
    {
    public:
        void Init();
        void Shutdown();

        void OnRuntimeStart(entt::registry& reg);
        void OnRuntimeStop(entt::registry& reg);
        void OnUpdate(entt::registry& reg, float dt);
    };

} // namespace Engine
