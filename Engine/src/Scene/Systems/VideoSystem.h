#pragma once

#include <entt/entt.hpp>
#include <thread>
#include <unordered_map>

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

        void OnVideoPlayerDestroyed(entt::registry& reg, entt::entity entity);

    private:
        // 后台打开视频流的线程，按 entity ID 索引
        std::unordered_map<uint32_t, std::thread> m_OpenThreads;
    };

} // namespace Engine
