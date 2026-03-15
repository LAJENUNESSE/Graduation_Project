#pragma once

#include <cstdint>
#include <unordered_map>

namespace Engine
{

    struct AudioRuntimeState
    {
        uint32_t Source = 0;
        uint32_t Buffer = 0;
        bool IsPlaying = false;
    };

    class AudioRuntimeStore
    {
    public:
        void Insert(uint32_t entityID, AudioRuntimeState state)
        {
            m_States[entityID] = state;
        }

        void Remove(uint32_t entityID)
        {
            m_States.erase(entityID);
        }

        AudioRuntimeState* Get(uint32_t entityID)
        {
            auto it = m_States.find(entityID);
            return it != m_States.end() ? &it->second : nullptr;
        }

        const AudioRuntimeState* Get(uint32_t entityID) const
        {
            auto it = m_States.find(entityID);
            return it != m_States.end() ? &it->second : nullptr;
        }

        void Clear() { m_States.clear(); }

        auto begin() { return m_States.begin(); }
        auto end() { return m_States.end(); }

    private:
        std::unordered_map<uint32_t, AudioRuntimeState> m_States;
    };

} // namespace Engine
