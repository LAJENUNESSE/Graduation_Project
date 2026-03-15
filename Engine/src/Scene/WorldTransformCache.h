#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <unordered_map>

namespace Engine
{

    class WorldTransformCache
    {
    public:
        void BeginFrame() { ++m_FrameCounter; }

        bool TryGet(entt::entity e, glm::mat4& out) const
        {
            auto it = m_Entries.find(static_cast<uint32_t>(e));
            if (it != m_Entries.end() && it->second.Frame == m_FrameCounter)
            {
                out = it->second.Matrix;
                return true;
            }
            return false;
        }

        void Put(entt::entity e, const glm::mat4& mat)
        {
            m_Entries[static_cast<uint32_t>(e)] = {m_FrameCounter, mat};
        }

    private:
        struct Entry
        {
            uint64_t Frame = 0;
            glm::mat4 Matrix{1.0f};
        };
        uint64_t m_FrameCounter = 0;
        std::unordered_map<uint32_t, Entry> m_Entries;
    };

} // namespace Engine
