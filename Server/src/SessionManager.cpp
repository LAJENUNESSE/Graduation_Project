#include "engpch.h"
#include "SessionManager.h"

#include "Core/Log.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"

#include <limits>

namespace Engine
{

    void SessionManager::BindPlayerEntities(Ref<Scene> scene)
    {
        m_Scene = std::move(scene);
        auto& registry = m_Scene->GetRegistry();

        for (auto [entity, tag] : registry.view<TagComponent>().each())
        {
            for (size_t i = 0; i < kMaxPlayers; ++i)
            {
                const std::string expectedTag = "Player" + std::to_string(i + 1);
                if (tag.Tag == expectedTag && !m_Slots[i].Occupied)
                {
                    m_Slots[i].Entity              = entity;
                    m_Slots[i].Tag                 = tag.Tag;
                    ENGINE_CORE_INFO("[SessionMgr] Slot {0} bound to entity '{1}'", i, tag.Tag);
                }
            }
        }
    }

    uint16_t SessionManager::AssignSlot(uint64_t sessionId)
    {
        for (size_t i = 0; i < kMaxPlayers; ++i)
        {
            if (!m_Slots[i].Occupied)
            {
                m_Slots[i].Occupied  = true;
                m_Slots[i].SessionId = sessionId;
                m_Slots[i].InputBits = 0;
                m_Slots[i].LastSeq   = 0;
                m_Slots[i].HasInput  = false;
                return static_cast<uint16_t>(i);
            }
        }
        return std::numeric_limits<uint16_t>::max();
    }

    void SessionManager::ReleaseSlot(uint64_t sessionId)
    {
        for (auto& slot : m_Slots)
        {
            if (slot.Occupied && slot.SessionId == sessionId)
            {
                slot.Occupied = false;
                slot.SessionId = 0;
                slot.InputBits = 0;
                slot.HasInput  = false;
            }
        }
    }

    uint16_t SessionManager::SlotOfSession(uint64_t sessionId) const
    {
        for (size_t i = 0; i < kMaxPlayers; ++i)
        {
            if (m_Slots[i].Occupied && m_Slots[i].SessionId == sessionId)
                return static_cast<uint16_t>(i);
        }
        return std::numeric_limits<uint16_t>::max();
    }

    void SessionManager::ApplyInput(uint64_t sessionId, uint16_t inputBits, uint32_t seq)
    {
        uint16_t slotIndex = SlotOfSession(sessionId);
        if (slotIndex >= kMaxPlayers)
            return;

        auto& slot = m_Slots[slotIndex];
        // 丢弃过期序号（乱序/重发保护）
        if (slot.HasInput && seq <= slot.LastSeq)
            return;

        slot.InputBits = inputBits;
        slot.LastSeq   = seq;
        slot.HasInput  = true;
    }

} // namespace Engine
