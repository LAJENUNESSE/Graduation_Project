#pragma once

#include <entt/entt.hpp>

namespace Engine
{

    class AudioSystem
    {
    public:
        void Init();
        void Shutdown();

        void OnRuntimeStart(entt::registry& reg);
        void OnRuntimeStop(entt::registry& reg);
        void OnUpdate(entt::registry& reg, float dt);
    };

} // namespace Engine
