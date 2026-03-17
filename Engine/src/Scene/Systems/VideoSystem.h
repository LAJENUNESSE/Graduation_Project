#pragma once

#include "Scene/Runtime/VideoRuntimeStore.h"

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

        void DestroyEntityVideo(uint32_t entityID);

        VideoRuntimeStore&       GetStore() { return m_Store; }
        const VideoRuntimeStore& GetStore() const { return m_Store; }

    private:
        VideoRuntimeStore m_Store;
    };

} // namespace Engine
