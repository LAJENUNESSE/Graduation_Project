#pragma once

#include "Scene/Runtime/AudioRuntimeStore.h"

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

        void DestroyEntityAudio(uint32_t entityID);

        AudioRuntimeStore&       GetStore() { return m_Store; }
        const AudioRuntimeStore& GetStore() const { return m_Store; }

    private:
        AudioRuntimeStore m_Store;
    };

} // namespace Engine
